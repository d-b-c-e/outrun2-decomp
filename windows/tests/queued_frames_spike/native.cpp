// P20 composition proof. Real Core/Adapter, fake devices/game, the reviewed worker and the real session Writer. The
// composition is production code now (windows/host/recorder/capture.hpp, windows/source/outrun_capture.hpp; Astra
// 16553); this fixture keeps its scripted capture, the writer barrier, the comparators and the replay.
#include "frame_codec.hpp"
#include "outrun_capture.hpp"
#include <functional>
namespace rs=dbce::shim::recorder;
using rs::schema;using rs::row_bound;using rs::preflight;using rs::write_new;using rs::report;
using namespace std::chrono_literals;
constexpr const char* Contract="dbce.shim.queued-frames-spike@2\n";
constexpr const char* InputContract="shim-frame-audit-2-pending-unadopted";
std::string fixture_profile(){return profile({Steer,Throttle,Brake,sb("shiftUp",11),wb("camera",5),wb("navLeft",7)});}
void same_view(const GameView& a,const GameView& b){
    auto fields=[](const GameView& v){return std::tie(v.mode,v.car,v.gear,v.speed,v.consumed_steer,v.consumed_accel,v.consumed_brake);};
    check(fields(a)==fields(b),"typed adapter input GameView round trip");
}
void same_frame(const Frame& a,const Frame& b){
    auto head=[](const Frame& f){return std::tie(f.seq,f.tick_us,f.boundary_us,f.event_seq,f.run,f.latched,f.suppress_native,f.down,f.up);};
    check(head(a)==head(b)&&a.samples.size()==b.samples.size(),"typed frame header round trip");
    for(int i=0;i<ActionCount;++i){const auto& x=a.action[i];const auto& y=b.action[i];
        check(std::tie(x.value,x.status,x.sample,x.injected)==std::tie(y.value,y.status,y.sample,y.injected),"typed action round trip");}
    for(std::size_t i=0;i<a.samples.size();++i){const auto& x=a.samples[i];const auto& y=b.samples[i];
        auto fields=[](const DeviceSample& s){return std::tie(s.device,s.epoch,s.seq,s.read_start_us,s.read_end_us,s.poll,s.state,s.valid,s.stale,
            s.injected.batch,s.injected.axes,s.injected.povs,s.injected.buttons);};
        auto raw_same=[](const RawState& l,const RawState& r){return
            std::equal(std::begin(l.axes),std::end(l.axes),std::begin(r.axes))&&
            std::equal(std::begin(l.buttons),std::end(l.buttons),std::begin(r.buttons))&&
            std::equal(std::begin(l.povs),std::end(l.povs),std::begin(r.povs));};
        check(fields(x)==fields(y)&&raw_same(x.physical,y.physical)&&raw_same(x.delivered,y.delivered)&&
            std::equal(std::begin(x.injected.hash),std::end(x.injected.hash),std::begin(y.injected.hash)),"typed device sample round trip");
    }
}
std::vector<Snapshot> capture(const std::function<void(const Snapshot&)>& emit={}){
    Rig r(false,fixture_profile(),true);r.view.mode=16;r.view.car=true;r.view.gear=2;
    std::vector<Snapshot> rows;
    for(int t=0;t<16;++t){
        auto& w=r.dev(r.w);auto& s=r.dev(r.s);
        w.state.axes[0]=t<2?32768:t<8?65535:0;w.state.axes[2]=t==0?0:t<7?32768:0;
        w.state.axes[5]=t==5?65535:0;w.state.buttons[5]=(t==3||t==4)?0x80:0;
        w.state.buttons[7]=(t==11||t==12)?0x80:0;w.state.buttons[127]=std::uint8_t(t+1);
        s.state.buttons[11]=(t==2||t==3||t==5||t==7||t==9||t==11)?0x80:0;
        if(t==4)s.script.push_back({Call::Pending,Call::NotCalled});
        if(t==6)s.script.push_back({Call::Ok,Call::Pending});
        if(t==8)s.script.push_back({Call::Gone,Call::NotCalled});
        r.view.mode=t>=11&&t<=12?3:16;
        r.view.car=t!=13;
        r.view.speed=float(t*2);r.view.consumed_steer=t-8;r.view.consumed_accel=t*10;r.view.consumed_brake=t==6?255:0;
        auto out=r.tick();rows.push_back({*r.frame,r.adapter->signals(),out,r.view});
        if(emit)emit(rows.back()); // Caller must copy: the next tick changes both Core and Adapter storage.
    }
    check(rows[4].frame.samples[1].poll==Call::Pending,"record Poll Pending");
    check(rows[6].frame.samples[1].state==Call::Pending,"record State Pending");
    check(rows[4].frame.action[action_index("shiftUp")].status==Status::ReadPending,"Pending stays unavailable");
    check(rows[5].frame.action[action_index("shiftUp")].status==Status::Rearming,"held shift rearms after Pending");
    check(rows[8].frame.action[action_index("shiftUp")].status==Status::ReadFailed,"record failed read");
    check(rows[9].frame.action[action_index("shiftUp")].status==Status::Rearming,"held shift rearms after reopen");
    return rows;
}
// The fixture's writer barrier: the worker is held on its first row while the producer finishes every tick.
struct Barrier {std::atomic<bool> entered{false},resume{false};void hold(){entered=true;while(!resume)std::this_thread::yield();}};
void wait_entered(const std::shared_ptr<Barrier>& b){
    const auto until=std::chrono::steady_clock::now()+2s;
    while(!b->entered){if(std::chrono::steady_clock::now()>until)throw std::runtime_error("worker barrier");std::this_thread::yield();}
}
struct ResumeOnExit {std::shared_ptr<Barrier> b;~ResumeOnExit(){b->resume=true;}};
dbce::session::Metadata fixture_metadata(){   // the sink adds captureNonce
    dbce::session::Metadata m{"outrun-queued-frames-fixture","offline-only","native-session-1",{}, {}};
    m.properties={{"inputContract",InputContract},{"profileSha256",hex(sha256(fixture_profile()))},
        {"context","known two-device fixture only; no epoch/caps journal or portable replay"},
        {"adapterConsumedOffset","previous tick; adapter record fields are this tick"}};
    return m;
}
void record(const fs::path& root,bool window){
    check(fs::create_directory(root),"new capture directory");
    const auto expected=capture();const auto names=schema(channels(expected[0]));preflight(names,62.5,.24);
    check(names.size()==382,"full two-device schema plus every adapter signal and input");
    bool short_budget=false;try{preflight(names,60,600);}catch(const std::invalid_argument&){short_budget=true;}
    check(short_budget,"600 seconds at 60Hz refused before any writer under default budget");
    bool too_long=false;try{preflight(names,0.01,601);}catch(const std::invalid_argument&){too_long=true;}
    check(too_long,"low-rate duration over writer limit refused before opening");
    for(const auto rate:{0.0,-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        bool refused=false;try{preflight(names,rate,1);}catch(const std::invalid_argument&){refused=true;}check(refused,"invalid rate refused");}
    check(4*1024*1024/sizeof(rs::Packet)==510,"fixed-slot production capacity obeys both upper bounds");
    auto wide=expected[0];while(wide.frame.samples.size()<8)wide.frame.samples.push_back(wide.frame.samples[0]);
    bool too_wide=false;try{schema(channels(wide));}catch(const std::invalid_argument&){too_wide=true;}
    check(too_wide,"adapter signals count toward complete channel preflight");
    const std::size_t start=window?6:0;const auto first=expected[start].frame.seq;const auto origin=expected[start].frame.tick_us;
    auto id=std::make_shared<rs::Identity>();auto barrier=std::make_shared<Barrier>();ResumeOnExit release{barrier};
    rs::Controller c(16,std::uint32_t(names.size()),16*sizeof(rs::Packet),
        std::make_unique<rs::QueuedSink>(root/"source.jsonl",names,id,origin,fixture_metadata(),dbce::session::Limits{},[barrier]{barrier->hold();}),first);
    auto producer=c.producer();std::size_t ordinal=0;
    const auto actual=capture([&](const Snapshot& s){
        if(ordinal++<start)return;
        auto p=packet(s,names);check(producer.push(p),"actual Core/Adapter snapshot queued");
        if(s.frame.seq==first)wait_entered(barrier);
        // Overwrite producer storage while the worker is held: the owned queued copy must survive.
        p.values.fill(-999);
    });
    check(!barrier->resume&&producer.report().accepted==actual.size()-start,"producer completes all ticks while writer is blocked");
    producer.finish(actual.back().frame.seq);barrier->resume=true;const auto r=c.drain(2s);
    check(r.worker_done&&id->owner_calls==0,"all native writer I/O off the input producer");
    report(root,r,*id,first,origin);
    auto pushed=dbce::session::jsonStream();
    for(std::size_t i=start;i<actual.size();++i){const auto row=channels(actual[i]);pushed<<"ROW "<<row.size()<<'\n';for(const auto& v:row)pushed<<v.first<<' '<<v.second<<'\n';}
    write_new(root.parent_path()/(root.filename().string()+".producer.tsv"),pushed.str());
    write_new(root/"profile.ini",fixture_profile());write_new(root/"bundle.meta",Contract);
    auto b=dbce::session::jsonStream();b<<names.size()<<'\t'<<row_bound(names)<<'\t'<<sizeof(rs::Packet)*16<<'\n';write_new(root/"budget.tsv",b.str());
    std::cout<<"Recorded "<<r.written<<" actual frames; first="<<first<<", channels="<<names.size()<<", owned ring="<<r.ring_bytes<<" bytes\n";
}
std::vector<Channels> read_export(const fs::path& path){
    std::ifstream in(path);in.imbue(std::locale::classic());check(bool(in),"validated managed export exists");std::vector<Channels> result;std::string tag;
    while(in>>tag){check(tag=="ROW","row delimiter");std::size_t n;in>>n;check(n<=1024,"bounded export row");Channels c;
        for(std::size_t k=0;k<n;++k){std::string key;double v;check(bool(in>>key>>v),"numeric channel");check(c.emplace(key,v).second,"no duplicate channel");}result.push_back(std::move(c));}return result;
}
void replay(const fs::path& root,bool window){
    const auto expected=capture();const auto rows=read_export(root/"frames.tsv");
    const auto pushed=read_export(root.parent_path()/(root.filename().string()+".producer.tsv"));const std::size_t start=window?6:0;
    check(rows.size()==expected.size()-start,"all queued samples returned");
    check(rows==pushed,"export matches the original producer snapshots, not only regenerated fakes");
    Rig r(false,fixture_profile(),true);Adapter adapter("Sequential");
    // Window mode only proves sequence/count framing, not mid-session resolver or adapter restoration.
    for(std::size_t k=0;k<rows.size();++k){
        const auto& want=expected[k+start];check(rows[k]==channels(want),"every original frame and adapter channel preserved");
        const Frame f=decode(frame_channels(rows[k]));same_frame(f,want.frame);same_signals(decoded_signals(rows[k]),want.signals);
        const GameView view=decoded_view(rows[k]);same_view(view,want.view);
        if(window)continue;
        r.view=view;
        for(const auto& s:f.samples){auto& d=r.dev(s.device);d.state=s.physical;d.script.push_back({s.poll,s.state});}
        const auto out=r.tick(f.tick_us-r.p.clock);check(encode(*r.frame)==encode(f),"raw replay uses real resolver and preserves Pending/rearm/epoch");
        same_sink(out,want.sink);same_signals(r.adapter->signals(),want.signals);
        NativeInputState action_out;adapter.apply(f,view,action_out);same_sink(action_out,want.sink);same_signals(adapter.signals(),want.signals);
    }
    auto corrupt=frame_channels(rows[0]);corrupt["sample.0.poll"]=5;bool bad=false;try{decode(corrupt);}catch(const std::exception&){bad=true;}check(bad,"future Call enum refused");
    if(!window){
        for(bool wrong_shift:{false,true}){
            auto prof=profile({wrong_shift?Steer:Steer+" inverted",Throttle,Brake,sb("shiftUp",wrong_shift?10:11),wb("camera",5),wb("navLeft",7)});
            Rig wrong(false,prof,true);std::vector<std::size_t> mismatches;
            for(std::size_t k=0;k<rows.size();++k){const auto f=decode(frame_channels(rows[k]));wrong.view=decoded_view(rows[k]);
                for(const auto& s:f.samples){auto& d=wrong.dev(s.device);d.state=s.physical;d.script.push_back({s.poll,s.state});}
                wrong.tick(f.tick_us-wrong.p.clock);const auto a=action_index(wrong_shift?"shiftUp":"steer");
                if(wrong.frame->action[a].value!=f.action[a].value)mismatches.push_back(k);}
            const std::vector<std::size_t> wanted=wrong_shift?std::vector<std::size_t>{2,3,11}:std::vector<std::size_t>{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
            check(mismatches==wanted,"wrong applied profile changes exactly the expected frames");std::cout<<"Wrong "<<(wrong_shift?"shift":"steering")<<" profile detected on "<<mismatches.size()<<" frames\n";
        }
    }
    std::cout<<"PASS "<<(window?"non-one source sequence window only":"queued raw/action Core and OutRun adapter replay")<<": "<<rows.size()<<" frames, "<<checks<<" checks\n";
}
int main(int argc,char** argv){try{
    check(argc==4,"usage: queued_frames_spike record|replay directory full|window");
    const bool window=std::string(argv[3])=="window";check(window||std::string(argv[3])=="full","known mode");
    if(std::string(argv[1])=="record")record(argv[2],window);else if(std::string(argv[1])=="replay")replay(argv[2],window);else throw std::runtime_error("unknown operation");
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
