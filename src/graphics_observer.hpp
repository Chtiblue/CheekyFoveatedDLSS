#pragma once
#include <d3d12.h>
#include <cstdint>
namespace cheeky::foveated_dlss {
struct NativeObserverStatus {
    bool ready{};
    std::uint64_t submissions{}, copies{}, resets{}, destroyed{};
};
bool initialize_native_observer(ID3D12Device*, ID3D12CommandQueue*) noexcept;
bool ensure_native_observer(ID3D12GraphicsCommandList*) noexcept;
NativeObserverStatus native_observer_status() noexcept;
std::uint64_t observe_native_resource(ID3D12Resource*) noexcept;
// Diagnostic only: last depth binding recorded on a given command list.
struct StereoDepthBinding { ID3D12Resource* resource{}; std::uint32_t first_slice{UINT32_MAX}, array_size{}; std::uint64_t sequence{}; };
StereoDepthBinding observe_stereo_depth_binding(ID3D12GraphicsCommandList*) noexcept;
} // namespace cheeky::foveated_dlss
