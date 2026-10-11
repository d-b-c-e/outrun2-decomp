// Executable design spike only. No runtime hooks, device API, or production recorder.
#include "session_writer.h"
#define main upstream_adapter_tests_main
#include "../test_outrun_adapter.cpp"
#undef main
#include <filesystem>
#include <fstream>
#include <limits>
namespace fs=std::filesystem;
using Channels=dbce::session::Channels;

// Numeric v1 channels are doubles. Split identifiers/timestamps rather than silently rounding uint64.
void u64(Channels& c,const std::string& key,std::uint64_t n){
    c[key+".lo"]=std::uint32_t(n);c[key+".hi"]=std::uint32_t(n>>32);
}
std::int64_t number(const Channels& c,const std::string& key,std::int64_t low,std::int64_t high){
    const auto n=c.at(key);
    if(!std::isfinite(n)||n!=std::floor(n)||n<double(low)||n>double(high))
        throw std::runtime_error("invalid integer: "+key);
    return std::int64_t(n);
}
std::uint64_t u64(const Channels& c,const std::string& key){
    return std::uint64_t(number(c,key+".lo",0,UINT32_MAX))|
        (std::uint64_t(number(c,key+".hi",0,UINT32_MAX))<<32);
}
void raw(Channels& c,const std::string& key,const RawState& r){
    for(int k=0;k<8;++k)c[key+".axis"+std::to_string(k)]=r.axes[k];
    for(int k=0;k<4;++k)c[key+".pov"+std::to_string(k)]=r.povs[k];
    // Preserve all 128 bytes, including low bits, in 32 exact unsigned 32-bit words.
    for(int k=0;k<32;++k){
        std::uint32_t word=0;
        for(int b=0;b<4;++b)word|=std::uint32_t(r.buttons[4*k+b])<<(8*b);
        c[key+".buttons"+std::to_string(k)]=word;
    }
}
RawState raw(const Channels& c,const std::string& key){
    RawState r;
    for(int k=0;k<8;++k)r.axes[k]=std::int32_t(number(c,key+".axis"+std::to_string(k),INT32_MIN,INT32_MAX));
    for(int k=0;k<4;++k)r.povs[k]=std::uint32_t(number(c,key+".pov"+std::to_string(k),0,UINT32_MAX));
    for(int k=0;k<32;++k){
        const auto word=std::uint32_t(number(c,key+".buttons"+std::to_string(k),0,UINT32_MAX));
        for(int b=0;b<4;++b)r.buttons[4*k+b]=std::uint8_t(word>>(8*b));
    }
    return r;
}
void bits(Channels& c,const std::string& key,const std::bitset<128>& bs){
    for(int k=0;k<4;++k){std::uint32_t w=0;for(int b=0;b<32;++b)if(bs[32*k+b])w|=1u<<b;
        c[key+std::to_string(k)]=w;}
}
std::bitset<128> bits(const Channels& c,const std::string& key){
    std::bitset<128> bs;
    for(int k=0;k<4;++k){auto w=std::uint32_t(number(c,key+std::to_string(k),0,UINT32_MAX));
        for(int b=0;b<32;++b)bs[32*k+b]=(w&(1u<<b))!=0;}
    return bs;
}
Channels encode(const Frame& f){
    Channels c;
    u64(c,"frame",f.seq);u64(c,"tickUs",std::uint64_t(f.tick_us));
    u64(c,"boundaryUs",std::uint64_t(f.boundary_us));u64(c,"eventSeq",f.event_seq);
    u64(c,"down",f.down);u64(c,"up",f.up);
    c["run"]=int(f.run);c["latched"]=f.latched;c["suppressNative"]=f.suppress_native;
    c["sampleCount"]=double(f.samples.size());
    for(int k=0;k<ActionCount;++k){
        const auto p="action."+std::string(action_name(k))+".";const auto& a=f.action[k];
        c[p+"value"]=a.value;c[p+"status"]=int(a.status);c[p+"sample"]=a.sample;c[p+"injected"]=a.injected;
    }
    for(std::size_t k=0;k<f.samples.size();++k){
        const auto p="sample."+std::to_string(k)+".";const auto& s=f.samples[k];
        c[p+"device"]=s.device;c[p+"epoch"]=s.epoch;u64(c,p+"seq",s.seq);
        u64(c,p+"readStartUs",std::uint64_t(s.read_start_us));u64(c,p+"readEndUs",std::uint64_t(s.read_end_us));
        c[p+"poll"]=int(s.poll);c[p+"state"]=int(s.state);c[p+"valid"]=s.valid;c[p+"stale"]=s.stale;
        raw(c,p+"physical",s.physical);raw(c,p+"delivered",s.delivered);
        u64(c,p+"batch",s.injected.batch);c[p+"axes"]=s.injected.axes;c[p+"povs"]=s.injected.povs;
        bits(c,p+"buttonMask",s.injected.buttons);
        for(int word=0;word<2;++word){std::uint32_t v=0;
            for(int b=0;b<4;++b)v|=std::uint32_t(s.injected.hash[word*4+b])<<(8*b);
            c[p+"hash"+std::to_string(word)]=v;}
    }
    return c;
}
Frame decode(const Channels& c){
    Frame f;
    f.seq=u64(c,"frame");f.tick_us=std::int64_t(u64(c,"tickUs"));f.boundary_us=std::int64_t(u64(c,"boundaryUs"));
    if(f.tick_us<0||f.boundary_us<f.tick_us)throw std::runtime_error("invalid frame times");
    f.event_seq=u64(c,"eventSeq");f.down=u64(c,"down");f.up=u64(c,"up");
    f.run=Run(number(c,"run",0,int(Run::Inert)));f.latched=number(c,"latched",0,1)!=0;
    f.suppress_native=number(c,"suppressNative",0,1)!=0;
    f.samples.resize(std::size_t(number(c,"sampleCount",0,31)));
    for(int k=0;k<ActionCount;++k){
        const auto p="action."+std::string(action_name(k))+".";auto& a=f.action[k];
        const double v=c.at(p+"value");
        if(!std::isfinite(v)||v < (k==action_index("steer")?-1:0)||v>1)throw std::runtime_error("invalid action value");
        a.value=float(v);a.status=Status(number(c,p+"status",0,int(Status::TestNeutral)));
        a.sample=std::int16_t(number(c,p+"sample",-1,std::int64_t(f.samples.size())-1));
        a.injected=number(c,p+"injected",0,1)!=0;
    }
    for(std::size_t k=0;k<f.samples.size();++k){
        const auto p="sample."+std::to_string(k)+".";auto& s=f.samples[k];
        s.device=std::uint32_t(number(c,p+"device",0,30));s.epoch=std::uint32_t(number(c,p+"epoch",0,UINT32_MAX));
        s.seq=u64(c,p+"seq");s.read_start_us=std::int64_t(u64(c,p+"readStartUs"));s.read_end_us=std::int64_t(u64(c,p+"readEndUs"));
        s.poll=Call(number(c,p+"poll",0,int(Call::NotCalled)));s.state=Call(number(c,p+"state",0,int(Call::NotCalled)));
        s.valid=number(c,p+"valid",0,1)!=0;s.stale=number(c,p+"stale",0,1)!=0;
        s.physical=raw(c,p+"physical");s.delivered=raw(c,p+"delivered");
        s.injected.batch=u64(c,p+"batch");s.injected.axes=std::uint8_t(number(c,p+"axes",0,255));
        s.injected.povs=std::uint8_t(number(c,p+"povs",0,15));s.injected.buttons=bits(c,p+"buttonMask");
        for(int word=0;word<2;++word){const auto v=std::uint32_t(number(c,p+"hash"+std::to_string(word),0,UINT32_MAX));
            for(int b=0;b<4;++b)s.injected.hash[word*4+b]=std::uint8_t(v>>(8*b));}
    }
    // Reject extensions or missing fields rather than silently omitting provenance.
    if(encode(f)!=c)throw std::runtime_error("noncanonical/missing/unknown frame channels");
    return f;
}

std::string fixture_profile(){return profile({Steer,Throttle,Brake,sb("shiftUp",11),wb("camera",5),wb("navLeft",7)});}
std::vector<Frame> capture(){
    Rig r(false,fixture_profile(),true);r.view.mode=16;r.view.car=true;r.view.gear=2;
    std::vector<Frame> result;
    for(int t=0;t<12;++t){
        auto& wheel=r.dev(r.w);auto& shifter=r.dev(r.s);
        wheel.state.axes[0]=t<3?32768:t<7?65535:0;
        wheel.state.axes[2]=t==0?0:t<5?32768:0;
        wheel.state.axes[5]=t==5?65535:0;
        wheel.state.buttons[5]=(t==3||t==4)?0x80:0;
        // A low bit in an unbound button is preserved, even though it is not a press.
        wheel.state.buttons[127]=std::uint8_t(1+t);
        shifter.state.buttons[11]=(t==2||t==3||t>=7)?0x80:0;
        if(t==6)shifter.script.push_back({Call::Gone,Call::NotCalled});
        r.tick();result.push_back(*r.frame); // deep copy: next tick mutates the core's frame
    }
    check(result.front().samples.size()==2,"two device samples");
    check(result[3].action[action_index("camera")].value==1,"captured camera press");
    check(result[6].action[action_index("shiftUp")].status==Status::ReadFailed,"capture includes failed shifter read");
    check(result[7].action[action_index("shiftUp")].status==Status::Rearming,"held shift waits for release after reopen");
    return result;
}
void same_sink(const NativeInputState& a,const NativeInputState& b){
    check(a.steering==b.steering&&a.accelerator==b.accelerator&&a.brake==b.brake&&
        a.pc_device.axes_94==b.pc_device.axes_94&&a.pc_device.buttons_04==b.pc_device.buttons_04&&
        a.shift_up==b.shift_up&&a.shift_down==b.shift_down&&a.frontend.feature_mask==b.frontend.feature_mask,
        "action replay matches actual adapter sink");
}
void expect_decode_refused(Channels c){bool refused=false;try{decode(c);}catch(const std::exception&){refused=true;}
    check(refused,"semantic corruption refused");}
dbce::session::Metadata metadata(){
    dbce::session::Metadata m{"outrun-2006-decomp","fixture-only-bb15e54","native-session-1",{}, {}};
    m.properties["captureKind"]="synthetic-codec-conformance-not-a-game-recording";
    m.properties["inputContract"]="shim-frame-audit-1-unadopted";
    m.properties["profileBytes"]=fixture_profile();
    m.properties["profileSha256"]=hex(sha256(fixture_profile()));
    m.properties["largeIdDecimal"]="9007199254740993";
    m.properties["deviceCaps"]="fixture wheel_caps only; not a runtime epoch/caps journal";
    return m;
}
void generate(const fs::path& root){
    if(!fs::create_directory(root))throw std::runtime_error("output must be absent");
    const auto frames=capture();const auto m=metadata();
    dbce::session::Writer w;check(w.open((root/"frames.jsonl").wstring(),m),"open native writer");
    for(const auto& f:frames)check(w.sample(double(f.tick_us-frames[0].tick_us)/1e6,encode(f)),"write copied frame");
    check(w.stop(),"stop complete");
    { Rig r(true,fixture_profile(),true);r.view.mode=16;r.admit();r.tick();
      r.batch(UINT64_C(999999999999999),{"inject raw axis 0 dev="+W+" value=65535 ms=1000",
          "inject raw button 11 dev="+S+" value=1 ms=1000"});
      r.tick();check(r.frame->samples[0].injected.any()&&r.frame->samples[1].injected.any(),"two-device injection marks");
      dbce::session::Writer injected;check(injected.open((root/"injected.jsonl").wstring(),m),"injected open");
      check(injected.sample(0,encode(*r.frame))&&injected.stop(),"injected frame write");}
    { dbce::session::Writer ids;check(ids.open((root/"ids.jsonl").wstring(),m),"ids open");
      Channels c;const auto big=UINT64_C(9007199254740993);c["unsafeDouble"]=double(big);u64(c,"safe",big);
      u64(c,"max",UINT64_MAX);c["povNeutral"]=UINT32_MAX;c["signedMin"]=INT32_MIN;
      check(ids.sample(0,c)&&ids.stop(),"ids write");}
    // The writer cannot see an upstream queue drop. This is an intentionally wrong caller, retained as a counterexample.
    { dbce::session::Writer dropped;check(dropped.open((root/"drop-then-stop.jsonl").wstring(),m),"drop open");
      check(dropped.sample(0,encode(frames[0])),"drop first");
      check(dropped.sample(.032,encode(frames[2])),"drop third");check(dropped.stop(),"writer reports complete despite upstream drop");}
    { dbce::session::Writer aborted;check(aborted.open((root/"drop-abandon.jsonl").wstring(),m),"abandon open");
      check(aborted.sample(0,encode(frames[0])),"abandon first");aborted.abandon();}
    { dbce::session::Writer wide;check(wide.open((root/"too-wide.jsonl").wstring(),m),"wide open");
      Frame f=frames[0];while(encode(f).size()<=1024)f.samples.push_back(f.samples[0]);
      std::cout<<"Full-frame limit: "<<f.samples.size()<<" device samples = "<<encode(f).size()<<" channels (refused)\n";
      check(!wide.sample(0,encode(f))&&!wide.completed(),"oversized frame must fail, never truncate");}
    std::cout<<"Native frame fixture: "<<frames.size()<<" copied frames, "<<encode(frames[0]).size()<<" channels each; no devices or game\n";
}
std::vector<Channels> read_export(const fs::path& path){
    std::ifstream in(path);in.imbue(std::locale::classic());check(bool(in),"managed export exists");
    std::vector<Channels> result;std::string tag;
    while(in>>tag){check(tag=="ROW","row delimiter");std::size_t n;in>>n;check(n<=1024,"bounded export row");
        Channels c;for(std::size_t k=0;k<n;++k){std::string key;double v;check(bool(in>>key>>v),"numeric channel");
            check(c.emplace(key,v).second,"no duplicate export channel");}result.push_back(std::move(c));}
    return result;
}
void replay(const fs::path& root){
    const auto expected=capture();const auto rows=read_export(root/"frames.tsv");
    check(rows.size()==expected.size(),"frame count survived managed reader");
    Rig raw_rig(false,fixture_profile(),true);raw_rig.view.mode=16;raw_rig.view.car=true;raw_rig.view.gear=2;
    Adapter a("Sequential"),b("Sequential");GameView view;view.mode=16;view.car=true;view.gear=2;
    for(std::size_t k=0;k<rows.size();++k){
        check(rows[k]==encode(expected[k]),"every original channel survived native/managed round trip");
        const Frame f=decode(rows[k]);
        NativeInputState x,y;a.apply(expected[k],view,x);b.apply(f,view,y);same_sink(x,y);
        for(const auto& s:f.samples){check(s.device<2,"fixture device slot");auto& d=raw_rig.dev(s.device);
            d.state=s.physical;d.script.push_back({s.poll,s.state});}
        raw_rig.tick(f.tick_us-raw_rig.p.clock);
        check(encode(*raw_rig.frame)==rows[k],"raw replay through fresh real Core reproduces actions, epochs, edges and provenance");
    }
    auto corrupt=rows[0];corrupt["action.steer.status"]=99;expect_decode_refused(corrupt);
    corrupt=rows[0];corrupt.erase("sample.0.physical.axis0");expect_decode_refused(corrupt);
    corrupt=rows[0];corrupt["action.steer.sample"]=31;expect_decode_refused(corrupt);
    corrupt=rows[0];corrupt["frame.lo"]=-1;expect_decode_refused(corrupt);
    corrupt=rows[0];corrupt["unknown"]=1;expect_decode_refused(corrupt);
    std::cout<<"PASS: 12 raw and action replay frames through production core/adapter, exact full-channel comparison; 5 semantic corruptions refused\n";
}
int main(int argc,char** argv){try{
    check(argc==3,"usage: spike generate|replay absent-output-directory");
    if(std::string(argv[1])=="generate")generate(fs::path(argv[2]));
    else if(std::string(argv[1])=="replay")replay(fs::path(argv[2]));
    else throw std::runtime_error("unknown operation");
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
