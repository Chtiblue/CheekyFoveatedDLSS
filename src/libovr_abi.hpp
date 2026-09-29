#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>

// Minimal mirror of the public Oculus PC SDK 1.x C ABI (OVR_CAPI.h and
// OVR_CAPI_D3D.h). Only frame submission and swap-chain lookup are used.
namespace cheeky::foveated_dlss::libovr {
using Result = std::int32_t;
using Session = void*;
using SwapChain = void*;
constexpr Result success = 0;
[[nodiscard]] constexpr bool succeeded(Result result) noexcept { return result >= 0; }

constexpr std::uint32_t init_request_version = 0x00000004U;
struct alignas(8) InitParams {
    std::uint32_t Flags;
    std::uint32_t RequestedMinorVersion;
    void* LogCallback;
    std::uintptr_t UserData;
    std::uint32_t ConnectionTimeoutMS;
    std::uint32_t pad0;
};

struct Recti { std::int32_t x, y, width, height; };
struct FovPort { float UpTan, DownTan, LeftTan, RightTan; };
struct Quatf { float x, y, z, w; };
struct Vector3f { float x, y, z; };
struct Posef { Quatf Orientation; Vector3f Position; };

enum LayerType : std::int32_t {
    layer_eye_fov = 1,
    layer_eye_fov_depth = 2,
    layer_eye_fov_multires = 7,
};
constexpr std::uint32_t layer_flag_texture_origin_at_bottom_left = 0x02U;

// SDK 1.25 appended 128 reserved bytes to ovrLayerHeader. The runtime knows the
// client's requested minor version; older clients submit the 8-byte header.
constexpr std::uint32_t reserved_header_minor_version = 25;
struct alignas(8) LayerHeader { std::int32_t Type; std::uint32_t Flags; char Reserved[128]; };
struct alignas(8) LegacyLayerHeader { std::int32_t Type; std::uint32_t Flags; };

// EyeFov, EyeFovDepth and EyeFovMultires share this prefix.
template <class Header> struct alignas(8) LayerEyeFovPrefix {
    Header header;
    SwapChain ColorTexture[2];
    Recti Viewport[2];
    FovPort Fov[2];
    Posef RenderPose[2];
    double SensorSampleTime;
};
using LayerEyeFov = LayerEyeFovPrefix<LayerHeader>;
using LegacyLayerEyeFov = LayerEyeFovPrefix<LegacyLayerHeader>;
static_assert(offsetof(LayerEyeFov, ColorTexture) == 136 && sizeof(LayerEyeFov) == 280);
static_assert(offsetof(LegacyLayerEyeFov, ColorTexture) == 8 && sizeof(LegacyLayerEyeFov) == 152);

using InitializeFn = Result (*)(const InitParams*);
using ShutdownFn = void (*)();
using CreateFn = Result (*)(Session*, void*);
using DestroyFn = void (*)(Session);
using CreateSwapChainDXFn = Result (*)(Session, IUnknown*, const void*, SwapChain*);
using DestroySwapChainFn = void (*)(Session, SwapChain);
using GetCurrentIndexFn = Result (*)(Session, SwapChain, int*);
using GetBufferDXFn = Result (*)(Session, SwapChain, int, IID, void**);
using CommitFn = Result (*)(Session, SwapChain);
using EndFrameFn = Result (*)(Session, long long, const void*, const void* const*, unsigned);
}  // namespace cheeky::foveated_dlss::libovr
