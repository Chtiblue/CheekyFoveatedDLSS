#pragma once
#include <cstdint>

namespace cheeky::foveated_dlss {
inline constexpr const char* nr_network_help(std::uint32_t value) noexcept {
    switch (value) {
    case 1: return "Preset A: requests the older, larger NR network in DLL 310.9.16. This is independent of appearance Model A/B/C.";
    case 2: return "Preset B: requests the newer, smaller/faster NR network in DLL 310.9.16. Actual speed and image quality depend on your setup.";
    default: return "Default: lets the NR DLL choose its network (reported as B in 310.9.16). Default does not mean appearance Model A or Model C.";
    }
}
inline constexpr const char* nr_appearance_help(std::uint32_t value) noexcept {
    switch (value) {
    case 1: return "Model B: RenoDX appearance mode B (API Style=1; previously labeled Natural). Changes network conditioning and associated grading, not the network preset.";
    case 2: return "Model C: RenoDX appearance mode C (API Style=2; previously labeled Cinematic). Changes network conditioning and associated grading, not the network preset.";
    default: return "Model A: RenoDX appearance mode A (API Style=0; previously labeled Standard). Baseline appearance mode, not NR off and not network Preset A.";
    }
}
inline constexpr const char* nr_network_version_help =
    "Network descriptions are version-specific: 310.8.0 had one network; 310.9.16 supports A/B. Patched DLL behavior is not verified here. Appearance A/B/C are not quality levels.";
}
