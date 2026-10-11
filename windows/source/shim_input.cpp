// The input shim in a latched (test) process (shim_input.hpp).
#include "shim_input.hpp"
#include "control_journal.hpp"
#include "desktop_settings.hpp"
#include "native_window.hpp"
#include "outrun_adapter.hpp"
#include "provider_host.hpp"
#include "platform/native_runtime.hpp"
#include "platform/runtime_input.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
namespace outrun::desktop {
namespace {
namespace h=dbce::shim::hosting;
namespace c=dbce::shim::control;
namespace sh=dbce::shim;
// The first in-game loop has no capture yet; the host still requires its lazy recorder contract.
struct NoCapture {
    void finish(){}
    struct Drained{h::RecOutcome outcome;bool joined;};
    Drained drain(h::Clock::time_point){return {h::RecOutcome::Complete,true};}
};
constexpr const char* CoreVersion="input-shim core (fe120fe line)";
constexpr const char* AdapterVersion="outrun adapter slice 2a.1";
struct State {
    fence::Plan plan;
    std::string profile;                       // the exact [Controls] bytes Core parses; their hash is the identity
    std::string transmission;
    bool configured{},started{},stopped{};
    std::unique_ptr<h::ProviderHost<NoCapture>> host;
    std::unique_ptr<Adapter> adapter;
    Signals last{};bool logged{};std::chrono::steady_clock::time_point last_log{};
};
State g;

std::uint64_t process_start(){
    FILETIME a,b,k,u;
    if(!GetProcessTimes(GetCurrentProcess(),&a,&b,&k,&u))return 0;
    return (std::uint64_t(a.dwHighDateTime)<<32)|a.dwLowDateTime;
}

// The journal identity, built once on the input thread before the control worker starts (no file is read here: the
// profile bytes are the ones Core parsed). The executable and adapter-schema hashes are not built yet; the sink
// reports them as unavailable rather than inventing them (CONTROL-JOURNAL.md section 10).
std::shared_ptr<const c::JournalIdentity> identity(){
    auto id=std::make_shared<c::JournalIdentity>();
    id->id.process=GetCurrentProcessId();
    id->id.process_start=process_start();
    for(std::size_t i=0;i<id->id.nonce.size()&&i<g.plan.nonce.size();++i)id->id.nonce[i]=std::uint8_t(g.plan.nonce[i]);
    const auto parsed=sh::read_profile(g.profile);
    id->id.profile=parsed.file_hash;             // read_profile hashes the very bytes it parsed
    id->latched=true;id->nonce_text=g.plan.nonce;
    id->executable_known=false;id->build_known=false;   // the identity builder (by handle, shared with the observer) is next
    id->core_version=CoreVersion;id->adapter_version=AdapterVersion;
    const auto devices=g.host->device_reports();
    for(const auto& d:devices)id->devices.push_back(d.identity);
    id->id.profile_devices=std::uint32_t(id->devices.size());
    const auto start=g.host->start_report();
    id->start_line=start.line;
    for(const auto& n:start.notes)id->start_line+="\n"+n;
    id->profile_revision=parsed.ok?parsed.section.revision:"";
    return id;
}

void start(){
    g.started=true;
    const auto hwnd=static_cast<HWND>(xbox_runtime::native_window().hwnd);
    IDirectInput8W* input=nullptr;
    if(FAILED(DirectInput8Create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,reinterpret_cast<void**>(&input),nullptr))||!input){
        std::printf("input shim: DirectInput8Create failed; the latched run has no shim input\n");return;}
    try{
        g.host=std::make_unique<h::ProviderHost<NoCapture>>(g.plan.run_root,g.plan.nonce,input,g.profile,true,std::make_unique<NoCapture>(),
            outrun_capability(),hwnd);
    }catch(const std::exception& e){std::printf("input shim: host refused: %s\n",e.what());return;}
    g.adapter=std::make_unique<Adapter>(g.transmission);
    (void)g.host->tick();   // the first frame: the start report follows the device checks
    const auto start=g.host->start_report();
    std::printf("%s\n",start.line.c_str());
    for(const auto& n:start.notes)std::printf("%s\n",n.c_str());
    c::JournalLimits limits;limits.cap_bytes=c::LatchedJournalCap;
    auto io=std::make_shared<c::FileIo>(g.plan.dbce_folder+L"\\inject.on",g.plan.dbce_folder+L"\\inject.txt",g.plan.run_root,false,identity(),limits);
    // The fence verified the run folder before the game started; this build sends no force at all.
    g.host->start_control(io,{true,true,true,g.plan.nonce});
    std::printf("input shim: latched; control worker on %s\\inject.txt, journal %s\\controls-journal.jsonl\n",
        fence::utf8(g.plan.dbce_folder).c_str(),fence::utf8(g.plan.run_root).c_str());
}

// The first loop's game-side evidence (the observer comes next): what the adapter wrote and what the game consumed,
// in runtime.log, on every mode change and at most every 250 ms while the values move.
void log_signals(const Signals& s,sh::Run run){
    const auto now=std::chrono::steady_clock::now();
    const bool moved=!g.logged||s.record_steer!=g.last.record_steer||s.record_accel!=g.last.record_accel||s.record_brake!=g.last.record_brake||
        s.record_buttons!=g.last.record_buttons||s.consumed_steer!=g.last.consumed_steer||s.consumed_accel!=g.last.consumed_accel||
        s.consumed_brake!=g.last.consumed_brake||s.gear!=g.last.gear;
    if(g.logged&&s.mode==g.last.mode&&(!moved||now-g.last_log<std::chrono::milliseconds(250)))return;
    std::printf("input shim: frame %llu run %s mode %u%s record steer %d accel %d brake %d buttons %08x consumed steer %d accel %d brake %d gear %u speed %.1f suppressed %08x\n",
        (unsigned long long)s.frame_seq,sh::run_name(run),s.mode,s.in_gameplay?" race":"",s.record_steer,s.record_accel,s.record_brake,s.record_buttons,
        s.consumed_steer,s.consumed_accel,s.consumed_brake,s.gear,double(s.speed),s.native_suppressed);
    g.last=s;g.logged=true;g.last_log=now;
}

GameView view(const platform::NativeRuntimeContext* runtime){
    GameView v{};
    if(!runtime)return v;
    v.mode=runtime->mode_state.current;
    const bool race=v.mode==16u||v.mode==18u;
    v.car=race;
    if(race){
        const auto* car=runtime->event_function36.car_select.car_799d18.data();
        std::memcpy(&v.gear,car+0x208,4);std::memcpy(&v.speed,car+0x1c4,4);
    }
    const auto& a=runtime->race.car_world.analog_7d6810.current;
    v.consumed_steer=a[0];v.consumed_accel=a[1];v.consumed_brake=a[2];
    return v;
}
}

void shim_input_configure(const fence::Plan& plan,const Ini& settings){
    g.plan=plan;
    g.profile="[Controls]\n";
    for(const auto& line:settings.section_lines("Controls"))g.profile+=line+"\n";
    const auto parsed=sh::read_profile(g.profile);
    g.transmission=parsed.ok?parsed.section.transmission:"";
    g.configured=true;
}

void shim_input_filter(platform::NativeInputState& out,const platform::NativeRuntimeContext* runtime){
    if(!g.configured||g.stopped)return;
    if(!g.started)start();
    if(!g.host)return;
    const auto& frame=g.host->tick();
    g.adapter->apply(frame,view(runtime),out);
    log_signals(g.adapter->signals(),frame.run);
}

void shim_input_shutdown(){
    if(!g.host||g.stopped)return;
    g.stopped=true;
    g.host->close_control_admission();
    (void)g.host->tick();                      // the final neutral Core tick after the close (host order)
    platform::NativeInputState neutral{};g.adapter->apply_neutral(true,neutral);
    const auto report=g.host->stop();
    std::printf("input shim: host stop %s%s\n",report.resolved?"resolved":"UNRESOLVED",report.control_complete?"":" (control journal incomplete)");
    std::fflush(stdout);
    if(!report.resolved)g.host->park_for_supervisor();   // the supervisor ends a latched process that did not resolve (P21)
}
}
