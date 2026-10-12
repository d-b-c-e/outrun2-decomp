// The input shim in a latched (test) process (shim_input.hpp).
#include "shim_input.hpp"
#include "control_journal.hpp"
#include "desktop_keyboard.hpp"
#include "desktop_settings.hpp"
#include "native_window.hpp"
#include "outrun_adapter.hpp"
#include "ordinary_recording.hpp"
#include "outrun_recorder.hpp"
#include "outrun_replay.hpp"
#include "provider_host.hpp"
#include "shim_steer.hpp"
#include "wheelffb_bridge.hpp"
#include "ffb_live.hpp"
#include "journal_identity.hpp"
#include "platform/native_runtime.hpp"
#include "platform/runtime_input.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
namespace outrun::desktop {
namespace {
namespace h=dbce::shim::hosting;
namespace c=dbce::shim::control;
namespace sh=dbce::shim;
constexpr const char* CoreVersion="input-shim core (fe120fe line)";
constexpr const char* AdapterVersion="outrun adapter slice 2a.1";
// The signals line's own throttle (log_signals): the last line written, per path.
struct SignalLog { Signals last{};bool logged{};std::chrono::steady_clock::time_point last_log{};std::string last_extra; };
// The start reports (initial_report / resolved_report below): when the first was printed, and whether the resolved one has.
struct StartReports { bool resolved{};std::chrono::steady_clock::time_point since{}; };
struct State {
    fence::Plan plan;
    std::string profile;                       // the exact [Controls] bytes Core parses; their hash is the identity
    std::string transmission;
    bool configured{},started{},stopped{};
    std::unique_ptr<h::ProviderHost<ShimRecorder>> host;
    std::unique_ptr<Adapter> adapter;
    bool replay_run{};                         // [ShimReplay] Tape= names a tape: an R3 replay run (outrun_replay.hpp)
    std::unique_ptr<Tape> tape;                // null in a replay run whose tape was refused: nothing is played
    SignalLog log;
    StartReports reports;
};
State g;

// The F4 delivery's hand-over of the wheel to and from WheelFfb (ffbdev::Owner over the bridge), on the game thread.
struct BridgeOwner final:ffbdev::Owner {
    sh::WheelFfbBridge* b{};
    bool begin_open()override{return b&&b->begin_promotion();}
    void end_open(bool held)override{if(b)b->end_promotion(held);}
    void begin_release()override{if(b)b->begin_release();}
    void end_release()override{if(b)b->end_release();}
    bool library_quiet()const override{return !b||b->library_quiet();}
    bool enter_library()override{return !b||b->enter_library();}
    void leave_library()override{if(b)b->leave_library();}
};
struct Ordinary {
    std::string profile,transmission,folder;
    bool configured{},started{},stopped{};
    std::unique_ptr<h::ProviderHost<OrdinaryRecorder>> host;
    // Play with Recording: the request (shim_input_arm_recording), then the recorder handle (unarmed without one; the
    // host holds a copy). `pending` is this tick's row from the filter, completed and pushed at the consumed-input seam.
    std::optional<session::Request> record;std::string record_exe;
    OrdinaryRecorder rec;
    std::optional<Snapshot> pending;
    std::uint64_t ticks{};bool f11{};
    std::unique_ptr<Adapter> adapter;
    bool debug{};SignalLog log;               // OR2_INPUT_DEBUG: the signals line, as in a latched run, plus the keyboard state
    Steering steering;                         // [Wheel], as the native wheel path reads it
    std::optional<dbce::controls::Binding> steer;   // [Controls] steer from the parse of `profile` (shim_input_steer)
    const sh::Frame* frame{};                  // Core's latest frame (the host's tick), for shim_input_steer's `live`
    std::shared_ptr<sh::WheelFfbBridge> bridge;   // with the wheel output and Input = Shim (wheelffb_bridge.hpp)
    BridgeOwner owner;
    StartReports reports;
};
Ordinary o;

// The start report at the first tick comes before the provider's first open, so it lists every bound action as
// device-missing. It is labelled initial, and a resolved report follows once every profile device is open, or after
// ResolveMs with the devices still missing named (romi 25546; DRIVE logs resolved identities after adoption too).
constexpr long long ResolveMs=3000;
template<class Host> void initial_report(const Host& host,StartReports& r){
    r.since=std::chrono::steady_clock::now();
    const auto start=host.start_report();
    std::printf("input shim: initial start report, at the first tick before any device open (the resolved report follows)\n");
    std::printf("%s\n",start.line.c_str());
    for(const auto& n:start.notes)std::printf("%s\n",n.c_str());
}
template<class Host> void resolved_report(const Host& host,StartReports& r){
    if(r.resolved)return;
    const auto devices=host.device_reports();
    std::string missing;
    for(const auto& d:devices)if(!d.opened)missing+=(missing.empty()?"":", ")+("\""+d.identity.name+"\" "+d.identity.dev);
    const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-r.since).count();
    if(!missing.empty()&&ms<ResolveMs)return;
    r.resolved=true;
    const auto start=host.start_report();
    if(missing.empty())std::printf("input shim: resolved start report: all %zu profile devices open after %lld ms\n",devices.size(),ms);
    else std::printf("input shim: resolved start report after %lld ms; not open: %s\n",ms,missing.c_str());
    std::printf("%s\n",start.line.c_str());
    for(const auto& n:start.notes)std::printf("%s\n",n.c_str());
    for(std::size_t i=0;i<devices.size();++i){
        const auto& d=devices[i];
        std::printf("input shim: device %zu \"%s\" instance %s product %s: %s, epoch %u%s%s\n",i,d.identity.name.c_str(),d.identity.dev.c_str(),
            d.identity.prod.c_str(),d.status.c_str(),unsigned(d.epoch),d.identity.interface_path.empty()?"":", path ",d.identity.interface_path.c_str());
    }
    std::fflush(stdout);
}

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
    initial_report(*g.host,g.reports);
    c::JournalLimits limits;limits.cap_bytes=c::LatchedJournalCap;
    auto io=std::make_shared<c::FileIo>(g.plan.dbce_folder+L"\\inject.on",g.plan.dbce_folder+L"\\inject.txt",g.plan.run_root,false,identity(),limits);
    // The fence verified the run folder before the game started; this build sends no force at all.
    g.host->start_control(io,{true,true,true,g.plan.nonce});
    std::printf("input shim: latched; control worker on %s\\inject.txt, journal %s\\controls-journal.jsonl\n",
        fence::utf8(g.plan.dbce_folder).c_str(),fence::utf8(g.plan.run_root).c_str());
}

// The first loop's game-side evidence (the observer comes next): what the adapter wrote and what the game consumed,
// in runtime.log, on every mode change and at most every 250 ms while the values move.
// Ordinary play logs it only with OR2_INPUT_DEBUG, with `extra` (the keyboard state) at the end of the line.
void log_signals(const Signals& s,const char* run,SignalLog& l,const std::string& extra={}){
    const auto now=std::chrono::steady_clock::now();
    const bool moved=!l.logged||s.record_steer!=l.last.record_steer||s.record_accel!=l.last.record_accel||s.record_brake!=l.last.record_brake||
        s.record_buttons!=l.last.record_buttons||s.consumed_steer!=l.last.consumed_steer||s.consumed_accel!=l.last.consumed_accel||
        s.consumed_brake!=l.last.consumed_brake||s.gear!=l.last.gear||extra!=l.last_extra;   // the keyboard state too (romi 25597)
    if(l.logged&&s.mode==l.last.mode&&(!moved||now-l.last_log<std::chrono::milliseconds(250)))return;
    std::printf("input shim: frame %llu run %s mode %u%s record steer %d accel %d brake %d buttons %08x consumed steer %d accel %d brake %d gear %u speed %.1f suppressed %08x%s\n",
        (unsigned long long)s.frame_seq,run,s.mode,s.in_gameplay?" race":"",s.record_steer,s.record_accel,s.record_brake,s.record_buttons,
        s.consumed_steer,s.consumed_accel,s.consumed_brake,s.gear,double(s.speed),s.native_suppressed,extra.c_str());
    l.last=s;l.logged=true;l.last_log=now;l.last_extra=extra;
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

// Play with Recording, on the input thread (the producer): the stop, with the final device table for the members.
void stop_recording(const char* by){
    if(!o.rec.recording())return;
    if(o.host)o.rec.set_devices(o.host->device_reports());
    o.rec.stop(by);
    std::printf("input shim: recording stopped (%s) after %llu rows; saving\n",by,(unsigned long long)o.rec.pushed());std::fflush(stdout);
    o.rec.publish_status();
}
// The request-bound stop: <result>\stop.request holding this request's id (anything else is ignored, once logged).
bool stop_requested(){
    const auto* q=o.rec.request();
    if(!q)return false;
    const auto path=q->result/L"stop.request";
    HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;
    char b[129]{};DWORD n=0;
    const bool read=ReadFile(f,b,128,&n,nullptr)!=0;
    CloseHandle(f);
    std::string id(b,read?n:0);
    while(!id.empty()&&(id.back()=='\r'||id.back()=='\n'||id.back()==' '))id.pop_back();
    if(id==q->request_id)return true;
    static bool logged=false;
    if(!logged){logged=true;std::printf("input shim: stop.request ignored: it does not name this request\n");std::fflush(stdout);}
    return false;
}
// The consumed-input seam (platform::native_input_consumed_hook, after 453BB0's arbitration and race update): what
// the game consumed completes this tick's row, which is pushed now (romi 33134: the final effective input).
void on_consumed(const platform::NativeInputState& in,const platform::NativeRuntimeContext&){
    if(!o.pending)return;
    o.pending->consumed=in;o.pending->has_consumed=true;
    o.rec.push(*o.pending);
    o.pending.reset();
    if(!o.rec.recording()){   // the recorder refused this row or its start: stopped by the recorder itself
        std::printf("input shim: recording refused after %llu rows; saving the failure\n",(unsigned long long)o.rec.pushed());std::fflush(stdout);
        o.rec.publish_status();
        return;
    }
    if(o.rec.at_limit())stop_recording("limit");
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
    resolved_report(*g.host,g.reports);
    g.adapter->apply(*frame,v,out);
    g.host->recorder().push(Snapshot{*frame,g.adapter->signals(),out,v,row});   // one capture row per tick, copied now
    log_signals(g.adapter->signals(),sh::run_name(live.run),g.log);
}

std::string shim_input_status_text(){
    if(!o.configured)return {};
    if(!o.host)return o.started?"the shared input shim did not start (see runtime.log); the keyboard and the pad still drive"
                               :"the shared input shim starts with the first frame";
    const auto devices=o.host->device_reports();
    std::size_t open=0;for(const auto& d:devices)if(d.opened)++open;
    std::string s="shared input shim: "+std::to_string(open)+" of "+std::to_string(devices.size())+" profile devices open";
    for(const auto& d:devices)s+="; \""+d.identity.name+"\" "+d.status;
    return s;
}

ShimSteer shim_input_steer(){
    if(!o.configured)return {};
    const auto* steer=o.steer?&*o.steer:nullptr;
    if(o.host)return shim_steer(steer,o.host->device_reports(),o.frame);
    auto s=shim_steer(steer,{});
    if(steer)s.status=o.started?"the shared input shim did not start":"the shared input shim starts with the first frame";
    return s;
}

void shim_input_configure_ordinary(const Ini& settings,const std::string& folder){
    o.profile="[Controls]\n";
    for(const auto& line:settings.section_lines("Controls"))o.profile+=line+"\n";
    const auto parsed=sh::read_profile(o.profile);
    o.transmission=parsed.ok?parsed.section.transmission:"";
    o.steer.reset();
    if(parsed.ok)if(const auto* b=dbce::controls::find(parsed.section,"steer"))o.steer=*b;
    o.folder=folder;
    const auto steering=wheel_steering(settings);   // as wheel_input reads it; non-finite values are the defaults
    o.steering.sensitivity=steering.sensitivity;o.steering.dead_zone=steering.dead_zone;
    o.debug=std::getenv("OR2_INPUT_DEBUG")!=nullptr;
    o.rec=OrdinaryRecorder{};
    if(o.record){
        OrdinaryRecorder::Arm a;
        a.request=*o.record;a.profile=o.profile;a.exe_sha256=o.record_exe;a.adapter_version=AdapterVersion;
        o.rec=OrdinaryRecorder(std::move(a));
        platform::native_input_consumed_hook()=on_consumed;
        o.rec.publish_status(true);
        std::printf("input shim: Play with Recording: request %s, capture %s (contract @2, at most 5 minutes); F11 stops\n",
            o.record->request_id.c_str(),session::utf8(o.record->recording/L"capture").c_str());
        std::fflush(stdout);
    }
    o.configured=true;
}

void shim_input_arm_recording(const session::Request& request,const std::string& exe_sha256){o.record=request;o.record_exe=exe_sha256;}

std::string shim_input_recording_badge(){return o.rec.badge();}

void shim_input_filter_ordinary(platform::NativeInputState& out,const platform::NativeRuntimeContext* runtime){
    if(!o.configured||o.stopped)return;
    if(!o.started){
        o.started=true;
        o.adapter=std::make_unique<Adapter>(o.transmission,HPattern::Config{},o.steering);   // before the host: the keyboard fallback needs it (romi 25245)
        IDirectInput8W* input=nullptr;
        if(FAILED(DirectInput8Create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,reinterpret_cast<void**>(&input),nullptr))||!input){
            std::printf("input shim: DirectInput8Create failed; ordinary play has no shim input\n");std::fflush(stdout);
            o.rec.fail("DirectInput8Create failed: the shared input shim did not start");o.rec.publish_status();return;}
        // The wheel output waits with Input = Shim: the steering wheel is read through the WheelFfb bridge, which hands
        // it to WheelFfb for the force and back (INPUT-SHIM.md section 6, one owner at a time).
        const WheelFfbApi* ffb=o.steer?ffb_shim_wheel_api():nullptr;
        if(ffb&&!sh::WheelFfbBridge::usable(*ffb))ffb=nullptr;
        std::function<std::shared_ptr<sh::Provider>(const std::shared_ptr<sh::DInputProvider>&)> front;
        if(ffb){
            const std::string dev=o.steer->dev;
            front=[ffb,dev](const std::shared_ptr<sh::DInputProvider>& inner)->std::shared_ptr<sh::Provider>{
                o.bridge=std::make_shared<sh::WheelFfbBridge>(inner,ffb,dev);return o.bridge;};
        }
        try{
            o.host=std::make_unique<h::ProviderHost<OrdinaryRecorder>>(std::filesystem::u8path(o.folder).wstring(),"ordinary-desktop",input,o.profile,false,
                std::make_unique<OrdinaryRecorder>(o.rec),outrun_capability(),static_cast<HWND>(xbox_runtime::native_window().hwnd),100'000,front);
        }catch(const std::exception& e){
            o.bridge.reset();std::printf("input shim: host refused: %s\n",e.what());std::fflush(stdout);
            o.rec.fail(std::string("the shared input shim did not start: ")+e.what());o.rec.publish_status();return;}
        if(o.bridge){
            o.owner.b=o.bridge.get();
            ffb_set_shim_owner(&o.owner);
            std::printf("input shim: the steering wheel %s is handed to WheelFfb while the wheel output holds it (WheelFfb bridge)\n",
                o.steer->dev.c_str());
        }
        (void)o.host->tick();
        if(o.rec.armed())o.rec.set_devices(o.host->device_reports());   // before the first row (the capture's device slots)
        initial_report(*o.host,o.reports);
        std::printf("input shim: ordinary play on the shim backend ([Input] Backend = Shim); keyboard and pad keep their native paths; "
            "steering sensitivity %g, dead zone %g ([Wheel])\n",double(o.steering.sensitivity),double(o.steering.dead_zone));
        std::fflush(stdout);
    }
    using outrun::desktop::desktop_key;
    Keyboard kb;kb.any=outrun::desktop::desktop_any_key();
    kb.enter=desktop_key(0x1c);kb.esc=desktop_key(0x01);kb.up=desktop_key(0xc8);kb.down=desktop_key(0xd0);kb.left=desktop_key(0xcb);kb.right=desktop_key(0xcd);
    // No host (DirectInput or the host refused): the keyboard keeps its path anyway. An empty frame (every action
    // unbound) through the same adapter: the PC layer on, the menu keys, and the race on the PC layer's record.
    static const sh::Frame no_shim{};
    // The previous tick's row never reached the consumed-input seam: not a row, and the capture cannot complete.
    if(o.pending){o.rec.count_missed_final();o.pending.reset();}
    if(o.host)resolved_report(*o.host,o.reports);
    const sh::Frame& frame=o.host?o.host->tick():no_shim;
    o.frame=o.host?&frame:nullptr;
    const auto v=view(runtime);
    o.adapter->apply(frame,v,out,&kb);
    if(o.rec.armed()&&o.host){
        ++o.ticks;
        // The stops before this tick's row: F11 (its press), the request-bound stop file (every 30 ticks).
        const bool f11=desktop_key(0x57);
        if(f11&&!o.f11)stop_recording("f11");
        o.f11=f11;
        if(o.ticks%30==0&&o.rec.recording()&&stop_requested())stop_recording("stop-request");
        if(o.rec.recording()){
            Snapshot s{frame,o.adapter->signals(),out,v,NotReplay};
            s.keyboard={kb.enter,kb.esc,kb.up,kb.down,kb.left,kb.right,kb.any,out.pc_overlay_yield};
            o.pending=std::move(s);
        }
        if(o.ticks%30==0&&o.rec.state()==OrdinaryRecorder::State::Complete&&!o.rec.saved()&&o.rec.observe_receipt()){
            std::printf("input shim: recording saved (the managed receipt names this request, process and artifact)\n");std::fflush(stdout);}
        o.rec.publish_status();
    }
    // The record is the wheel's (the adapter's); with "keyboard 1" the race reads the PC layer's (the keyboard's) instead.
    if(o.debug)log_signals(o.adapter->signals(),"ordinary",o.log,std::string(" keyboard ")+(out.pc_overlay_yield?"1":"0")+" layer "+(out.pc_input_layer?"1":"0")+
        " overlay "+(out.pc_overlay?"1":"0")+(o.host?"":" no-host"));
}

void shim_input_shutdown_ordinary(){
    if(!o.host||o.stopped)return;
    o.stopped=true;
    // The game loop ended: a row still waiting for the seam is not one (and the capture cannot complete).
    if(o.pending){o.rec.count_missed_final();o.pending.reset();}
    (void)o.host->tick();                      // a final poll before the stop (host order); Core is still ordinary live here
    o.frame=nullptr;
    if(o.rec.armed()){
        // Recording: the stop and its one drain (the saver), waited for here so the host's stop reads the cached result.
        stop_recording("exit");
        const bool done=o.rec.wait_saved(h::Clock::now()+std::chrono::seconds(65));
        if(done&&o.rec.state()==OrdinaryRecorder::State::Complete&&!o.rec.saved())(void)o.rec.observe_receipt();
        o.rec.publish_status();
        const auto why=o.rec.why();
        std::printf("input shim: recording %s after %llu rows%s%s\n",!done?"still saving at the exit":o.rec.saved()?"saved":
            o.rec.state()==OrdinaryRecorder::State::Complete?"complete (not verified)":"FAILED",(unsigned long long)o.rec.pushed(),
            why.empty()?"":": ",why.c_str());
        std::fflush(stdout);
    }
    if(o.bridge)o.bridge->shutdown();          // after the last tick: no read slot left, nothing opened through WheelFfb again
    const auto report=o.host->stop();
    std::printf("input shim: ordinary host stop %s\n",report.resolved?"resolved":"UNRESOLVED");std::fflush(stdout);
    platform::native_input_consumed_hook()=nullptr;
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
