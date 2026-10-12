// The input shim in a latched (test) process (shim_input.hpp).
#include "shim_input.hpp"
#include "control_journal.hpp"
#include "desktop_keyboard.hpp"
#include "desktop_settings.hpp"
#include "native_window.hpp"
#include "outrun_adapter.hpp"
#include "outrun_recorder.hpp"
#include "outrun_replay.hpp"
#include "provider_host.hpp"
#include "journal_identity.hpp"
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
constexpr const char* CoreVersion="input-shim core (fe120fe line)";
constexpr const char* AdapterVersion="outrun adapter slice 2a.1";
struct State {
    fence::Plan plan;
    std::string profile;                       // the exact [Controls] bytes Core parses; their hash is the identity
    std::string transmission;
    bool configured{},started{},stopped{};
    std::unique_ptr<h::ProviderHost<ShimRecorder>> host;
    std::unique_ptr<Adapter> adapter;
    bool replay_run{};                         // [ShimReplay] Tape= names a tape: an R3 replay run (outrun_replay.hpp)
    std::unique_ptr<Tape> tape;                // null in a replay run whose tape was refused: nothing is played
    Signals last{};bool logged{};std::chrono::steady_clock::time_point last_log{};
};
State g;

// Ordinary play: no capture. The host's recorder contract (lazy; drain and finish at stop), with nothing to write.
struct NoCapture {
    void finish(){}
    struct Drained{h::RecOutcome outcome;bool joined;};
    Drained drain(h::Clock::time_point){return {h::RecOutcome::Complete,true};}
};
struct Ordinary {
    std::string profile,transmission,folder;
    bool configured{},started{},stopped{};
    std::unique_ptr<h::ProviderHost<NoCapture>> host;
    std::unique_ptr<Adapter> adapter;
};
Ordinary o;

// The journal identity (host/journal_identity.hpp, shared with dbce_shim.dll).
std::shared_ptr<const c::JournalIdentity> identity(){
    return h::journal_identity(*g.host,g.plan.nonce,g.profile,CoreVersion,AdapterVersion);
}

void start(){
    g.started=true;
    const auto hwnd=static_cast<HWND>(xbox_runtime::native_window().hwnd);
    IDirectInput8W* input=nullptr;
    if(FAILED(DirectInput8Create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,reinterpret_cast<void**>(&input),nullptr))||!input){
        std::printf("input shim: DirectInput8Create failed; the latched run has no shim input\n");return;}
    try{
        CaptureSettings capture;
        capture.folder=std::filesystem::path(g.plan.run_root)/L"capture";capture.profile=g.profile;
        capture.run_nonce=g.plan.nonce;capture.adapter_version=AdapterVersion;
        g.host=std::make_unique<h::ProviderHost<ShimRecorder>>(g.plan.run_root,g.plan.nonce,input,g.profile,true,
            std::make_unique<ShimRecorder>(capture),outrun_capability(),hwnd);
    }catch(const std::exception& e){std::printf("input shim: host refused: %s\n",e.what());return;}
    g.adapter=std::make_unique<Adapter>(g.transmission);
    (void)g.host->tick();   // the first frame: the start report follows the device checks
    g.host->recorder().set_devices(g.host->device_reports());
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
    // R3: a replay run loads its tape before the game loop starts. A refused tape plays nothing (the run stays neutral).
    const auto tape=settings.text("ShimReplay","Tape");
    if(!tape.empty()){
        g.replay_run=true;
        try{
            g.tape=std::make_unique<Tape>(std::filesystem::u8path(tape));
            std::printf("input shim: replay tape %s: %zu race rows (from %llu rows, frames %llu..%llu)\n",tape.c_str(),g.tape->size(),
                (unsigned long long)g.tape->source_rows(),(unsigned long long)g.tape->row(0).seq,(unsigned long long)g.tape->row(g.tape->size()-1).seq);
        }catch(const std::exception& e){std::printf("input shim: replay tape refused: %s; nothing is played\n",e.what());}
        std::fflush(stdout);
    }
    g.configured=true;
}

void shim_input_filter(platform::NativeInputState& out,const platform::NativeRuntimeContext* runtime){
    if(!g.configured||g.stopped)return;
    if(!g.started)start();
    if(!g.host)return;
    const auto& live=g.host->tick();
    const auto v=view(runtime);
    std::int64_t row=NotReplay;const sh::Frame* frame=&live;
    if(g.replay_run){
        row=-1;
        if(g.tape){
            const bool was=g.tape->started(),done=g.tape->ended();
            frame=&g.tape->play(live,v.car,row);
            if(!was&&g.tape->started())std::printf("input shim: replay start: tape row 0 at live frame %llu mode %u\n",(unsigned long long)live.seq,v.mode);
            if(!done&&g.tape->ended())std::printf("input shim: replay end at live frame %llu: %zu of %zu tape rows played (%s)\n",
                (unsigned long long)live.seq,g.tape->played(),g.tape->size(),g.tape->end_reason().c_str());
        }
    }
    g.adapter->apply(*frame,v,out);
    g.host->recorder().push(Snapshot{*frame,g.adapter->signals(),out,v,row});   // one capture row per tick, copied now
    log_signals(g.adapter->signals(),live.run);
}

bool shim_backend_selected(const Ini& settings){
    const auto backend=settings.text("Input","Backend","Native");
    return _stricmp(backend.c_str(),"Shim")==0;
}

void shim_input_configure_ordinary(const Ini& settings,const std::string& folder){
    o.profile="[Controls]\n";
    for(const auto& line:settings.section_lines("Controls"))o.profile+=line+"\n";
    const auto parsed=sh::read_profile(o.profile);
    o.transmission=parsed.ok?parsed.section.transmission:"";
    o.folder=folder;
    o.configured=true;
}

void shim_input_filter_ordinary(platform::NativeInputState& out,const platform::NativeRuntimeContext* runtime){
    if(!o.configured||o.stopped)return;
    if(!o.started){
        o.started=true;
        o.adapter=std::make_unique<Adapter>(o.transmission);   // before the host: the keyboard fallback needs it (romi 25245)
        IDirectInput8W* input=nullptr;
        if(FAILED(DirectInput8Create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,reinterpret_cast<void**>(&input),nullptr))||!input){
            std::printf("input shim: DirectInput8Create failed; ordinary play has no shim input\n");std::fflush(stdout);return;}
        try{
            o.host=std::make_unique<h::ProviderHost<NoCapture>>(std::filesystem::u8path(o.folder).wstring(),"ordinary-desktop",input,o.profile,false,
                std::make_unique<NoCapture>(),outrun_capability(),static_cast<HWND>(xbox_runtime::native_window().hwnd));
        }catch(const std::exception& e){std::printf("input shim: host refused: %s\n",e.what());std::fflush(stdout);return;}
        (void)o.host->tick();
        const auto start=o.host->start_report();
        std::printf("%s\n",start.line.c_str());
        for(const auto& n:start.notes)std::printf("%s\n",n.c_str());
        std::printf("input shim: ordinary play on the shim backend ([Input] Backend = Shim); keyboard and pad keep their native paths\n");
        std::fflush(stdout);
    }
    using outrun::desktop::desktop_key;
    Keyboard kb;kb.any=outrun::desktop::desktop_any_key();
    kb.enter=desktop_key(0x1c);kb.esc=desktop_key(0x01);kb.up=desktop_key(0xc8);kb.down=desktop_key(0xd0);kb.left=desktop_key(0xcb);kb.right=desktop_key(0xcd);
    // No host (DirectInput or the host refused): the keyboard keeps its path anyway. An empty frame (every action
    // unbound) through the same adapter: the PC layer on, the menu keys, and the race on the PC layer's record.
    static const sh::Frame no_shim{};
    o.adapter->apply(o.host?o.host->tick():no_shim,view(runtime),out,&kb);
}

void shim_input_shutdown_ordinary(){
    if(!o.host||o.stopped)return;
    o.stopped=true;
    (void)o.host->tick();                      // a final poll before the stop (host order); Core is still ordinary live here
    const auto report=o.host->stop();
    std::printf("input shim: ordinary host stop %s\n",report.resolved?"resolved":"UNRESOLVED");std::fflush(stdout);
}

void shim_input_shutdown(){
    if(!g.host||g.stopped)return;
    g.stopped=true;
    g.host->recorder().set_devices(g.host->device_reports());   // the final device table for the capture members
    g.host->close_control_admission();
    (void)g.host->tick();                      // the final neutral Core tick after the close (host order)
    platform::NativeInputState neutral{};g.adapter->apply_neutral(true,neutral);
    const auto report=g.host->stop();
    std::printf("input shim: host stop %s%s; capture %llu rows%s%s\n",report.resolved?"resolved":"UNRESOLVED",
        report.control_complete?"":" (control journal incomplete)",(unsigned long long)g.host->recorder().pushed(),
        g.host->recorder().refusal().empty()?"":", refused: ",g.host->recorder().refusal().c_str());
    std::fflush(stdout);
    if(!report.resolved)g.host->park_for_supervisor();   // the supervisor ends a latched process that did not resolve (P21)
}
}
