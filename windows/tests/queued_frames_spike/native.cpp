// P20 composition proof only. Real Core/Adapter, fake devices/game, reviewed worker and real session Writer.
#include "frame_codec.hpp"
#include "worker.hpp"
#include <bcrypt.h>
#include <functional>
namespace rs=recorder_spike;
using namespace std::chrono_literals;
constexpr const char* Contract="dbce.shim.queued-frames-spike@2\n";
constexpr const char* InputContract="shim-frame-audit-2-pending-unadopted";
std::string fixture_profile(){return profile({Steer,Throttle,Brake,sb("shiftUp",11),wb("camera",5),wb("navLeft",7)});}
struct Snapshot {Frame frame;Signals signals;NativeInputState sink;GameView view;};
Channels signal_channels(const Signals& s){
    Channels c;u64(c,"adapter.frame",s.frame_seq);
    c["adapter.mode"]=s.mode;c["adapter.inGame"]=s.in_gameplay;c["adapter.speed"]=s.speed;c["adapter.gear"]=s.gear;
    c["adapter.consumedSteer"]=s.consumed_steer;c["adapter.consumedAccel"]=s.consumed_accel;c["adapter.consumedBrake"]=s.consumed_brake;
    c["adapter.recordSteer"]=s.record_steer;c["adapter.recordAccel"]=s.record_accel;c["adapter.recordBrake"]=s.record_brake;
    c["adapter.recordButtons"]=s.record_buttons;c["adapter.nativeSuppressed"]=s.native_suppressed;c["adapter.gearbox"]=int(s.gearbox);
    return c;
}
Channels channels(const Snapshot& s){auto c=encode(s.frame);const auto a=signal_channels(s.signals);c.insert(a.begin(),a.end());
    c["gameInput.mode"]=s.view.mode;c["gameInput.car"]=s.view.car;c["gameInput.gear"]=s.view.gear;c["gameInput.speed"]=s.view.speed;
    c["gameInput.consumedSteer"]=s.view.consumed_steer;c["gameInput.consumedAccel"]=s.view.consumed_accel;c["gameInput.consumedBrake"]=s.view.consumed_brake;return c;}
Channels frame_channels(const Channels& all){Channels c;for(const auto& v:all)if(v.first.rfind("adapter.",0)!=0&&v.first.rfind("gameInput.",0)!=0)c.insert(v);return c;}
GameView decoded_view(const Channels& c){
    GameView v;v.mode=std::uint32_t(number(c,"gameInput.mode",0,UINT32_MAX));v.car=number(c,"gameInput.car",0,1)!=0;
    v.gear=std::uint32_t(number(c,"gameInput.gear",0,UINT32_MAX));v.speed=float(c.at("gameInput.speed"));check(std::isfinite(v.speed),"finite input speed");
    v.consumed_steer=std::int32_t(number(c,"gameInput.consumedSteer",INT32_MIN,INT32_MAX));
    v.consumed_accel=std::int32_t(number(c,"gameInput.consumedAccel",INT32_MIN,INT32_MAX));
    v.consumed_brake=std::int32_t(number(c,"gameInput.consumedBrake",INT32_MIN,INT32_MAX));return v;
}
void same_view(const GameView& a,const GameView& b){
    auto fields=[](const GameView& v){return std::tie(v.mode,v.car,v.gear,v.speed,v.consumed_steer,v.consumed_accel,v.consumed_brake);};
    check(fields(a)==fields(b),"typed adapter input GameView round trip");
}
Signals decoded_signals(const Channels& c){
    Signals s;s.frame_seq=u64(c,"adapter.frame");s.mode=std::uint32_t(number(c,"adapter.mode",0,UINT32_MAX));
    s.in_gameplay=number(c,"adapter.inGame",0,1)!=0;s.gear=std::uint32_t(number(c,"adapter.gear",0,UINT32_MAX));
    s.speed=float(c.at("adapter.speed"));check(std::isfinite(s.speed),"finite adapter speed");
    s.consumed_steer=std::int32_t(number(c,"adapter.consumedSteer",INT32_MIN,INT32_MAX));
    s.consumed_accel=std::int32_t(number(c,"adapter.consumedAccel",INT32_MIN,INT32_MAX));
    s.consumed_brake=std::int32_t(number(c,"adapter.consumedBrake",INT32_MIN,INT32_MAX));
    s.record_steer=std::int32_t(number(c,"adapter.recordSteer",INT32_MIN,INT32_MAX));
    s.record_accel=std::int32_t(number(c,"adapter.recordAccel",INT32_MIN,INT32_MAX));
    s.record_brake=std::int32_t(number(c,"adapter.recordBrake",INT32_MIN,INT32_MAX));
    s.record_buttons=std::uint32_t(number(c,"adapter.recordButtons",0,UINT32_MAX));
    s.native_suppressed=std::uint32_t(number(c,"adapter.nativeSuppressed",0,UINT32_MAX));
    s.gearbox=HPattern::State(number(c,"adapter.gearbox",0,int(HPattern::State::AtTarget)));return s;
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
std::vector<std::string> schema(const Channels& c){
    if(c.empty()||c.size()>rs::MaxValues)throw std::invalid_argument("complete schema exceeds channel budget");
    std::vector<std::string> names;for(const auto& v:c){if(!dbce::session::ascii(v.first,256,true))throw std::invalid_argument("channel name");names.push_back(v.first);}return names;
}
std::size_t row_bound(const std::vector<std::string>& names){
    // Writer uses precision 17 and defaultfloat: finite binary64 needs fewer than 32 ASCII bytes.
    // 192 covers the fixed JSON syntax, uint64 sequence, elapsed binary64 and newline.
    std::size_t bytes=192;for(const auto& name:names)bytes+=dbce::session::quote(name).size()+34;return bytes;
}
void preflight(const std::vector<std::string>& names,double hz,double seconds,dbce::session::Limits limits={}){
    if(names.empty()||names.size()>1024||!std::isfinite(hz)||hz<=0||!std::isfinite(seconds)||seconds<=0||
       !std::isfinite(limits.seconds)||limits.seconds<=0||limits.seconds>86400||seconds>limits.seconds||
       limits.bytes<16384||limits.bytes>1024ull*1024*1024||limits.recordBytes<1024||limits.recordBytes>1024*1024)
        throw std::invalid_argument("schema/rate/duration/limits bound");
    const long double frame_count=std::ceil(static_cast<long double>(hz)*seconds)+1;
    if(frame_count>=static_cast<long double>(UINT64_MAX))throw std::invalid_argument("frame bound overflow");
    const auto frames=static_cast<std::uint64_t>(frame_count);
    const auto row=row_bound(names);
    // Fixture metadata is separately bounded below 8192, plus writer's 8192 footer reserve.
    if(row>limits.recordBytes||limits.bytes<16384||frames>(limits.bytes-16384)/row)
        throw std::invalid_argument("requested capture exceeds worst-case file budget");
}
rs::Packet packet(const Snapshot& s,const std::vector<std::string>& names){
    auto c=channels(s);check(c.size()==names.size(),"one complete fixed schema per capture");
    rs::Packet p;p.sequence=s.frame.seq;p.source_us=s.frame.tick_us;p.width=std::uint32_t(names.size());
    for(std::size_t i=0;i<names.size();++i)p.values[i]=c.at(names[i]);return p;
}
struct Identity {
    std::string nonce,sha;std::uint64_t bytes{};std::atomic<bool> entered{false},resume{false};
    const std::thread::id producer=std::this_thread::get_id();std::atomic<unsigned> owner_calls{0};
    Identity(){std::array<std::uint8_t,16> n{};if(BCryptGenRandom(nullptr,n.data(),ULONG(n.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)throw std::runtime_error("nonce failed");nonce=hex(n.data(),n.size());}
    void worker(){if(std::this_thread::get_id()==producer){++owner_calls;throw std::runtime_error("writer called on producer");}}
    void closed(const fs::path& p){
        worker();struct File{HANDLE h;~File(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}} f{
            CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
        LARGE_INTEGER n{};
        if(f.h==INVALID_HANDLE_VALUE||!GetFileSizeEx(f.h,&n)||n.QuadPart<=0||n.QuadPart>64*1024*1024)throw std::runtime_error("closed source size");
        std::vector<std::uint8_t> b(std::size_t(n.QuadPart));DWORD got{};
        if(!ReadFile(f.h,b.data(),DWORD(b.size()),&got,nullptr)||got!=b.size())throw std::runtime_error("closed source read");
        sha=hex(sha256(b.data(),b.size()));bytes=b.size();
    }
};
struct QueuedSink final:rs::Sink {
    dbce::session::Writer writer;fs::path path;std::vector<std::string> names;std::shared_ptr<Identity> id;std::int64_t origin;
    QueuedSink(fs::path p,std::vector<std::string> n,std::shared_ptr<Identity> i,std::int64_t o):path(std::move(p)),names(std::move(n)),id(std::move(i)),origin(o){}
    bool open()override{
        id->worker();dbce::session::Metadata m{"outrun-queued-frames-fixture","offline-only","native-session-1",{}, {}};
        m.properties={{"captureNonce",id->nonce},{"inputContract",InputContract},{"profileSha256",hex(sha256(fixture_profile()))},
            {"context","known two-device fixture only; no epoch/caps journal or portable replay"},
            {"adapterConsumedOffset","previous tick; adapter record fields are this tick"}};
        return writer.open(path.wstring(),m);
    }
    bool write(const rs::Packet& p)override{
        id->worker();id->entered=true;while(!id->resume)std::this_thread::yield();
        Channels c;for(std::size_t i=0;i<names.size();++i)c[names[i]]=p.values[i];
        return writer.sample(double(p.source_us-origin)/1e6,c);
    }
    bool stop()override{id->worker();if(!writer.stop())return false;id->closed(path);return true;}
    bool stopped_at_limit()const override{return !writer.active()&&(writer.reason()=="durationLimit"||writer.reason()=="fileSizeLimit"||writer.reason()=="recordSizeLimit");}
    void abandon()noexcept override{writer.abandon();}
    std::string reason()const override{return writer.reason();}
};
void wait_entered(const std::shared_ptr<Identity>& id){
    const auto until=std::chrono::steady_clock::now()+2s;
    while(!id->entered){if(std::chrono::steady_clock::now()>until)throw std::runtime_error("worker barrier");std::this_thread::yield();}
}
struct ResumeOnExit {std::shared_ptr<Identity> id;~ResumeOnExit(){id->resume=true;}};
void write_new(const fs::path& path,const std::string& text){check(!fs::exists(path),"new output required");std::ofstream f(path,std::ios::binary);f<<text;f.close();check(bool(f),"output write");}
void report(const fs::path& root,const rs::Report& r,const Identity& id,std::uint64_t first,std::int64_t origin){
    check(r.may_seal()&&r.capture_counts_final&&!r.first_rejected&&r.wrong_producer==0,"eligible actual worker result");
    auto s=dbce::session::jsonStream();
    s<<int(r.outcome)<<'\t'<<r.accepted<<'\t'<<r.written<<'\t'<<r.attempted<<'\t'<<r.declared_last<<'\t'<<r.last_written
     <<'\t'<<first<<'\t'<<origin<<'\t'<<id.nonce<<'\t'<<id.bytes<<'\t'<<id.sha<<'\t'<<r.producer_done<<'\t'<<r.worker_done
     <<'\t'<<r.drain_timed_out<<'\t'<<r.queue_full<<'\t'<<r.invalid<<'\t'<<r.after_failure<<'\t'<<r.wrong_producer
     <<'\t'<<r.after_end<<'\t'<<r.after_end_wrong_producer<<'\t'<<r.discarded<<'\t'<<r.first_rejected<<'\t'<<r.sink_reason<<'\n';
    write_new(root/"worker.tsv",s.str());
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
    auto id=std::make_shared<Identity>();ResumeOnExit release{id};
    rs::Controller c(16,std::uint32_t(names.size()),16*sizeof(rs::Packet),std::make_unique<QueuedSink>(root/"source.jsonl",names,id,origin),first);
    auto producer=c.producer();std::size_t ordinal=0;
    const auto actual=capture([&](const Snapshot& s){
        if(ordinal++<start)return;
        auto p=packet(s,names);check(producer.push(p),"actual Core/Adapter snapshot queued");
        if(s.frame.seq==first)wait_entered(id);
        // Overwrite producer storage while the worker is held: the owned queued copy must survive.
        p.values.fill(-999);
    });
    check(!id->resume&&producer.report().accepted==actual.size()-start,"producer completes all ticks while writer is blocked");
    producer.finish(actual.back().frame.seq);id->resume=true;const auto r=c.drain(2s);
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
