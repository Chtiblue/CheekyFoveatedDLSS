#pragma once
#include <algorithm>
#include <cstdint>

namespace cheeky::foveated_dlss {
struct ScaledRange {
    std::uint32_t base{};
    std::uint32_t extent{};
};

// Maps [base, base + extent) from output_extent space to source_extent space,
// widening to whole source pixels.
[[nodiscard]] inline ScaledRange scale_range(
    const std::uint32_t base,
    const std::uint32_t extent,
    const std::uint32_t source_extent,
    const std::uint32_t output_extent
) noexcept {
    const auto scaled_base = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(base) * source_extent / output_extent
    );
    const auto scaled_end = static_cast<std::uint32_t>((std::min)(
        static_cast<std::uint64_t>(source_extent),
        (static_cast<std::uint64_t>(base + extent) * source_extent +
            output_extent - 1U) / output_extent
    ));
    return {scaled_base, (std::max)(1U, scaled_end - scaled_base)};
}

// Upper bound of scale_range(base, extent, ...).extent for any base. Guide
// textures sized by it stay allocated while a gaze-following region moves.
[[nodiscard]] inline std::uint32_t scaled_capacity(
    const std::uint32_t extent,
    const std::uint32_t source_extent,
    const std::uint32_t output_extent
) noexcept {
    const auto scaled = (static_cast<std::uint64_t>(extent) * source_extent +
        output_extent - 1U) / output_extent + 1U;
    return static_cast<std::uint32_t>((std::max)(std::uint64_t{1U},
        (std::min)(static_cast<std::uint64_t>(source_extent), scaled)));
}
}  // namespace cheeky::foveated_dlss
