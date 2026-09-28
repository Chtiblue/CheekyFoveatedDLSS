#pragma once
#include "libovr_abi.hpp"
#include "gaze_math.hpp"
#include "openvr_gaze_math.hpp"
#include "pvr_abi.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace cheeky::foveated_dlss {
enum class LibOVRLayout : std::uint32_t { unknown, current, legacy };

// The scene projection from one ovr_EndFrame/ovr_SubmitFrame layer list.
struct LibOVRProjection {
    std::array<libovr::SwapChain, 2> chains{};
    std::array<libovr::Recti, 2> viewports{};
    std::array<libovr::FovPort, 2> fovs{};
    std::array<libovr::Posef, 2> poses{};
    unsigned candidates{};
    bool valid{};
};

[[nodiscard]] inline bool libovr_fov_valid(const libovr::FovPort& fov) noexcept {
    for (const float tangent : {fov.UpTan, fov.DownTan, fov.LeftTan, fov.RightTan})
        if (!std::isfinite(tangent) || std::abs(tangent) > 20.F) return false;
    return fov.LeftTan + fov.RightTan > 0.01F && fov.UpTan + fov.DownTan > 0.01F;
}

[[nodiscard]] inline bool libovr_viewport_valid(const libovr::Recti& rect) noexcept {
    constexpr std::int32_t limit = 16384;
    return rect.x >= 0 && rect.y >= 0 && rect.width > 0 && rect.height > 0 &&
        rect.x <= limit && rect.y <= limit && rect.width <= limit && rect.height <= limit;
}

// Select the first stereo EyeFov-family layer. A missing right texture means
// the left texture carries both viewports, as documented for ovrLayerEyeFov.
// Only swap chains the caller has observed are accepted, so reading a list
// with the wrong header layout cannot hand arbitrary data to the runtime.
template <class Known>
[[nodiscard]] LibOVRProjection parse_libovr_projection(const void* const* layers, unsigned count,
    LibOVRLayout layout, Known&& known) noexcept {
    LibOVRProjection result;
    if (!layers || layout == LibOVRLayout::unknown) return result;
    for (unsigned index = 0; index < count && index < 64; ++index) {
        const auto* layer = static_cast<const unsigned char*>(layers[index]);
        if (!layer) continue;
        libovr::LegacyLayerHeader header{};
        std::memcpy(&header, layer, sizeof(header));
        if (header.Type != libovr::layer_eye_fov && header.Type != libovr::layer_eye_fov_depth &&
            header.Type != libovr::layer_eye_fov_multires) continue;
        LibOVRProjection candidate;
        const auto read = [&](const auto& eye_fov) {
            for (unsigned eye = 0; eye < 2; ++eye) {
                candidate.chains[eye] = eye_fov.ColorTexture[eye];
                candidate.viewports[eye] = eye_fov.Viewport[eye];
                candidate.fovs[eye] = eye_fov.Fov[eye];
                candidate.poses[eye] = eye_fov.RenderPose[eye];
            }
        };
        if (layout == LibOVRLayout::current) {
            libovr::LayerEyeFov eye_fov;
            std::memcpy(&eye_fov, layer, sizeof(eye_fov));
            read(eye_fov);
        } else {
            libovr::LegacyLayerEyeFov eye_fov;
            std::memcpy(&eye_fov, layer, sizeof(eye_fov));
            read(eye_fov);
        }
        if (!candidate.chains[1]) candidate.chains[1] = candidate.chains[0];
        bool valid = (header.Flags & libovr::layer_flag_texture_origin_at_bottom_left) == 0U;
        for (unsigned eye = 0; eye < 2; ++eye)
            valid = valid && candidate.chains[eye] && known(candidate.chains[eye]) &&
                libovr_viewport_valid(candidate.viewports[eye]) && libovr_fov_valid(candidate.fovs[eye]);
        if (!valid) continue;
        if (!result.valid) {
            const auto candidates = result.candidates;
            result = candidate;
            result.candidates = candidates;
            result.valid = true;
        }
        ++result.candidates;
    }
    return result;
}

// OpenVR-style raw tangents (left/top negative) for openvr_project_direction.
struct LibOVRRawProjection { float left{}, right{}, top{}, bottom{}; };
[[nodiscard]] inline LibOVRRawProjection libovr_raw_projection(const libovr::FovPort& fov) noexcept {
    return {-fov.LeftTan, fov.RightTan, -fov.UpTan, fov.DownTan};
}

// Rotation in the 3x4 eye-to-head layout used by openvr_project_direction.
inline void eye_rotation_matrix(gaze_math::Quaternion q, float matrix[3][4]) noexcept {
    const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!std::isfinite(length) || length < 0.5F || length > 2.F) q = {0, 0, 0, 1};
    else { q.x /= length; q.y /= length; q.z /= length; q.w /= length; }
    matrix[0][0] = 1 - 2 * (q.y * q.y + q.z * q.z);
    matrix[0][1] = 2 * (q.x * q.y - q.w * q.z);
    matrix[0][2] = 2 * (q.x * q.z + q.w * q.y);
    matrix[1][0] = 2 * (q.x * q.y + q.w * q.z);
    matrix[1][1] = 1 - 2 * (q.x * q.x + q.z * q.z);
    matrix[1][2] = 2 * (q.y * q.z - q.w * q.x);
    matrix[2][0] = 2 * (q.x * q.z - q.w * q.y);
    matrix[2][1] = 2 * (q.y * q.z + q.w * q.x);
    matrix[2][2] = 1 - 2 * (q.x * q.x + q.y * q.y);
    matrix[0][3] = matrix[1][3] = matrix[2][3] = 0;
}

// Each eye's rotation relative to the shared forward of the submitted render
// poses: the game's actual (possibly parallel) projection, not the optics.
// Head-relative gaze needs no tracking-space head pose. Identity on failure.
inline bool libovr_eye_rotations(const std::array<libovr::Posef, 2>& poses, float matrices[2][3][4]) noexcept {
    std::array<gaze_math::Pose, 2> eyes{};
    bool valid = true;
    for (unsigned eye = 0; eye < 2; ++eye) {
        const auto& q = poses[eye].Orientation;
        const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        valid = valid && std::isfinite(length) && length > 0.9F && length < 1.1F;
        if (valid) eyes[eye].orientation = {q.x / length, q.y / length, q.z / length, q.w / length};
    }
    gaze_math::Pose head;
    valid = valid && gaze_math::stereo_forward_pose(eyes[0], eyes[1], head);
    for (unsigned eye = 0; eye < 2; ++eye) {
        gaze_math::Quaternion relative{0, 0, 0, 1};
        if (valid) {
            // conjugate(head) * eye
            const auto& a = head.orientation;
            const auto& b = eyes[eye].orientation;
            relative = {a.w * b.x - a.x * b.w - a.y * b.z + a.z * b.y,
                a.w * b.y + a.x * b.z - a.y * b.w - a.z * b.x,
                a.w * b.z - a.x * b.y + a.y * b.x - a.z * b.w,
                a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z};
        }
        eye_rotation_matrix(relative, matrices[eye]);
    }
    return valid;
}

// One head-relative ray from both eyes' gaze tangents, as Pimax's OpenXR eye
// gaze interaction reports it. Components follow OpenVR/OpenXR axes (y up, -z
// forward), which PVR's tangents already use.
[[nodiscard]] inline bool pvr_combined_gaze_ray(const pvr::EyeTrackingInfo& info, float ray[3]) noexcept {
    if (info.TimeInSeconds == 0) return false;
    const float x = (info.GazeTan[0].x + info.GazeTan[1].x) * .5F;
    const float y = (info.GazeTan[0].y + info.GazeTan[1].y) * .5F;
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 10.F || std::abs(y) > 10.F) return false;
    ray[0] = x; ray[1] = y; ray[2] = -1.F;
    return true;
}
}  // namespace cheeky::foveated_dlss
