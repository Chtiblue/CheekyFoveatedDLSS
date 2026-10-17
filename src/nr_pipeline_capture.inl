// Explicit, bounded diagnostic only. Included by dlss_nr.cpp.
#include "nr_pipeline_capture.hpp"
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <memory>
#include <vector>
#include <stdexcept>

namespace cheeky::foveated_dlss {
namespace {
using CaptureResource = Microsoft::WRL::ComPtr<ID3D12Resource>;
struct NrCapturePlane {
    CaptureResource source, readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    D3D12_BOX box{};
    D3D12_RESOURCE_STATES state{};
    std::string name;
    UINT64 bytes{};
};
struct NrCaptureJob {
    NrLifetime lifetime;
    std::vector<NrCapturePlane> planes;
    std::filesystem::path directory;
    std::string metadata;
    std::array<std::uint32_t,48> constants{};
    Microsoft::WRL::ComPtr<ID3D12Device> device;
};
struct NrCaptureController {
    std::mutex mutex;
    std::filesystem::path root;
    std::string status{"Idle"};
    ULONGLONG ready{}, deadline{};
    std::uint64_t generation{}, views[2]{};
    unsigned accepted{}, saved{}, writing{};
    bool armed{};
    // Pending jobs are never freed on timeout: recordings may still execute.
    std::vector<std::shared_ptr<NrCaptureJob>> pending;
};
NrCaptureController& nr_capture_controller() { static auto* c = new NrCaptureController; return *c; }
constexpr std::uint64_t capture_pass2_bit=std::uint64_t{1}<<63, capture_pass3_bit=std::uint64_t{1}<<61;
std::uint64_t capture_base_view(std::uint64_t view) {return view & ~(capture_pass2_bit|capture_pass3_bit);}
std::uint64_t begin_nr_pixel_chain(std::uint64_t view) {
    auto& c=nr_capture_controller(); std::lock_guard lock(c.mutex);
    const auto now=GetTickCount64();
    if (!c.armed || now<c.ready || now>c.deadline || c.accepted>=2) return 0;
    for(unsigned i=0;i<c.accepted;++i) if(c.views[i]==view) return 0;
    c.views[c.accepted++]=view;
    return c.generation;
}
void save_nr_pixel_job(std::shared_ptr<NrCaptureJob> job) noexcept {
    bool ok=true;
    try {
        std::filesystem::create_directory(job->directory);
        for(auto& p:job->planes) {
            void* data{}; D3D12_RANGE range{0,SIZE_T(p.bytes)};
            if(FAILED(p.readback->Map(0,&range,&data))) throw std::runtime_error("Map failed");
            std::ofstream out(job->directory/(p.name+".bin"),std::ios::binary);
            out.write(static_cast<const char*>(data),static_cast<std::streamsize>(p.bytes));
            out.close(); D3D12_RANGE none{}; p.readback->Unmap(0,&none);
            if(!out) throw std::runtime_error("Image write failed");
        }
        std::ofstream meta(job->directory/"capture.json"); meta<<job->metadata; meta.close();
        if(!meta) throw std::runtime_error("Metadata write failed");
        std::ofstream constants(job->directory/"codec-constants.bin",std::ios::binary);
        constants.write(reinterpret_cast<const char*>(job->constants.data()),sizeof(job->constants));constants.close();
        if(!constants) throw std::runtime_error("Constants write failed");
    } catch(...) {ok=false;}
    auto& c=nr_capture_controller(); std::lock_guard lock(c.mutex);
    --c.writing; if(ok) ++c.saved;
    c.status=ok ? "Saved "+std::to_string(c.saved)+" pass captures: "+c.root.string() : "Capture file write failed";
}
void collect_nr_pixel_jobs() noexcept {
    auto& c=nr_capture_controller(); std::lock_guard lock(c.mutex);
    for(auto it=c.pending.begin();it!=c.pending.end();) {
        auto job=*it; job->lifetime.collect();
        if(!job->lifetime.empty()) {++it;continue;}
        // A discarded command list has no completion evidence. Device removal
        // also must not turn UINT64_MAX fence completion into a valid image.
        if(!job->lifetime.fences_released() || FAILED(job->device->GetDeviceRemovedReason())) {
            c.status="Capture discarded: no successful GPU completion"; it=c.pending.erase(it); continue;
        }
        try {++c.writing; std::thread(save_nr_pixel_job,job).detach();}
        catch(...) {--c.writing;c.status="Could not start capture writer"; ++it;continue;}
        it=c.pending.erase(it);
    }
    if(c.armed && GetTickCount64()>c.deadline) {
        c.armed=false;
        if(c.pending.empty() && !c.writing) c.status="Capture finished: "+std::to_string(c.saved)+" pass files in "+c.root.string();
        else c.status="Capture waiting for GPU recording retirement; retained safely";
    }
}
void record_nr_pixel_pass(const DlssNrFrame& frame,const Settings& settings,
    const NrRegion& region,ID3D12Resource* original,ID3D12Resource* proxy,
    ID3D12Resource* neural,ID3D12Resource* exposure,const std::array<std::uint32_t,48>& constants,bool reset,const char* exposure_reason) noexcept {
    if(!frame.diagnostic_capture) return;
    auto& c=nr_capture_controller(); std::lock_guard lock(c.mutex);
    if(!c.armed || frame.diagnostic_capture!=c.generation || c.pending.size()+c.saved+c.writing>=6) return;
    // Pass identity is structural, not inferred from whichever eye rendered first.
    const auto pass=(frame.view_id & capture_pass3_bit) ? 3U : (frame.view_id & capture_pass2_bit) ? 2U : 1U;
    try {
        auto job=std::make_shared<NrCaptureJob>();
        job->constants=constants;
        job->directory=c.root/("view-"+std::to_string(capture_base_view(frame.view_id))+"-pass-"+std::to_string(pass));
        if(FAILED(frame.command_list->GetDevice(IID_PPV_ARGS(&job->device)))) return;
        UINT64 total{};
        const auto add=[&](const char* name,ID3D12Resource* resource,D3D12_RESOURCE_STATES state,
            UINT x,UINT y,UINT width,UINT height) {
            const auto desc=resource->GetDesc();
            if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.SampleDesc.Count!=1 ||
                desc.DepthOrArraySize!=1 || !width || !height || x+UINT64(width)>desc.Width || y+UINT64(height)>desc.Height)
                throw std::runtime_error("Unsupported capture texture");
            NrCapturePlane p;p.source=resource;p.name=name;p.state=state;p.box={x,y,0,x+width,y+height,1};
            auto cropped=desc;cropped.Width=width;cropped.Height=height;cropped.MipLevels=1;
            job->device->GetCopyableFootprints(&cropped,0,1,0,&p.footprint,nullptr,nullptr,&p.bytes);
            if(p.bytes==UINT64_MAX || p.bytes>32*1024*1024 || total+p.bytes>64*1024*1024)
                throw std::runtime_error("Capture size limit exceeded");
            total+=p.bytes;
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=p.bytes;
            buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if(FAILED(job->device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,IID_PPV_ARGS(&p.readback)))) throw std::runtime_error("Readback allocation failed");
            job->planes.push_back(std::move(p));
        };
        constexpr auto read=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        add("original",original,read,0,0,region.width,region.height);
        const auto pd=proxy->GetDesc();const auto nd=neural->GetDesc();
        add("proxy",proxy,read,0,0,UINT(pd.Width),pd.Height);
        add("neural",neural,read,0,0,UINT(nd.Width),nd.Height);
        add("restored",frame.color,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,region.base_x,region.base_y,region.width,region.height);
        if(exposure) add("exposure",exposure,frame.exposure_state,0,0,1,1);
        std::ostringstream meta;meta.precision(9);
        meta<<"{\"capture\":"<<c.generation<<",\"base_view\":\""<<capture_base_view(frame.view_id)<<"\",\"pass\":"<<pass
            <<",\"boundary\":\"one NR invocation, before next pass; not final SR or headset image\",\"processing_order\":"<<unsigned(settings.nr_processing_order)
            <<",\"route\":"<<unsigned(frame.route)<<",\"create_flags\":"<<frame.create_flags
            <<",\"reset\":"<<(reset ? "true":"false")<<",\"jitter_uv\":["<<frame.jitter_uv_x<<','<<frame.jitter_uv_y<<']'
            <<",\"color_pipeline\":"<<settings.nr_color_pipeline<<",\"signal\":"<<settings.nr_signal_encoding
            <<",\"scaling_domain\":"<<settings.nr_scaling_domain<<",\"exposure_mode\":"<<settings.nr_exposure_mode
            <<",\"exposure_multiplier\":"<<settings.nr_exposure_multiplier<<",\"exposure_supplied\":"<<(frame.exposure_texture ? "true":"false")
            <<",\"exposure_accepted\":"<<(exposure ? "true":"false")<<",\"reconstruction\":"<<settings.nr_reconstruction
            <<",\"exposure_query_result\":"<<frame.exposure_query_result<<",\"exposure_tag_flags\":"<<frame.exposure_tag_flags
            <<",\"exposure_state\":"<<unsigned(frame.exposure_state)
            <<",\"exposure_reason\":\""<<exposure_reason<<"\""
            <<",\"paper_white_scale\":"<<settings.nr_paper_white_scale<<",\"transfer_strength\":"<<settings.nr_hdr_transfer_strength
            <<",\"color_strength\":"<<settings.nr_color_strength<<",\"intensity\":"<<settings.nr_intensity
            <<",\"preset\":"<<settings.nr_preset<<",\"style\":"<<settings.nr_style
            <<",\"downsample_filter\":"<<settings.nr_downsample_filter<<",\"upsample_filter\":"<<settings.nr_upsample_filter
            <<",\"region\":["<<region.base_x<<','<<region.base_y<<','<<region.width<<','<<region.height<<"],\"planes\":[";
        bool first=true;for(auto& p:job->planes){if(!first)meta<<',';first=false;
            meta<<"{\"name\":\""<<p.name<<"\",\"format\":"<<unsigned(p.footprint.Footprint.Format)
                <<",\"width\":"<<p.footprint.Footprint.Width<<",\"height\":"<<p.footprint.Footprint.Height
                <<",\"row_pitch\":"<<p.footprint.Footprint.RowPitch<<",\"bytes\":"<<p.bytes<<"}";}
        meta<<"]}";job->metadata=meta.str();
        if(!job->lifetime.record(frame.command_list)) throw std::runtime_error("Unobservable recording");
        c.pending.push_back(job); // Retain before emitting any GPU commands.
        for(auto& p:job->planes) {
            D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource=p.source.Get();barrier.Transition.Subresource=0;
            barrier.Transition.StateBefore=p.state;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
            if(p.state!=D3D12_RESOURCE_STATE_COPY_SOURCE)frame.command_list->ResourceBarrier(1,&barrier);
            D3D12_TEXTURE_COPY_LOCATION src{},dst{};src.pResource=p.source.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.pResource=p.readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=p.footprint;
            frame.command_list->CopyTextureRegion(&dst,0,0,0,&src,&p.box);
            std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);
            if(p.state!=D3D12_RESOURCE_STATE_COPY_SOURCE)frame.command_list->ResourceBarrier(1,&barrier);
        }
        c.status="Recorded pass "+std::to_string(pass)+"; waiting for GPU completion";
    } catch(...) { c.status="Capture allocation/recording refused; rendering unchanged"; }
}
}
bool request_nr_pipeline_capture(const std::filesystem::path& directory) noexcept {
    auto& c=nr_capture_controller();std::lock_guard lock(c.mutex);
    if(c.armed || !c.pending.empty() || c.writing) return false;
    try {
        // A diagnostic writer may outlive a host detach. Pin this code module
        // only when explicitly requested; normal rendering has no new pin.
        HMODULE pinned{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&request_nr_pipeline_capture),&pinned)) return false;
        c.generation=GetTickCount64();c.root=directory/(std::to_string(GetCurrentProcessId())+"-"+std::to_string(c.generation));
        if(!std::filesystem::create_directories(c.root)) return false;
        c.ready=c.generation+10000;c.deadline=c.ready+30000;c.accepted=c.saved=0;c.views[0]=c.views[1]=0;c.armed=true;
        c.status="Armed: close menus; capture starts in 10 seconds";return true;
    } catch(...) {c.status="Cannot create diagnostic folder";return false;}
}
std::string nr_pipeline_capture_status() {
    auto& c=nr_capture_controller();std::lock_guard lock(c.mutex);return c.status;
}
}
