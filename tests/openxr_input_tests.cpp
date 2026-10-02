#include <Windows.h>
#include "../third_party/openxr/include/openxr/openxr.h"
#include "../third_party/openxr/include/openxr/openxr_loader_negotiation.h"
#include "cheeky_gaze_abi.h"
#include "gaze_foveation.hpp"
#include "../openxr_layer/menu_geometry.hpp"
#include "../uevr/support_bundle.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class T> T handle(std::uintptr_t n) { return reinterpret_cast<T>(n); }
struct Runtime {
    inline static bool extension = true, supported = true, focused = true, active = true;
    inline static bool attached{}, synced{}, injected{};
    inline static unsigned probe_failure{}; // 1: lookup, 2: count, 3: list
    inline static bool cylinder{};
    inline static XrResult create_result = XR_SUCCESS;
    inline static std::vector<std::vector<std::string>> create_requests;
    inline static XrResult attach_result = XR_SUCCESS, sync_result = XR_SUCCESS,
        binding_result = XR_SUCCESS, locate_result = XR_SUCCESS, space_result = XR_SUCCESS;
    inline static unsigned attaches{}, syncs{}, bindings{}, locates{}, created{};
    inline static std::vector<XrActionSet> attached_sets;
    inline static std::vector<XrActiveActionSet> active_sets;
    static void reset() {
        extension = supported = focused = active = true;
        attached = synced = injected = false;
        probe_failure = 0; cylinder = false; create_result = XR_SUCCESS; create_requests.clear();
        attach_result = sync_result = binding_result = locate_result = space_result = XR_SUCCESS;
        attaches = syncs = bindings = locates = created = 0;
        attached_sets.clear(); active_sets.clear();
    }
    static XrResult XRAPI_CALL create(const XrInstanceCreateInfo* info,
        const XrApiLayerCreateInfo*, XrInstance* instance) {
        create_requests.emplace_back();
        bool requested_gaze{}, requested_cylinder{};
        for (unsigned i = 0; i < info->enabledExtensionCount; ++i) {
            create_requests.back().emplace_back(info->enabledExtensionNames[i]);
            requested_gaze |= !strcmp(info->enabledExtensionNames[i], XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME);
            requested_cylinder |= !strcmp(info->enabledExtensionNames[i], XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
        }
        if (XR_FAILED(create_result)) return create_result;
        if ((requested_gaze && !extension) || (requested_cylinder && !cylinder)) return XR_ERROR_EXTENSION_NOT_PRESENT;
        injected = requested_gaze;
        *instance = handle<XrInstance>(1); return XR_SUCCESS;
    }
    static XrResult XRAPI_CALL get(XrInstance, const char* name, PFN_xrVoidFunction* out) {
        *out = nullptr;
        if (probe_failure == 1 && !strcmp(name, "xrEnumerateInstanceExtensionProperties"))
            return XR_ERROR_FUNCTION_UNSUPPORTED;
#define FN(n, ...) if (!strcmp(name, n)) { *out = reinterpret_cast<PFN_xrVoidFunction>(+__VA_ARGS__); return XR_SUCCESS; }
        FN("xrEnumerateInstanceExtensionProperties", [](const char*, uint32_t capacity, uint32_t* count, XrExtensionProperties* props) {
            if (probe_failure == 2 || (probe_failure == 3 && capacity)) return XR_ERROR_RUNTIME_FAILURE;
            *count = probe_failure == 3 ? 1U : unsigned(extension) + unsigned(cylinder);
            if (capacity) {
                unsigned i{};
                if (extension) strcpy_s(props[i++].extensionName, XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME);
                if (cylinder) strcpy_s(props[i].extensionName, XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
            }
            return XR_SUCCESS;
        });
        FN("xrGetInstanceProperties", [](XrInstance, XrInstanceProperties* p) { strcpy_s(p->runtimeName, "Gaze mock"); return XR_SUCCESS; });
        FN("xrDestroyInstance", [](XrInstance) { return XR_SUCCESS; });
        FN("xrCreateSession", [](XrInstance, const XrSessionCreateInfo*, XrSession* s) { *s = handle<XrSession>(2); return XR_SUCCESS; });
        FN("xrDestroySession", [](XrSession) { return XR_SUCCESS; });
        FN("xrBeginSession", [](XrSession, const XrSessionBeginInfo*) { return XR_SUCCESS; });
        FN("xrEndSession", [](XrSession) { return XR_SUCCESS; });
        FN("xrBeginFrame", [](XrSession, const XrFrameBeginInfo*) { return XR_SUCCESS; });
        FN("xrGetSystemProperties", [](XrInstance, XrSystemId, XrSystemProperties* p) {
            auto* gaze = static_cast<XrSystemEyeGazeInteractionPropertiesEXT*>(p->next);
            if (gaze) gaze->supportsEyeGazeInteraction = supported; return XR_SUCCESS;
        });
        FN("xrCreateActionSet", [](XrInstance, const XrActionSetCreateInfo*, XrActionSet* set) {
            *set = handle<XrActionSet>(10 + created++); return XR_SUCCESS;
        });
        FN("xrDestroyActionSet", [](XrActionSet) { return XR_SUCCESS; });
        FN("xrCreateAction", [](XrActionSet, const XrActionCreateInfo*, XrAction* a) { *a = handle<XrAction>(20); return XR_SUCCESS; });
        FN("xrDestroyAction", [](XrAction) { return XR_SUCCESS; });
        FN("xrStringToPath", [](XrInstance, const char* path, XrPath* p) {
            *p = strstr(path, "interaction_profiles") ? 31 : 30; return XR_SUCCESS;
        });
        FN("xrSuggestInteractionProfileBindings", [](XrInstance, const XrInteractionProfileSuggestedBinding* info) {
            ++bindings;
            require((info->countSuggestedBindings == 1 || info->countSuggestedBindings == 4) &&
                info->suggestedBindings[0].action == handle<XrAction>(20), "Gaze/menu binding missing");
            return binding_result;
        });
        FN("xrCreateActionSpace", [](XrSession, const XrActionSpaceCreateInfo*, XrSpace* space) {
            if (XR_SUCCEEDED(space_result)) *space = handle<XrSpace>(40); return space_result;
        });
        FN("xrDestroySpace", [](XrSpace) { return XR_SUCCESS; });
        FN("xrAttachSessionActionSets", [](XrSession, const XrSessionActionSetsAttachInfo* info) {
            ++attaches;
            if (attached) return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
            if (XR_FAILED(attach_result)) return attach_result;
            attached_sets.assign(info->actionSets, info->actionSets + info->countActionSets);
            attached = true; return XR_SUCCESS;
        });
        FN("xrSyncActions", [](XrSession, const XrActionsSyncInfo* info) {
            ++syncs;
            if (!attached) return XR_ERROR_ACTIONSET_NOT_ATTACHED;
            active_sets.assign(info->activeActionSets, info->activeActionSets + info->countActiveActionSets);
            for (const auto& active : active_sets) {
                require(std::find(attached_sets.begin(), attached_sets.end(), active.actionSet) != attached_sets.end(),
                    "Sync included an unattached action set");
            }
            if (!focused) { synced = false; return XR_SESSION_NOT_FOCUSED; }
            synced = sync_result == XR_SUCCESS; return sync_result;
        });
        FN("xrGetActionStatePose", [](XrSession, const XrActionStateGetInfo*, XrActionStatePose* p) {
            p->isActive = attached && synced && focused && active; return XR_SUCCESS;
        });
        FN("xrLocateSpace", [](XrSpace, XrSpace, XrTime time, XrSpaceLocation* p) {
            ++locates;
            // Deliberately set flags even on failure: the layer must reject them.
            p->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
            p->pose.orientation.w = 1;
            static_cast<XrEyeGazeSampleTimeEXT*>(p->next)->time = time;
            return locate_result;
        });
        FN("xrLocateViews", [](XrSession, const XrViewLocateInfo*, XrViewState* state, uint32_t cap, uint32_t* count, XrView* views) {
            *count = 2;
            if (cap < 2) return XR_ERROR_SIZE_INSUFFICIENT;
            state->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT;
            for (unsigned i = 0; i < 2; ++i) { views[i].pose.orientation.w = 1; views[i].fov = {-0.8F,0.8F,0.8F,-0.8F}; }
            return XR_SUCCESS;
        });
        FN("xrPollEvent", [](XrInstance, XrEventDataBuffer* buffer) {
            XrEventDataSessionStateChanged event{XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED};
            event.session = handle<XrSession>(2);
            event.state = focused ? XR_SESSION_STATE_FOCUSED : XR_SESSION_STATE_VISIBLE;
            memcpy(buffer, &event, sizeof(event)); return XR_SUCCESS;
        });
#undef FN
        return XR_ERROR_FUNCTION_UNSUPPORTED;
    }
};
struct Layer {
    HMODULE module{};
    XrInstance instance{};
    XrSession session{};
    PFN_xrGetInstanceProcAddr get{};
    template<class T> T fn(const char* name) {
        PFN_xrVoidFunction f{};
        require(get(instance, name, &f) == XR_SUCCESS && f, "Missing layer function");
        return reinterpret_cast<T>(f);
    }
    Layer(bool menu = false, std::vector<const char*> extensions = {}, XrResult expected = XR_SUCCESS) {
        module = LoadLibraryW(L"CheekyOpenXRLayer.dll"); require(module != nullptr, "Missing layer DLL");
        const auto negotiate = reinterpret_cast<PFN_xrNegotiateLoaderApiLayerInterface>(GetProcAddress(module, "xrNegotiateLoaderApiLayerInterface"));
        XrNegotiateLoaderInfo loader{};
        loader.structType = XR_LOADER_INTERFACE_STRUCT_LOADER_INFO; loader.structVersion = XR_LOADER_INFO_STRUCT_VERSION;
        loader.structSize = sizeof(loader); loader.minInterfaceVersion = loader.maxInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
        loader.maxApiVersion = XR_CURRENT_API_VERSION;
        XrNegotiateApiLayerRequest request{};
        request.structType = XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST; request.structVersion = XR_API_LAYER_INFO_STRUCT_VERSION;
        request.structSize = sizeof(request);
        require(negotiate && negotiate(&loader, "XR_APILAYER_CHEEKY_foveated_dlss", &request) == XR_SUCCESS, "Negotiation failed");
        get = request.getInstanceProcAddr;
        XrApiLayerNextInfo next{}; next.nextGetInstanceProcAddr = Runtime::get; next.nextCreateApiLayerInstance = Runtime::create;
        XrApiLayerCreateInfo layer{}; layer.nextInfo = &next;
        XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
        info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        info.enabledExtensionNames = extensions.data();
        require(request.createApiLayerInstance(&info, &layer, &instance) == expected, "Unexpected create instance result");
        if (XR_FAILED(expected)) return;
        XrSessionCreateInfo create{XR_TYPE_SESSION_CREATE_INFO}; create.systemId = 1;
        struct MockD3D11Binding { XrStructureType type; const void* next; void* device; };
        const MockD3D11Binding graphics{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR, nullptr, nullptr};
        if (menu) create.next = &graphics;
        require(fn<PFN_xrCreateSession>("xrCreateSession")(instance, &create, &session) == XR_SUCCESS, "Create session failed");
        begin(); focus(true);
    }
    void begin() {
        XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO}; begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        require(fn<PFN_xrBeginSession>("xrBeginSession")(session, &begin) == XR_SUCCESS, "Begin failed");
    }
    void focus(bool value) {
        Runtime::focused = value; XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        require(fn<PFN_xrPollEvent>("xrPollEvent")(instance, &event) == XR_SUCCESS, "Focus event failed");
    }
    CheekyGazeSnapshotV1 locate(XrTime time) {
        XrViewLocateInfo info{XR_TYPE_VIEW_LOCATE_INFO}; info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        info.displayTime = time; info.space = handle<XrSpace>(50);
        XrViewState state{XR_TYPE_VIEW_STATE}; XrView views[2]{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}; uint32_t count{};
        require(fn<PFN_xrLocateViews>("xrLocateViews")(session, &info, &state, 2, &count, views) == XR_SUCCESS, "Locate views failed");
        return snapshot();
    }
    CheekyGazeSnapshotV1 snapshot() {
        CheekyGazeSnapshotV1 result{};
        const auto read = reinterpret_cast<CheekyOpenXRGetGazeSnapshotFn>(GetProcAddress(module, "CheekyOpenXR_GetGazeSnapshot"));
        require(read(CHEEKY_GAZE_ABI_VERSION, &result, sizeof(result)) != 0, "Snapshot failed"); return result;
    }
    CheekyGazeInputDiagnosticsV1 diagnostics() {
        CheekyGazeInputDiagnosticsV1 result{};
        const auto read = reinterpret_cast<CheekyOpenXRGetGazeInputDiagnosticsFn>(GetProcAddress(module, "CheekyOpenXR_GetGazeInputDiagnostics"));
        require(read && read(CHEEKY_GAZE_INPUT_DIAGNOSTICS_VERSION, &result, sizeof(result)), "Input diagnostics failed");
        return result;
    }
    XrActionSet create_host_set() {
        XrActionSet set{}; XrActionSetCreateInfo info{XR_TYPE_ACTION_SET_CREATE_INFO};
        require(fn<PFN_xrCreateActionSet>("xrCreateActionSet")(instance, &info, &set) == XR_SUCCESS, "Host set failed"); return set;
    }
    XrResult attach(XrActionSet set) {
        XrSessionActionSetsAttachInfo info{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO}; info.countActionSets = 1; info.actionSets = &set;
        return fn<PFN_xrAttachSessionActionSets>("xrAttachSessionActionSets")(session, &info);
    }
    void sync(XrActionSet set) {
        XrActiveActionSet active{set, 55}; XrActionsSyncInfo info{XR_TYPE_ACTIONS_SYNC_INFO}; info.countActiveActionSets = 1; info.activeActionSets = &active;
        require(fn<PFN_xrSyncActions>("xrSyncActions")(session, &info) == XR_SUCCESS, "Host sync failed");
    }
    ~Layer() {
        if (session) fn<PFN_xrDestroySession>("xrDestroySession")(session);
        if (instance) fn<PFN_xrDestroyInstance>("xrDestroyInstance")(instance);
        FreeLibrary(module);
    }
};
bool valid(const CheekyGazeSnapshotV1& s) { return (s.status_flags & CHEEKY_GAZE_STATUS_GAZE_VALID) != 0; }
std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void run_input_compatibility_fixture(const wchar_t* name, bool expected_il2, unsigned index) {
    // Exercise process-name detection through the real DLL, without a production
    // override. Each process owns its fixture directory, including parallel runs.
    wchar_t executable[32768]{};
    const auto length = GetModuleFileNameW(nullptr, executable, 32768);
    require(length && length < 32768, "Cannot resolve input test executable");
    const auto bin = std::filesystem::path(executable).parent_path();
    const auto directory = bin / "test-fixtures" / "il2-input" /
        std::to_wstring(GetCurrentProcessId()) / std::to_wstring(index);
    std::filesystem::create_directories(directory);
    const auto child = directory / name;
    std::filesystem::copy_file(executable, child, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(bin / "CheekyOpenXRLayer.dll", directory / "CheekyOpenXRLayer.dll",
        std::filesystem::copy_options::overwrite_existing);
    std::wstring command = L"\"" + child.wstring() + L"\" " +
        (expected_il2 ? L"--openxr-input-il2" : L"--openxr-input-unrecognized");
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    require(CreateProcessW(child.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
        nullptr, directory.c_str(), &startup, &process) != FALSE, "Cannot launch input compatibility fixture");
    const auto wait = WaitForSingleObject(process.hProcess, 60000);
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
    }
    DWORD code = 1;
    const bool exited = GetExitCodeProcess(process.hProcess, &code) != FALSE;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    require(wait == WAIT_OBJECT_0 && exited && code == 0, "Input compatibility fixture failed or timed out");
}
}

int run_openxr_input_compatibility_tests(bool expected_il2) {
    try {
        Runtime::reset();
        {
            const auto log_start = read_file(cheeky::openxr_startup_log_path()).size();
            Layer layer;
            require(!layer.diagnostics().realvr_detected, "IL-2 compatibility must not identify RealVR");
            const auto log = read_file(cheeky::openxr_startup_log_path()).substr(log_start);
            require((log.find("input_compatibility host=IL2Series.exe policy=guarded_independent_input") != std::string::npos)
                == expected_il2, "Startup log must identify only the admitted IL-2 process");
            if (!expected_il2) {
                require(!valid(layer.locate(1)) && !Runtime::attaches && !Runtime::syncs,
                    "Similar executable names must not receive independent input");
                std::cout << "PASS: unrecognized executable retains host-owned input\n";
                return 0;
            }
            require(!valid(layer.locate(0)) && !Runtime::attaches && !Runtime::syncs,
                "IL-2 fallback needs a positive display time");
            layer.focus(false);
            require(!valid(layer.locate(1)) && !Runtime::attaches, "IL-2 must not attach before focus");
            layer.focus(true);
            require(valid(layer.locate(2)), "No-input IL-2 must acquire gaze");
            require(Runtime::bindings == 1 && Runtime::attaches == 1 && Runtime::syncs == 1,
                "IL-2 must bind, attach and sync once on first focused locate");
            layer.locate(2);
            require(Runtime::syncs == 1, "IL-2 must not sync twice at the same display time");
            XrFrameBeginInfo frame{XR_TYPE_FRAME_BEGIN_INFO};
            require(layer.fn<PFN_xrBeginFrame>("xrBeginFrame")(layer.session, &frame) == XR_SUCCESS && Runtime::syncs == 1,
                "BeginFrame must not inject an extra gaze-only sync");
            layer.locate(3);
            const auto d = layer.diagnostics();
            require(!d.realvr_detected && d.action_attached && d.fallback_attach_calls == 1 &&
                d.fallback_sync_calls == 2 && !d.host_attach_calls && !d.host_sync_calls,
                "IL-2 diagnostics must report fallback input without inventing RealVR");
            layer.focus(false);
            require(!valid(layer.locate(4)) && Runtime::syncs == 2, "Unfocused IL-2 must invalidate gaze and stop sync");
            layer.focus(true);
            require(valid(layer.locate(5)) && Runtime::attaches == 1, "IL-2 must recover focus without reattaching");
            Runtime::active = false;
            require(!valid(layer.locate(6)), "Inactive IL-2 gaze action must remain invalid");
            Runtime::active = true;
            Runtime::sync_result = XR_SESSION_NOT_FOCUSED;
            require(!valid(layer.locate(7)), "NOT_FOCUSED must invalidate IL-2 gaze");
            Runtime::sync_result = XR_ERROR_RUNTIME_FAILURE;
            require(!valid(layer.locate(8)), "Failed sync must invalidate IL-2 gaze");
            Runtime::sync_result = XR_SUCCESS; Runtime::locate_result = XR_ERROR_RUNTIME_FAILURE;
            require(!valid(layer.locate(9)), "Failed locate must reject stale IL-2 gaze flags");
            Runtime::locate_result = XR_SUCCESS;
            require(valid(layer.locate(10)), "IL-2 gaze must recover after transient failures");
            require(layer.fn<PFN_xrEndSession>("xrEndSession")(layer.session) == XR_SUCCESS, "IL-2 session end failed");
            const auto syncs = Runtime::syncs;
            require(!valid(layer.locate(11)) && Runtime::syncs == syncs, "Stopped IL-2 session must not sync");
            layer.begin();
            require(valid(layer.locate(12)) && Runtime::attaches == 1, "IL-2 session restart must reuse attachment");
            const auto host = layer.create_host_set();
            require(layer.attach(host) == XR_ERROR_ACTIONSETS_ALREADY_ATTACHED,
                "IL-2 late host attachment must forward the real runtime error");
        }
        Runtime::reset();
        {
            Layer layer; const auto host = layer.create_host_set();
            require(!valid(layer.locate(1)) && !Runtime::attaches && !Runtime::syncs,
                "IL-2 host-created sets must reserve attachment");
            require(layer.attach(host) == XR_SUCCESS && Runtime::attached_sets.size() == 2 && Runtime::attached_sets[0] == host,
                "IL-2 host attachment must preserve its sets and append gaze");
            require(valid(layer.locate(2)), "IL-2 attached host may use fallback until it synchronizes");
            layer.sync(host);
            require(Runtime::active_sets.size() == 2 && Runtime::active_sets[0].actionSet == host &&
                Runtime::active_sets[0].subactionPath == 55, "IL-2 host sync must preserve active sets and subaction paths");
            const auto syncs = Runtime::syncs;
            layer.locate(3); layer.locate(4);
            require(Runtime::syncs == syncs && layer.diagnostics().host_sync_calls == 1,
                "IL-2 fallback must stop permanently after host sync");
        }
        Runtime::reset();
        {
            Layer layer;
            Runtime::attach_result = XR_ERROR_RUNTIME_FAILURE;
            require(layer.attach(handle<XrActionSet>(99)) == XR_ERROR_RUNTIME_FAILURE, "Host attach failure must propagate");
            Runtime::attach_result = XR_SUCCESS;
            layer.locate(1); layer.locate(2);
            require(Runtime::attaches == 1 && !Runtime::syncs && !layer.diagnostics().fallback_attach_calls,
                "Even a failed host attach reserves IL-2 attachment ownership");
        }
        Runtime::reset();
        {
            Layer layer;
            XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
            require(layer.fn<PFN_xrSyncActions>("xrSyncActions")(layer.session, &sync) == XR_ERROR_ACTIONSET_NOT_ATTACHED,
                "Host sync error must propagate");
            layer.locate(1); layer.locate(2);
            require(!Runtime::attaches && Runtime::syncs == 1 && !layer.diagnostics().fallback_sync_calls,
                "Even a failed host sync disables IL-2 fallback");
        }
        for (unsigned failure = 0; failure < 5; ++failure) {
            Runtime::reset();
            if (failure == 0) Runtime::extension = false;
            if (failure == 1) Runtime::supported = false;
            if (failure == 2) Runtime::binding_result = XR_ERROR_PATH_UNSUPPORTED;
            if (failure == 3) Runtime::attach_result = XR_ERROR_RUNTIME_FAILURE;
            if (failure == 4) Runtime::space_result = XR_ERROR_RUNTIME_FAILURE;
            Layer layer;
            require(!valid(layer.locate(1)) && !valid(layer.locate(2)) && !Runtime::syncs,
                "Unsupported or failed IL-2 setup must never sync or publish gaze");
            require(Runtime::attaches <= 1 && Runtime::bindings <= 1, "IL-2 setup failures must not retry every frame");
        }
        for (bool gaze : {true, false}) {
            Runtime::reset(); Runtime::extension = gaze;
            Layer layer(true);
            layer.locate(1); layer.locate(2);
            const auto count = gaze ? 2U : 1U;
            require(Runtime::attached_sets.size() == count && Runtime::active_sets.size() == count &&
                Runtime::attaches == 1 && Runtime::syncs == 2, "IL-2 must preserve gaze/menu action-set composition");
        }
        std::cout << "PASS: IL-2 process detection, guarded input lifecycle, ownership and failure diagnostics\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "OpenXR IL-2 input: " << e.what() << '\n'; return 1;
    }
}

int run_openxr_input_tests() {
    HMODULE realvr{};
    try {
        run_input_compatibility_fixture(L"IL2Series.exe", true, 0);
        run_input_compatibility_fixture(L"iL2sErIeS.ExE", true, 1);
        run_input_compatibility_fixture(L"IL2Series-test.exe", false, 2);
        // Exercise the actual DLL against a lower layer unable to enumerate
        // extensions before creation, including runtimes without gaze support.
        for (unsigned failure = 0; failure <= 3; ++failure) {
            for (bool gaze : {false, true}) {
              for (bool cylinder : {false, true}) {
                Runtime::reset(); Runtime::probe_failure = failure; Runtime::extension = gaze;
                Runtime::cylinder = cylinder;
                const auto log_start = read_file(cheeky::openxr_startup_log_path()).size();
                Layer layer(false, {"XR_TEST_host_extension"});
                const auto attempts = !failure ? 1U : gaze ? (cylinder ? 1U : 2U) : (cylinder ? 3U : 4U);
                require(Runtime::create_requests.size() == attempts, "Incorrect extension retry count");
                for (const auto& names : Runtime::create_requests) {
                    require(names.front() == "XR_TEST_host_extension", "Retry lost an application extension");
                }
                const auto& final_names = Runtime::create_requests.back();
                require((std::find(final_names.begin(), final_names.end(), XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME)
                    != final_names.end()) == cylinder, "Successful instance must retain supported cylinders");
                if (failure) {
                    for (unsigned i = 0; i < attempts; ++i) {
                        const auto& names = Runtime::create_requests[i];
                        const bool requested_gaze = std::find(names.begin(), names.end(), XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME) != names.end();
                        const bool requested_cylinder = std::find(names.begin(), names.end(), XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME) != names.end();
                        require(requested_gaze == (i < 2) && requested_cylinder == (i % 2 == 0),
                            "Retries must prefer gaze, then recover cylinder-only support");
                    }
                }
                require(Runtime::injected == gaze, "Successful instance has incorrect gaze extension state");
                const auto host = layer.create_host_set();
                require(layer.attach(host) == XR_SUCCESS, "Probe regression host attachment failed");
                layer.sync(host);
                const auto snapshot = layer.locate(1);
                require(valid(snapshot) == gaze, "Unanswered probe must restore functional gaze when supported");
                require(bool(snapshot.status_flags & CHEEKY_GAZE_STATUS_EXTENSION_ENABLED) == gaze,
                    "Published extension state must describe the successful attempt");
                const auto log = read_file(cheeky::openxr_startup_log_path()).substr(log_start);
                const auto availability = failure ? "unknown" : gaze ? "present" : "absent";
                require(log.find(std::string("availability=") + availability) != std::string::npos,
                    "Persistent log missing probe outcome");
                require(log.find("instance_create end result=0 gaze_enabled=" + std::to_string(gaze) +
                    " cylinder_enabled=" + std::to_string(cylinder)) != std::string::npos,
                    "Persistent log missing final extension states");
                require(log.find("attempt=" + std::to_string(attempts) + " result=0") != std::string::npos &&
                    log.find("attempt=" + std::to_string(attempts + 1)) == std::string::npos,
                    "Persistent log missing or inventing retry history");
                if (failure) {
                    const auto stage = failure == 1 ? "lookup" : failure == 2 ? "count" : "list";
                    const auto error = failure == 1 ? XR_ERROR_FUNCTION_UNSUPPORTED : XR_ERROR_RUNTIME_FAILURE;
                    require(log.find(std::string("stage=") + stage + " result=" + std::to_string(error)) != std::string::npos,
                        "Persistent log missing failed probe stage/result");
                }
              }
            }
        }
        Runtime::reset(); Runtime::probe_failure = 1; Runtime::supported = false;
        {
            Layer layer;
            require(!valid(layer.locate(1)), "Speculative extension must not invent system eye tracking support");
        }
        for (bool gaze : {false, true}) {
            Runtime::reset(); Runtime::probe_failure = 1; Runtime::extension = gaze;
            Layer layer(false, {XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME},
                gaze ? XR_SUCCESS : XR_ERROR_EXTENSION_NOT_PRESENT);
            require(Runtime::create_requests.size() == 2 && Runtime::create_requests.back().size() == 1,
                "Application gaze extension must not be duplicated or removed");
            for (const auto& names : Runtime::create_requests) {
                require(std::count(names.begin(), names.end(), XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME) == 1,
                    "Every retry must preserve application-requested gaze");
            }
        }
        for (bool cylinder : {false, true}) {
            Runtime::reset(); Runtime::probe_failure = 1; Runtime::extension = false; Runtime::cylinder = cylinder;
            Layer layer(false, {XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME},
                cylinder ? XR_SUCCESS : XR_ERROR_EXTENSION_NOT_PRESENT);
            require(Runtime::create_requests.size() == 2, "Unknown gaze alone requires at most one retry");
            for (const auto& names : Runtime::create_requests) {
                require(std::count(names.begin(), names.end(), XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME) == 1,
                    "Every retry must preserve application-requested cylinders");
            }
        }
        for (const auto error : {XR_ERROR_RUNTIME_FAILURE, XR_ERROR_EXTENSION_NOT_PRESENT}) {
            Runtime::reset(); Runtime::create_result = error;
            Runtime::probe_failure = error == XR_ERROR_RUNTIME_FAILURE ? 1U : 0U;
            Layer layer(false, {}, error);
            require(Runtime::create_requests.size() == 1, "Only an unknown extension rejection may retry");
        }
        Runtime::reset(); Runtime::probe_failure = 1; Runtime::create_result = XR_ERROR_EXTENSION_NOT_PRESENT;
        {
            const auto log_start = read_file(cheeky::openxr_startup_log_path()).size();
            Layer layer(false, {"XR_TEST_host_extension"}, XR_ERROR_EXTENSION_NOT_PRESENT);
            require(Runtime::create_requests.size() == 4 && Runtime::create_requests.back().size() == 1,
                "Failed retry must propagate without dropping host extensions or retrying indefinitely");
            const auto log = read_file(cheeky::openxr_startup_log_path()).substr(log_start);
            require(log.find("instance_create end result=" + std::to_string(XR_ERROR_EXTENSION_NOT_PRESENT) +
                " gaze_enabled=0 cylinder_enabled=0") != std::string::npos,
                "Failed instance creation must remain available in the persistent log");
        }
        Runtime::reset(); Runtime::cylinder = true;
        {
            Layer layer;
            require(Runtime::create_requests[0].size() == 2, "Confirmed cylinder support must remain enabled");
        }
        for (auto host : {CheekyRuntimeHost::uevr, CheekyRuntimeHost::standalone, CheekyRuntimeHost::optiscaler}) {
            const auto directory = std::filesystem::current_path() / "build" / "issue42-support-tests";
            const auto zip = cheeky::foveated_dlss::create_runtime_support_bundle(directory, "{}", "", "test", host);
            const auto contents = read_file(zip);
            require(contents.find("CheekyOpenXR-startup.log") != std::string::npos &&
                contents.find("availability=unknown") != std::string::npos &&
                contents.find("attempt=2 result=") != std::string::npos,
                "Support ZIP must contain the persistent OpenXR startup history");
        }
        // Controller rays must resolve to the same pixels on native cylinders
        // and on the quad-strip fallback, including non-divisible texture widths.
        for (bool cylinder : {false, true}) {
            for (unsigned i = 0; i < 12; ++i) {
                constexpr float width = 6.F, height = 2.F;
                constexpr unsigned pixels = 3443;
                const auto strip = cheeky::xr_menu::strip(width, pixels, i, 12);
                float expected = (strip.left + .3F * (strip.right - strip.left)) / pixels;
                XrVector3f point;
                if (cylinder) {
                    const float a = (expected - .5F) * width / cheeky::xr_menu::radius;
                    point = {3.F * std::sin(a), .2F, 3.F * (1.F - std::cos(a))};
                } else {
                    point = {strip.center.x - .2F * strip.width * std::cos(strip.angle),
                        .2F, strip.center.z - .2F * strip.width * std::sin(strip.angle)};
                }
                XrVector3f direction{point.x, point.y, point.z - 1.5F};
                const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
                direction = {direction.x / length, direction.y / length, direction.z / length};
                float u{}, v{};
                require(cheeky::xr_menu::hit({0,0,1.5F}, direction, width, height, pixels, 12, cylinder, u, v) &&
                    std::abs(u - expected) < 1e-4F && std::abs(v - .4F) < 1e-4F,
                    "Curved menu controller hit must match displayed pixel");
                require(!cheeky::xr_menu::hit({0,0,1.5F}, {0,0,1}, width, height, pixels, 12, cylinder, u, v),
                    "Controller pointing away must not hit menu");
            }
        }
        Runtime::reset();
        {
            Layer layer;
            const auto host = layer.create_host_set();
            require(layer.attach(host) == XR_SUCCESS, "Simulation host attachment");
            const auto simulate = reinterpret_cast<void(__cdecl*)(std::uint32_t)>(GetProcAddress(layer.module, "CheekyOpenXR_SetSimulatedGaze"));
            require(simulate != nullptr, "Simulation control export");
            simulate(1);
            Runtime::active = false;
            layer.sync(host);
            const auto before = layer.locate(1000000000);
            require(valid(before), "Simulation must work without a physical gaze action");
            for (unsigned i = 0; i < 8; ++i) {
                layer.sync(host);
                const auto after = layer.snapshot();
                require(valid(after) && after.sample_time == before.sample_time &&
                    after.views[0].center_u == before.views[0].center_u,
                    "Controller sync must preserve the simulated gaze sample");
            }
            simulate(0);
            layer.sync(host);
            require(!valid(layer.snapshot()), "Disabling simulation restores physical gaze validity");
        }
        Runtime::reset();
        { Layer layer; require(!valid(layer.locate(1)) && !Runtime::attaches && !Runtime::syncs,
            "Ordinary apps must not independently attach/sync"); }
        Runtime::reset();
        {
            Layer layer; const auto host = layer.create_host_set();
            require(layer.attach(host) == XR_SUCCESS, "Ordinary host attach failed");
            layer.sync(host);
            require(valid(layer.locate(1)) && Runtime::syncs == 1 && Runtime::attaches == 1,
                "Ordinary host must retain merged input without fallback calls");
            require(!layer.diagnostics().realvr_detected && !layer.diagnostics().fallback_sync_calls,
                "Ordinary host must not enable RealVR fallback");
        }
        // Load the existing version-resource fixture under a supported proxy name.
        wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, 32768);
        const auto bin = std::filesystem::path(executable).parent_path();
        const auto dir = bin / "test-fixtures" / "gaze-input";
        std::filesystem::create_directories(dir);
        std::filesystem::copy_file(bin / "test-fixtures" / "CheekyFakeRealVR.dll", dir / "RealVR64.dll", std::filesystem::copy_options::overwrite_existing);
        realvr = LoadLibraryW((dir / "RealVR64.dll").c_str()); require(realvr != nullptr, "RealVR fixture missing");
        for (bool gaze : {true, false}) {
            Runtime::reset();
            Runtime::extension = gaze;
            Layer layer(true);
            layer.locate(1);
            const auto expected = gaze ? 2U : 1U;
            require(Runtime::attached_sets.size() == expected && Runtime::active_sets.size() == expected,
                "RealVR must attach and synchronize menu controllers with or without eye tracking");
            layer.locate(2);
            require(Runtime::attaches == 1 && Runtime::syncs == 2,
                "RealVR menu actions must attach once and update each frame");
        }
        Runtime::reset();
        {
            Layer layer;
            require(Runtime::injected, "Layer must enable gaze extension when app did not request it");
            require(valid(layer.locate(1)), "No-input RealVR must acquire live gaze");
            require(Runtime::bindings == 1 && Runtime::attaches == 1 && Runtime::syncs == 1, "Initial binding/attach/sync counts");
            layer.locate(1); require(Runtime::syncs == 1, "Repeated LocateViews must not sync twice at the same display time");
            layer.locate(2); require(Runtime::attaches == 1 && Runtime::syncs == 2, "Attach once, sync each frame");
            const auto d = layer.diagnostics();
            require(d.realvr_detected && d.action_attached && d.fallback_attach_calls == 1 && d.fallback_sync_calls == 2 && !d.host_sync_calls && d.pose_result == XR_SUCCESS, "Fallback diagnostics missing");
            const auto live = cheeky::foveated_dlss::gaze_diagnostics();
            require(live.input.session_generation == d.session_generation && live.input.fallback_sync_calls == 2,
                "Support diagnostics must be available without a DLSS evaluation");
            const auto json = cheeky::foveated_dlss::gaze_input_diagnostics_json(d);
            require(json.find("\"fallback_sync_calls\":2") != std::string::npos, "Support JSON missing input state");
            layer.focus(false); require(!valid(layer.locate(3)) && Runtime::syncs == 2, "Unfocused session must clear gaze and stop polling");
            layer.focus(true); require(valid(layer.locate(4)) && Runtime::attaches == 1, "Focus recovery must reuse attachment");
            Runtime::active = false; require(!valid(layer.locate(5)), "Inactive provider must not become synthetic gaze"); Runtime::active = true;
            Runtime::sync_result = XR_SESSION_NOT_FOCUSED; require(!valid(layer.locate(6)), "Positive NOT_FOCUSED result must not pass gaze");
            Runtime::sync_result = XR_ERROR_RUNTIME_FAILURE; require(!valid(layer.locate(7)), "Failed sync must clear old gaze");
            Runtime::sync_result = XR_SUCCESS; Runtime::locate_result = XR_ERROR_RUNTIME_FAILURE;
            require(!valid(layer.locate(8)), "Failed space locate must reject stale flags"); Runtime::locate_result = XR_SUCCESS;
            require(valid(layer.locate(9)), "Transient failures must recover");
            layer.fn<PFN_xrEndSession>("xrEndSession")(layer.session);
            const auto calls = Runtime::syncs; require(!valid(layer.locate(10)) && Runtime::syncs == calls, "Stopped session must not poll or publish gaze");
            layer.begin(); require(valid(layer.locate(11)) && Runtime::attaches == 1, "Session restart must reuse action attachment");
            const auto host = layer.create_host_set();
            require(layer.attach(host) == XR_ERROR_ACTIONSETS_ALREADY_ATTACHED, "Late attachment error must be forwarded, never fake success");
        }
        Runtime::reset();
        {
            Layer layer; const auto host = layer.create_host_set();
            layer.locate(1); require(Runtime::attaches == 0, "Host-created action sets must reserve attachment for host");
            require(layer.attach(host) == XR_SUCCESS && Runtime::attached_sets.size() == 2 && Runtime::attached_sets[0] == host,
                "Host attachment must preserve sets and append gaze");
            require(valid(layer.locate(2)), "Attached host without sync should receive fallback gaze");
            layer.sync(host);
            require(Runtime::active_sets.size() == 2 && Runtime::active_sets[0].actionSet == host && Runtime::active_sets[0].subactionPath == 55,
                "Host active sets and subaction paths must be preserved");
            const auto calls = Runtime::syncs; layer.locate(3); layer.locate(4);
            require(Runtime::syncs == calls, "After host sync, independent sync must stop to preserve controllers");
            require(layer.diagnostics().host_attach_calls == 1 && layer.diagnostics().host_sync_calls == 1, "Host input diagnostics missing");
        }
        for (unsigned failure = 0; failure < 5; ++failure) {
            Runtime::reset();
            if (failure == 0) Runtime::extension = false;
            if (failure == 1) Runtime::supported = false;
            if (failure == 2) Runtime::binding_result = XR_ERROR_PATH_UNSUPPORTED;
            if (failure == 3) Runtime::attach_result = XR_ERROR_RUNTIME_FAILURE;
            if (failure == 4) Runtime::space_result = XR_ERROR_RUNTIME_FAILURE;
            Layer layer; require(!valid(layer.locate(1)) && !valid(layer.locate(2)) && !Runtime::syncs,
                "Unsupported/failed setup must never synchronize or publish gaze");
            require(Runtime::attaches <= 1 && Runtime::bindings <= 1, "Setup failures must not retry every frame");
            const auto d = layer.diagnostics();
            if (failure == 2) require(d.binding_result == XR_ERROR_PATH_UNSUPPORTED, "Binding failure diagnostic");
            if (failure == 3) require(d.attach_result == XR_ERROR_RUNTIME_FAILURE, "Attach failure diagnostic");
            if (failure == 4) require(d.space_result == XR_ERROR_RUNTIME_FAILURE, "Space failure diagnostic");
        }
        FreeLibrary(realvr);
        std::cout << "PASS: OpenXR independent RealVR gaze, host actions, lifecycle and failure diagnostics\n";
        return 0;
    } catch (const std::exception& e) {
        if (realvr) FreeLibrary(realvr);
        std::cerr << "OpenXR input: " << e.what() << '\n'; return 1;
    }
}
