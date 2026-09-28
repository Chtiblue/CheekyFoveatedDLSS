#pragma once
#include <cstddef>
#include <cstdint>

// Minimal mirror of the Pimax PVR client interface (libPVRClient64.dll,
// getPvrInterface). Only the prefix shared by SDK 1.23 through 1.32 is used;
// every entry up to getEyeTrackingInfo keeps its position in those versions.
namespace cheeky::foveated_dlss::pvr {
using Result = std::int32_t;
using HmdHandle = void*;
constexpr Result success = 0;
constexpr std::uint32_t major_version = 1;
constexpr std::uint32_t newest_minor_version = 32;
constexpr std::uint32_t oldest_minor_version = 23;

struct Vector2f { float x, y; };

// GazeTan is x right-positive and y up-positive (confirmed on a Pimax Crystal).
// TimeInSeconds == 0 marks an
// invalid sample. Clients before 1.30 write only GazeTan and TimeInSeconds.
struct alignas(8) EyeTrackingInfo {
    Vector2f GazeTan[2];
    double TimeInSeconds;
    float ConvergenceDistance;
    float blink[2];
};

// Positions in pvrInterfaceV23..V32; all other entries are opaque.
enum Slot : std::size_t {
    slot_destroy_hmd = 3,
    slot_get_time_seconds = 5,
    slot_get_tracking_state = 13,
    slot_end_frame = 31,
    slot_submit_frame = 34,
    slot_get_eye_tracking_info = 65,
    slot_count = 66,
};
struct Interface { void* slots[slot_count]; };

using GetInterfaceFn = Interface* (*)(std::uint32_t major, std::uint32_t minor);
using DestroyHmdFn = void (*)(HmdHandle);
using GetTimeSecondsFn = double (*)();
using GetTrackingStateFn = Result (*)(HmdHandle, double, void*);
using FrameFn = Result (*)(HmdHandle, long long, const void* const*, unsigned);
using GetEyeTrackingInfoFn = Result (*)(HmdHandle, double, EyeTrackingInfo*);

static_assert(offsetof(EyeTrackingInfo, TimeInSeconds) == 16 && sizeof(EyeTrackingInfo) == 40);
}  // namespace cheeky::foveated_dlss::pvr
