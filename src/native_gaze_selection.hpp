#pragma once
#include "cheeky_gaze_abi.h"
#include "settings.hpp"
#include <algorithm>

namespace cheeky::foveated_dlss {
// Publication order is not a measure of usefulness: an inner runtime can
// publish after the outer compositor without having gaze or a source mapping.
inline unsigned native_gaze_quality(const CheekyGazeSnapshotV1& sample,
    std::uint64_t resource, const StereoEyeAssignment& assignment,
    std::uint64_t now, std::uint64_t frequency, bool use_gaze) noexcept {
    if (sample.abi_version != CHEEKY_GAZE_ABI_VERSION || sample.structure_size < sizeof(sample) ||
        !sample.session_generation || !sample.publication_qpc || !frequency ||
        now < sample.publication_qpc || now - sample.publication_qpc > frequency / 20 ||
        !(sample.status_flags & CHEEKY_GAZE_STATUS_SESSION_FOCUSED)) return 0;
    if (!(sample.status_flags & CHEEKY_GAZE_STATUS_MAPPING_READY) ||
        (sample.status_flags & (CHEEKY_GAZE_STATUS_AMBIGUOUS_RESOURCE | CHEEKY_GAZE_STATUS_UNSUPPORTED_VIEW_CONFIG))) return 1;
    bool mapped = assignment.calibrated && sample.view_count == 2 &&
        ((sample.status_flags & CHEEKY_GAZE_STATUS_OPENVR) ? assignment.calibration_session == 0 :
            assignment.calibration_session == sample.session_generation);
    for (unsigned i = 0; i < (std::min)(sample.view_count, CHEEKY_GAZE_MAX_VIEWS); ++i)
        mapped |= resource && sample.views[i].resource_identity == resource &&
            (sample.views[i].flags & CHEEKY_GAZE_VIEW_RESOURCE_VALID);
    return 2 + (mapped ? 4 : 0) +
        (use_gaze && (sample.status_flags & CHEEKY_GAZE_STATUS_GAZE_VALID) ? 1 : 0);
}
} // namespace cheeky::foveated_dlss
