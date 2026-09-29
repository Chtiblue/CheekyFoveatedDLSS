#pragma once
#include "cheeky_gaze_abi.h"
#include "settings.hpp"
#include <Unknwn.h>
#include <string>
namespace cheeky::foveated_dlss {
// Called by the interception worker, after MinHook initialization. Observes a
// LibOVR runtime the game has already loaded (identified by its exports) and,
// on Pimax, the game's existing PVR session for eye tracking. Never loads,
// initializes or creates a VR runtime or session.
void poll_libovr_hooks() noexcept;
// Called after worker exit and before global MinHook teardown.
void stop_libovr_hooks() noexcept;
bool read_libovr_gaze(const Settings&, IUnknown*, CheekyGazeSnapshotV1&, std::uint64_t native_identity = 0) noexcept;
// Adapter state as a JSON object for runtime snapshots and support reports.
std::string libovr_gaze_json();
}
