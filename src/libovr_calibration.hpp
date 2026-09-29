#pragma once
#include "../openxr_layer/eye_calibration.hpp"

namespace cheeky::foveated_dlss {
// Legacy alternate-eye rendering commits one eye per LibOVR frame and
// resubmits the other eye's previous image. Keep the interval open for one
// more frame so the second eye's commit completes the pair; the core stamps
// intervening renders with the same markers.
[[nodiscard]] inline bool libovr_await_second_eye(const openxr_calibration::Frame& frame, bool usable,
                                                  bool deferred) noexcept {
    if (!usable || !frame.active || deferred || unsigned(frame.released[0]) + unsigned(frame.released[1]) != 1)
        return false;
    return frame.tickets[0] ? !frame.tickets[1] && frame.history[1].valid()
                            : frame.tickets[1] && frame.history[0].valid();
}
}  // namespace cheeky::foveated_dlss
