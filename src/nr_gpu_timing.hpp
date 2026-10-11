#pragma once
// Experimental opt-in GPU timestamp diagnostics. This is intentionally isolated
// from NR output and uses recording lifetimes before reading GPU results.
#include "dlss_nr_lifetime.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <deque>
#include <memory>
#include <new>

namespace cheeky::foveated_dlss {
class NrGpuTiming {
    using ComResource = Microsoft::WRL::ComPtr<ID3D12Resource>;
    using ComHeap = Microsoft::WRL::ComPtr<ID3D12QueryHeap>;
public:
    struct Sample {
        ComHeap queries;
        ComResource readback;
        NrLifetime lifetime;
        std::uint64_t view{};
        unsigned pass{};
        unsigned width{}, height{};
    };
private:
    std::deque<std::unique_ptr<Sample>> pending_;
    std::uint64_t attempted_{};
public:
    // Must be called under the existing NR execution mutex; no GPU waits.
    static bool enabled() noexcept {
        static const bool value = [] {
            wchar_t flag[8]{};
            return GetEnvironmentVariableW(L"CHEEKY_NR_GPU_TIMING", flag, 8) != 0 &&
                flag[0] == L'1' && flag[1] == 0;
        }();
        return value;
    }
    Sample* begin(ID3D12GraphicsCommandList* list, ID3D12Device* device,
        std::uint64_t view, unsigned pass, unsigned width, unsigned height) noexcept {
        if (!enabled() || !list || !device || pending_.size() >= 32U) return nullptr;
        // A low sampling rate limits timestamp overhead and GPU readback memory.
        if (++attempted_ > 12U && attempted_ % 600U != 0U) return nullptr;
        std::unique_ptr<Sample> sample(new (std::nothrow) Sample());
        if (!sample) return nullptr;
        D3D12_QUERY_HEAP_DESC q{};
        q.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        q.Count = 2;
        if (FAILED(device->CreateQueryHeap(&q, IID_PPV_ARGS(&sample->queries)))) return nullptr;
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = sizeof(std::uint64_t) * 2U;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&sample->readback)))) return nullptr;
        if (!sample->lifetime.record(list)) return nullptr;
        sample->view = view;
        sample->pass = pass;
        sample->width = width;
        sample->height = height;
        list->EndQuery(sample->queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        auto* result = sample.get();
        pending_.push_back(std::move(sample));
        return result;
    }
    void end(ID3D12GraphicsCommandList* list, Sample* sample) noexcept {
        if (!sample) return;
        list->EndQuery(sample->queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        list->ResolveQueryData(sample->queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            0, 2, sample->readback.Get(), 0);
    }
    template<class Reporter>
    void collect(Reporter report) noexcept {
        for (auto it = pending_.begin(); it != pending_.end();) {
            auto& s = **it;
            s.lifetime.collect();
            if (!s.lifetime.empty()) { ++it; continue; }
            // Only after every command-list recording using these resources
            // has retired and all its queue fences have completed.
            std::uint64_t* timestamps{};
            const D3D12_RANGE read_range{0, sizeof(std::uint64_t) * 2U};
            if (SUCCEEDED(s.readback->Map(0, &read_range, reinterpret_cast<void**>(&timestamps)))) {
                if (timestamps && timestamps[1] >= timestamps[0])
                    report(s.pass, s.view, s.width, s.height, timestamps[1] - timestamps[0]);
                s.readback->Unmap(0, nullptr);
            }
            it = pending_.erase(it);
        }
    }
};
} // namespace cheeky::foveated_dlss
