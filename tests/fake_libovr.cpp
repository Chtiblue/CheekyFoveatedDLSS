#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

// LibOVR 1.x runtime fixture (LibOVRRT64_1.dll), shaped like Pimax's
// compatibility runtime: sessions and frames go through an already loaded
// libPVRClient64.dll interface table. D3D11 swap chains only.
using Microsoft::WRL::ComPtr;
namespace {
using CreateHmd = int (*)(void**);
using DestroyHmd = void (*)(void*);
using PvrFrame = int (*)(void*, long long, const void* const*, unsigned);
struct Session { void* hmd{}; };
struct Chain {
    std::vector<ComPtr<ID3D11Texture2D>> textures;
    int current{};
};
// Test-owned swap-chain description: {width, height}.
struct ChainDesc { unsigned width, height; };
void** pvr_table{};
void* table_copy[66]{};
std::mutex mutex;
std::atomic<bool> borrowed_buffers{}, copy_table{};
std::atomic<unsigned> frames{}, commits{}, initialized_minor{};
void** table() {
    if (!pvr_table) {
        const auto module = GetModuleHandleW(L"LibPVRClient64.dll");
        const auto get = module ? reinterpret_cast<void** (*)(std::uint32_t, std::uint32_t)>(
            GetProcAddress(module, "getPvrInterface")) : nullptr;
        pvr_table = get ? get(1, 26) : nullptr;
        // A runtime may keep its own copy of the interface functions.
        if (pvr_table && copy_table) {
            std::memcpy(table_copy, pvr_table, sizeof(table_copy));
            pvr_table = table_copy;
        }
    }
    return pvr_table;
}
__declspec(noinline) int frame(Session* session, const void* const* layers, unsigned count, bool end) {
    ++frames;
    auto** t = table();
    // Calls go through the table on each frame, as with PVR_API.h.
    if (t && session) reinterpret_cast<PvrFrame>(t[end ? 31 : 34])(session->hmd, frames, layers, count);
    return 0;
}
}

extern "C" __declspec(dllexport) int ovr_Initialize(const std::uint32_t* params) {
    initialized_minor = params && (params[0] & 4U) ? params[1] : 0U;
    return 0;
}
extern "C" __declspec(dllexport) void ovr_Shutdown() { initialized_minor = 0; }
extern "C" __declspec(dllexport) int ovr_Create(Session** output, void*) {
    auto** t = table();
    if (!output || !t) return -1000;
    auto* session = new Session;
    reinterpret_cast<CreateHmd>(t[2])(&session->hmd);
    *output = session;
    return 0;
}
extern "C" __declspec(dllexport) void ovr_Destroy(Session* session) {
    if (!session) return;
    if (auto** t = table()) reinterpret_cast<DestroyHmd>(t[3])(session->hmd);
    delete session;
}
extern "C" __declspec(dllexport) int ovr_CreateTextureSwapChainDX(Session*, IUnknown* d3d, const ChainDesc* desc, Chain** output) {
    ComPtr<ID3D11Device> device;
    if (!d3d || !desc || !output || FAILED(d3d->QueryInterface(IID_PPV_ARGS(&device)))) return -1005;
    auto* chain = new Chain;
    D3D11_TEXTURE2D_DESC texture{desc->width, desc->height, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
        D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0};
    chain->textures.resize(3);
    for (auto& value : chain->textures)
        if (FAILED(device->CreateTexture2D(&texture, nullptr, &value))) { delete chain; return -1005; }
    *output = chain;
    return 0;
}
extern "C" __declspec(dllexport) void ovr_DestroyTextureSwapChain(Session*, Chain* chain) { delete chain; }
extern "C" __declspec(dllexport) int ovr_GetTextureSwapChainCurrentIndex(Session*, Chain* chain, int* index) {
    if (!chain || !index) return -1005;
    *index = chain->current;
    return 0;
}
extern "C" __declspec(dllexport) int ovr_GetTextureSwapChainBufferDX(Session*, Chain* chain, int index, IID iid, void** output) {
    if (!chain || !output) return -1005;
    if (index < 0) index = chain->current;
    if (index >= static_cast<int>(chain->textures.size())) return -1005;
    if (borrowed_buffers) {
        // Some compatibility runtimes hand out their own reference.
        if (iid != __uuidof(ID3D11Texture2D)) return -1005;
        *output = chain->textures[static_cast<std::size_t>(index)].Get();
        return 0;
    }
    return SUCCEEDED(chain->textures[static_cast<std::size_t>(index)]->QueryInterface(iid, output)) ? 0 : -1005;
}
extern "C" __declspec(dllexport) int ovr_CommitTextureSwapChain(Session*, Chain* chain) {
    if (!chain) return -1005;
    std::lock_guard lock(mutex);
    ++commits;
    chain->current = (chain->current + 1) % static_cast<int>(chain->textures.size());
    return 0;
}
extern "C" __declspec(dllexport) int ovr_EndFrame(Session* session, long long, const void*, const void* const* layers, unsigned count) {
    return frame(session, layers, count, true);
}
extern "C" __declspec(dllexport) int ovr_SubmitFrame2(Session* session, long long, const void*, const void* const* layers, unsigned count) {
    return frame(session, layers, count, false);
}
extern "C" __declspec(dllexport) void CheekyFakeLibOVR_SetBorrowedBuffers(bool value) { borrowed_buffers = value; }
// Copy now, as a runtime that initialized before Cheeky attached would have.
extern "C" __declspec(dllexport) void CheekyFakeLibOVR_CopyPvrTable() { copy_table = true; table(); }
extern "C" __declspec(dllexport) unsigned CheekyFakeLibOVR_Frames() { return frames.load(); }
extern "C" __declspec(dllexport) unsigned CheekyFakeLibOVR_InitializedMinor() { return initialized_minor.load(); }
// Current reference count of a swap-chain image, excluding this query.
extern "C" __declspec(dllexport) unsigned long CheekyFakeLibOVR_References(Chain* chain, int index) {
    auto* texture = chain->textures[static_cast<std::size_t>(index)].Get();
    texture->AddRef();
    return texture->Release();
}
