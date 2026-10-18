// Uses the runtime's transient comparison lease: no benchmark settings are saved.
namespace {
struct NrBenchmark {
    bool active{}, measuring{};
    unsigned stage{};
    ULONGLONG started{}, due{}, next_poll{};
    Settings original{};
    double start_ms{}, start_count{}, start_failures{};
    double sums[8]{}, counts[8]{};
    std::set<std::string> workloads[8];
    bool missing_workload[8]{};
    std::filesystem::path root;
    std::string status{"Idle"};
};
NrBenchmark nr_benchmark;
constexpr unsigned nr_benchmark_presets[]{1,2,2,1,2,1,1,2};
double benchmark_number(std::string_view json, const char* key) {
    const auto raw = member(json, key);
    double value{};
    const auto parsed = std::from_chars(raw.data(), raw.data()+raw.size(), value);
    return parsed.ec == std::errc{} && std::isfinite(value) ? value : -1;
}
void benchmark_stop(OverlayUiState& r, const OverlayRuntime& runtime, const std::string& message) {
    auto& b=nr_benchmark;
    const bool restored=command(r,runtime,"nr_comparison_end");
    b.active=false;
    b.status=message+(restored ? "; original settings restored" : "; RESTORE REQUEST FAILED; runtime timeout remains armed");
    try { nr_capture_write(b.root/"result.txt",b.status); } catch(...) { b.status += "; report write failed"; }
}
// Dimensions are periodically observed, not attached to individual GPU samples.
// Deliberately omit region origin: gaze and camera movement remain unrestricted.
void benchmark_observe(std::string_view snapshot) {
    auto& b=nr_benchmark;
    const auto details=member(snapshot,"nr_details");
    std::string signature;
    for(const auto* key : {"output_width","output_height","region_width","region_height","working_width","working_height"}) {
        const auto value=benchmark_number(details,key);
        if(value<=0) {b.missing_workload[b.stage]=true;return;}
        signature+=std::to_string(static_cast<unsigned>(value))+"/";
    }
    b.workloads[b.stage].insert(signature);
}
bool benchmark_apply(OverlayUiState& r, const OverlayRuntime& runtime, ULONGLONG now) {
    auto& b=nr_benchmark;
    const auto preset=std::to_string(nr_benchmark_presets[b.stage]);
    if(!command(r,runtime,"nr_comparison_set","NrProcessingOrder=0\nNrPreset="+preset+
        "\nNrPass2Preset="+preset+"\nNrPass3Preset="+preset)) return false;
    if(r.draft.nr_processing_order != NrProcessingOrder::after_upscaling ||
        r.draft.nr_preset != nr_benchmark_presets[b.stage] ||
        r.draft.nr_pass2_preset != nr_benchmark_presets[b.stage] ||
        r.draft.nr_pass3_preset != nr_benchmark_presets[b.stage]) return false;
    nr_capture_write(b.root/("stage-"+std::to_string(b.stage)+"-applied.json"),r.snapshot);
    b.measuring=false; b.due=now+8000;
    b.status="Stage "+std::to_string(b.stage+1)+"/8: network "+(preset=="1"?"A":"B")+", warming up; keep playing";
    return true;
}
void benchmark_start(OverlayUiState& r, const OverlayRuntime& runtime) {
    auto& b=nr_benchmark;
    try {
        refresh(r,runtime,true);
        if(nr_capture.active || !r.draft.nr_enabled) throw std::runtime_error("Enable NR and finish other captures first");
        if(benchmark_number(r.snapshot,"renderer")!=1) throw std::runtime_error("This benchmark currently requires native D3D12");
        if(member(r.snapshot,"nr_benchmark_timing").empty()) throw std::runtime_error("Matching benchmark runtime required");
        b=NrBenchmark{}; b.original=r.draft;
        static unsigned run_id{};
        b.root=nr_capture_module()/"nr-benchmarks"/(std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(++run_id));
        if(!std::filesystem::create_directories(b.root)) throw std::runtime_error("Cannot create benchmark folder");
        nr_capture_write(b.root/"original.json",r.snapshot);
        nr_capture_write(b.root/"README.txt","D3D12 after-upscaling gameplay NR benchmark. AB/BA/BA/AB; 8s warmup + 8s sampling each.\n"
            "All enabled NR passes share the tested preset; appearance, working scales and other tuning unchanged.\n"
            "GPU ms is mean sampled NR block per view invocation including enabled passes, not frame time or FPS.\n"
            "No screenshots. Play normally and look around naturally; keep graphics/FG settings unchanged.\n"
            "Dimension observations exclude gaze position. They are not GPU-sample-matched workload proof.\n"
            "Pair variation and dimension mismatches can make results inconclusive. No causal FPS or resolved-network claim.\n");
        if(!command(r,runtime,"nr_comparison_begin")) throw std::runtime_error("Runtime refused benchmark lease");
        b.active=true; b.stage=0; b.started=GetTickCount64(); b.due=b.started+5000;
        b.status="Close menu within 5 seconds, then play normally for about 2 minutes. Gaze stays active.";
    } catch(const std::exception& e) { if(b.active) benchmark_stop(r,runtime,e.what()); else b.status=e.what(); }
}
}
void tick_nr_benchmark_at(OverlayUiState& r, const OverlayRuntime& runtime, bool menu_open, ULONGLONG now) {
    auto& b=nr_benchmark; if(!b.active) return;
    try {
        if(now-b.started>150000) {benchmark_stop(r,runtime,"INVALID: benchmark timed out");return;}
        if(now<b.started+5000) return;
        if(menu_open) {benchmark_stop(r,runtime,"Cancelled: menu reopened or countdown expired with menu open");return;}
        if(now<b.next_poll) return;
        b.next_poll=now+250;
        // First transition after the five-second countdown.
        if(b.due==b.started+5000) {
            if(!benchmark_apply(r,runtime,now)) benchmark_stop(r,runtime,"INVALID: preset transaction rejected");
            return;
        }
        refresh(r,runtime,true);
        if(r.draft.nr_processing_order!=NrProcessingOrder::after_upscaling ||
           r.draft.nr_preset!=nr_benchmark_presets[b.stage] ||
           r.draft.nr_pass2_preset!=nr_benchmark_presets[b.stage] ||
           r.draft.nr_pass3_preset!=nr_benchmark_presets[b.stage]) {
            benchmark_stop(r,runtime,"INVALID: settings changed or lease expired");return;
        }
        const auto timing=member(r.snapshot,"nr_benchmark_timing");
        const auto ms=benchmark_number(timing,b.original.nr_foveated?"foveated_ms":"full_ms");
        const auto count=benchmark_number(timing,b.original.nr_foveated?"foveated_count":"full_count");
        const auto failures=benchmark_number(member(r.snapshot,"nr_details"),"failures");
        if(ms<0 || count<0 || failures<0) {benchmark_stop(r,runtime,"INVALID: missing GPU counters");return;}
        if(b.measuring) benchmark_observe(r.snapshot);
        if(now<b.due) return;
        if(!b.measuring) {
            b.start_ms=ms; b.start_count=count; b.start_failures=failures;
            b.measuring=true; b.due=now+8000;
            benchmark_observe(r.snapshot);
            nr_capture_write(b.root/("stage-"+std::to_string(b.stage)+"-start.json"),r.snapshot);
            b.status="Stage "+std::to_string(b.stage+1)+"/8: sampling GPU NR time; keep playing";
            return;
        }
        const auto samples=count-b.start_count, sum=ms-b.start_ms;
        nr_capture_write(b.root/("stage-"+std::to_string(b.stage)+"-end.json"),r.snapshot);
        if(samples<20 || !(sum>0) || failures!=b.start_failures) {
            benchmark_stop(r,runtime,"INVALID: insufficient fresh GPU samples or NR failures");return;
        }
        b.sums[b.stage]=sum; b.counts[b.stage]=samples;
        if(++b.stage<8) {
            if(!benchmark_apply(r,runtime,now)) benchmark_stop(r,runtime,"INVALID: next stage rejected");
            return;
        }
        std::ostringstream report; report.imbue(std::locale::classic());
        double effects[4]{}, mean{}, low=1e9, high=-1e9;
        unsigned matched{};
        for(unsigned p=0;p<4;++p) {
            const unsigned i=p*2, j=i+1;
            const bool comparable=!b.missing_workload[i] && !b.missing_workload[j] &&
                !b.workloads[i].empty() && b.workloads[i]==b.workloads[j];
            const auto ai=nr_benchmark_presets[i]==1?i:j, bi=ai==i?j:i;
            const auto a=b.sums[ai]/b.counts[ai], bb=b.sums[bi]/b.counts[bi];
            const auto effect=100*(a-bb)/a;
            report<<"Pair "<<p+1<<": A="<<a<<" ms; B="<<bb<<" ms; B time reduction="<<effect
                <<"%; observed dimensions "<<(comparable?"matched":"MISMATCHED/MISSING (excluded)")<<'\n';
            if(comparable) {effects[matched++]=effect;mean+=effect;low=(std::min)(low,effect);high=(std::max)(high,effect);}
        }
        if(matched) {
            mean/=matched; double variance{};
            for(unsigned i=0;i<matched;++i) variance+=(effects[i]-mean)*(effects[i]-mean);
            const double sd=matched>1?std::sqrt(variance/(matched-1)):0;
            const bool consistent=matched==4 && (low>0 || high<0) && std::abs(mean)>2*sd;
            report<<(consistent?"Consistent observed trend (not a controlled causal test)":"INCONCLUSIVE: too few comparable pairs or gameplay variation")
                <<". Matched pairs="<<matched<<"/4; mean B reduction="<<mean<<"%; range="<<low<<".."<<high
                <<"%; pair SD="<<sd<<" percentage points (not a confidence interval).\n";
        } else report<<"INCONCLUSIVE: no dimension-comparable pairs.\n";
        for(unsigned i=0;i<8;++i) {
            report<<"Stage "<<i+1<<" preset="<<nr_benchmark_presets[i]<<" samples="<<b.counts[i]<<" mean_ms="<<b.sums[i]/b.counts[i]<<" observed_dimensions=";
            for(const auto& signature:b.workloads[i]) report<<signature<<' ';
            report<<'\n';
        }
        report<<"After-upscaling GPU NR block per view, including enabled passes; NOT FPS or total frame time. "
              <<"Gaze remained active. Dimensions are periodic observations, not per-GPU-sample metadata. "
              <<"Scene/content and GPU contention still confound comparisons. Warmups exclude preset-switch hitches. "
              <<"Requested presets are not resolved-network proof.";
        benchmark_stop(r,runtime,report.str());
    } catch(const std::exception& e) {benchmark_stop(r,runtime,std::string("INVALID: ")+e.what());}
}
void tick_nr_benchmark(OverlayUiState& r, const OverlayRuntime& runtime, bool menu_open) {
    tick_nr_benchmark_at(r,runtime,menu_open,GetTickCount64());
}
void draw_nr_benchmark(OverlayUiState& r, const OverlayRuntime& runtime) {
    if(!ImGui::CollapsingHeader("NR automatic network A/B benchmark")) return;
    ImGui::TextWrapped("Play normally, look around naturally: eye tracking stays active. Tests four balanced A/B pairs AFTER upscaling across all enabled passes, preserving appearance and working scales. Five seconds to close menu, then about 128 seconds. Reports GPU NR-block cost and pair variation, not generated FPS. Dimension mismatches are excluded; inconsistent results are inconclusive. Settings restore automatically; no images captured.");
    if(nr_benchmark.active) {
        if(ImGui::Button("Cancel benchmark and restore")) benchmark_stop(r,runtime,"Cancelled");
    } else {
        ImGui::BeginDisabled(nr_capture.active);
        if(ImGui::Button("Start after-upscaling A/B benchmark")) benchmark_start(r,runtime);
        ImGui::EndDisabled();
    }
    ImGui::TextWrapped("%s",nr_benchmark.status.c_str());
    if(!nr_benchmark.root.empty()) ImGui::TextWrapped("%s",nr_benchmark.root.string().c_str());
}

#ifdef CHEEKY_OVERLAY_TEST_DESKTOP
void test_nr_benchmark_controller() {
    static Settings live{}, saved{};
    static double total{}, samples{}, failures{};
    static bool lease{};
    static unsigned ends{};
    live=Settings{}; live.nr_enabled=true; live.nr_second_pass=true;
    live.nr_processing_order=NrProcessingOrder::before_upscaling;
    live.nr_preset=0; live.nr_pass2_preset=1; saved=live;
    total=1000; samples=100; failures=0; lease=false; ends=0;
    OverlayRuntime runtime{1};
    runtime.command=[](std::uint64_t,const char* raw) {
        std::istringstream in(raw); std::string line, action;
        std::getline(in,line);std::getline(in,line);std::getline(in,action);
        if(action=="nr_comparison_begin") {if(lease)return false;saved=live;lease=true;return true;}
        if(action=="nr_comparison_end") {live=saved;lease=false;++ends;return true;}
        if(action!="nr_comparison_set" || !lease) return false;
        while(std::getline(in,line)) {const auto eq=line.find('='); if(eq==line.npos || !set_named_setting(live,line.substr(0,eq),line.substr(eq+1)))return false;}
        return true;
    };
    runtime.snapshot=[](char* out,std::uint32_t size) {
        std::ostringstream json;
        json<<"{\"renderer\":1,\"settings\":{\"NrEnabled\":true,\"NrSecondPass\":true,\"NrFoveated\":true,\"NrProcessingOrder\":"<<static_cast<unsigned>(live.nr_processing_order)
            <<",\"NrPreset\":"<<live.nr_preset<<",\"NrPass2Preset\":"<<live.nr_pass2_preset<<",\"NrPass3Preset\":"<<live.nr_pass3_preset
            <<"},\"nr_benchmark_timing\":{\"foveated_ms\":"<<total<<",\"foveated_count\":"<<samples<<"},\"nr_details\":{\"output_width\":2000,\"output_height\":2000,\"region_width\":800,\"region_height\":800,\"working_width\":400,\"working_height\":400,\"region_x\":"<<samples<<",\"failures\":"<<failures<<"}}";
        const auto text=json.str();if(text.size()+1>size)return false;
        std::copy(text.begin(),text.end(),out);out[text.size()]=0;return true;
    };
    auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    OverlayUiState r;
    benchmark_start(r,runtime);
    check(nr_benchmark.active && lease,"Benchmark starts transient lease");
    tick_nr_benchmark_at(r,runtime,true,nr_benchmark.started+1000);
    check(nr_benchmark.active,"Countdown allows closing menu");
    tick_nr_benchmark_at(r,runtime,false,nr_benchmark.started+5000);
    for(unsigned i=0;i<8;++i) {
        check(live.nr_preset==nr_benchmark_presets[i] && live.nr_pass2_preset==nr_benchmark_presets[i],"A/B/B/A applies to both passes");
        nr_benchmark.next_poll=0;
        tick_nr_benchmark_at(r,runtime,false,nr_benchmark.due);
        check(nr_benchmark.measuring,"Warmup ends before measurement");
        samples+=100;total+=nr_benchmark_presets[i]==1 ? 1000 : 500;
        nr_benchmark.next_poll=0;
        tick_nr_benchmark_at(r,runtime,false,nr_benchmark.due);
    }
    check(!lease && !nr_benchmark.active && ends==1,"Completion restores lease");
    check(live.nr_processing_order==saved.nr_processing_order && live.nr_pass2_preset==saved.nr_pass2_preset,"Completion restores original order and pass2");
    check(nr_benchmark.status.find("B time reduction=50%")!=std::string::npos,"Known GPU samples yield 50 percent reduction");
    check(nr_benchmark.status.find("Consistent observed trend")!=std::string::npos,"Moving gaze origins do not invalidate equal dimensions");
    // Repeat with alternating content effects and one changed workload pair.
    benchmark_start(r,runtime);
    check(nr_benchmark.active && lease,"Second gameplay run starts");
    tick_nr_benchmark_at(r,runtime,false,nr_benchmark.started+5000);
    for(unsigned i=0;i<8;++i) {
        nr_benchmark.next_poll=0;
        tick_nr_benchmark_at(r,runtime,false,nr_benchmark.due);
        if(i==0) nr_benchmark.workloads[i].insert("different-resolution");
        samples+=100;
        total+=nr_benchmark_presets[i]==1 ? 1000 : (i/2%2 ? 1500 : 500);
        nr_benchmark.next_poll=0;
        tick_nr_benchmark_at(r,runtime,false,nr_benchmark.due);
    }
    check(!lease && nr_benchmark.status.find("INCONCLUSIVE")!=std::string::npos,"Variable gameplay must not claim a consistent advantage");
    check(nr_benchmark.status.find("Matched pairs=3/4")!=std::string::npos,"Mismatched observed dimensions exclude a pair");
    // Exercise cancellation and stale/no-sample refusal without another directory.
    auto arm=[&](){lease=true;nr_benchmark.active=true;nr_benchmark.started=GetTickCount64()-6000;nr_benchmark.next_poll=0;nr_benchmark.due=GetTickCount64();nr_benchmark.stage=0;};
    arm();tick_nr_benchmark_at(r,runtime,true,GetTickCount64());
    check(!lease && !nr_benchmark.active,"Menu reopen restores settings");
    arm();live.nr_processing_order=NrProcessingOrder::after_upscaling;live.nr_preset=live.nr_pass2_preset=live.nr_pass3_preset=1;
    nr_benchmark.measuring=true;nr_benchmark.start_count=samples;nr_benchmark.start_ms=total;nr_benchmark.start_failures=failures;
    tick_nr_benchmark_at(r,runtime,false,GetTickCount64());
    check(!lease && nr_benchmark.status.find("INVALID")!=std::string::npos,"No fresh samples cannot produce a result");
    nr_benchmark=NrBenchmark{};
}
#endif
