#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>

// Pimax PVR client fixture (libPVRClient64.dll). The interface table lives in
// this DLL and the LibOVR fixture calls through it, like Pimax's runtime does.
// Only the PVR SDK 1.23-1.32 slots Cheeky reads or observes are populated.
namespace {
struct Vector2f { float x, y; };
struct EyeTrackingInfo { Vector2f gaze[2]; double time; float convergence; float blink[2]; };
int hmd_storage{};
std::atomic<unsigned> end_frames{}, tracking_queries{}, eye_queries{}, destroyed{};
std::atomic<bool> valid{true};
std::atomic<unsigned> clock_mode{}; // 0 advancing, 1 frozen, 2 NaN
float gaze_x{0.2F}, gaze_y{-0.1F};
double sample_time{};
__declspec(noinline) int create_hmd(void** hmd) { *hmd = &hmd_storage; return 0; }
__declspec(noinline) void destroy_hmd(void* hmd) { if (hmd == &hmd_storage) ++destroyed; }
__declspec(noinline) double time_seconds() { return static_cast<double>(GetTickCount64()) / 1000.0; }
__declspec(noinline) int tracking_state(void* hmd, double, void*) { ++tracking_queries; return hmd == &hmd_storage ? 0 : -1; }
__declspec(noinline) int end_frame(void* hmd, long long, const void* const*, unsigned) { ++end_frames; return hmd == &hmd_storage ? 0 : -1; }
__declspec(noinline) int submit_frame(void* hmd, long long, const void* const*, unsigned) { ++end_frames; return hmd == &hmd_storage ? 0 : -1; }
__declspec(noinline) int eye_tracking(void* hmd, double, EyeTrackingInfo* info) {
    ++eye_queries;
    if (hmd != &hmd_storage || !info) return -1;
    *info = {};
    if (!valid) return 0;
    if (clock_mode == 0) sample_time += 0.004;
    info->gaze[0] = info->gaze[1] = {gaze_x, gaze_y};
    info->time = clock_mode == 2 ? std::numeric_limits<double>::quiet_NaN() : sample_time;
    return 0;
}
struct Table { void* slots[66]; } table = [] {
    Table value{};
    value.slots[2] = reinterpret_cast<void*>(&create_hmd);
    value.slots[3] = reinterpret_cast<void*>(&destroy_hmd);
    value.slots[5] = reinterpret_cast<void*>(&time_seconds);
    value.slots[13] = reinterpret_cast<void*>(&tracking_state);
    value.slots[31] = reinterpret_cast<void*>(&end_frame);
    value.slots[34] = reinterpret_cast<void*>(&submit_frame);
    value.slots[65] = reinterpret_cast<void*>(&eye_tracking);
    return value;
}();
}
extern "C" __declspec(dllexport) void* getPvrInterface(std::uint32_t major, std::uint32_t minor) {
    return major == 1 && minor >= 20 && minor <= 32 ? &table : nullptr;
}
extern "C" __declspec(dllexport) void CheekyFakePVR_SetGaze(float x, float y, bool value) { gaze_x = x; gaze_y = y; valid = value; }
extern "C" __declspec(dllexport) void CheekyFakePVR_SetClockMode(unsigned mode) { clock_mode = mode; }
extern "C" __declspec(dllexport) unsigned CheekyFakePVR_EndFrames() { return end_frames.load(); }
extern "C" __declspec(dllexport) unsigned CheekyFakePVR_EyeQueries() { return eye_queries.load(); }
extern "C" __declspec(dllexport) unsigned CheekyFakePVR_Destroyed() { return destroyed.load(); }
// The table slot the runtime patched, so tests can prove detours forward.
extern "C" __declspec(dllexport) bool CheekyFakePVR_EndFrameObserved() { return table.slots[31] != reinterpret_cast<void*>(&end_frame); }
