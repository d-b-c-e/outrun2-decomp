// The control journal sink, offline (windows/docs/CONTROL-JOURNAL.md section 8): real files in a fresh folder given
// as argv[1], the production JournalFile/FileIo, Core and Pump with the scripted device provider. No game, device,
// install or force. Writes <root>/cases.json for run.py's strict reader checks.
#include "control_journal.hpp"
#include "shim_fake_provider.hpp"
#include <atomic>
#include <iostream>
#include <set>
namespace c=dbce::shim::control;
using namespace dbce::shim;
namespace {
int checks=0;
void check(bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);}
const std::string W="{11111111-1111-1111-1111-111111111111}",P="{22222222-2222-2222-2222-222222222222}",N="JournalFixture01";
std::wstring root;
std::string utf8(const std::wstring& w){
    if(w.empty())return {};
    std::string s(std::size_t(WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),nullptr,0,nullptr,nullptr)),'\0');
    WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),s.data(),int(s.size()),nullptr,nullptr);return s;
}
std::wstring folder(const std::wstring& name,bool make){
    const auto f=root+L"\\"+name;
    if(make)check(CreateDirectoryW(f.c_str(),nullptr)!=FALSE,"fresh case folder");
    return f;
}
std::shared_ptr<c::JournalIdentity> identity(bool latched=true){
    auto id=std::make_shared<c::JournalIdentity>();
    id->id.process=GetCurrentProcessId();
    FILETIME a,b,k,u;GetProcessTimes(GetCurrentProcess(),&a,&b,&k,&u);
    id->id.process_start=(std::uint64_t(a.dwHighDateTime)<<32)|a.dwLowDateTime;
    for(std::size_t i=0;i<16;++i)id->id.nonce[i]=std::uint8_t(N[i]);
    id->id.executable=sha256("executable fixture");id->id.build=sha256("adapter schema fixture");id->id.profile=sha256("profile fixture");
    id->id.profile_devices=1;id->latched=latched;id->nonce_text=N;id->profile_revision="r1";
    id->core_version="core-fixture";id->adapter_version="adapter-fixture";
    id->devices={DeviceInfo{W,P,"fake wheel"}};
    id->start_line="controls: profile \"fixture\" revision r1 via shim-ini-controls-1: applied 1 (confirm)\n  horn: unsupported";
    return id;
}
c::JournalLimits limits(std::size_t cap=c::LatchedJournalCap){c::JournalLimits l;l.cap_bytes=cap;return l;}
Event ev(std::uint64_t seq,EventKind k,int device,std::uint32_t epoch,const std::string& detail){Event e;e.seq=seq;e.us=std::int64_t(seq)*1000;e.kind=k;e.device=device;e.epoch=epoch;e.detail=detail;return e;}
StatusChange st(StatusChange::Of of,int index,const std::string& status,const std::string& reason,int device,std::uint32_t epoch){
    StatusChange s;s.us=5000;s.of=of;s.index=index;s.status=status;s.reason=reason;s.device=device;s.epoch=epoch;return s;
}
std::string manifest;
void expect(const std::wstring& case_folder,const std::string& json){
    if(!manifest.empty())manifest+=",\n";
    std::string file;c::journal_detail::json_string(file,utf8(case_folder+L"\\controls-journal.jsonl"),4096);
    manifest+="  {\"file\":"+file+","+json+"}";
}
std::string read_all(const std::wstring& path){
    HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    if(h==INVALID_HANDLE_VALUE)return {};
    std::string s;char buf[65536];DWORD n=0;
    while(ReadFile(h,buf,sizeof buf,&n,nullptr)&&n)s.append(buf,n);
    CloseHandle(h);return s;
}
int count(const std::string& s,const std::string& what){int n=0;for(auto p=s.find(what);p!=std::string::npos;p=s.find(what,p+1))++n;return n;}

void roundtrip(){
    const auto f=folder(L"roundtrip",true);
    c::JournalFile j(f,false,identity(),limits());
    c::Journal d;
    d.events={ev(1,EventKind::EpochBegin,0,1,W),ev(2,EventKind::BatchAccepted,-1,0,"batch 1 accepted"),ev(3,EventKind::RunChanged,-1,0,"running: batch 1 activated"),
              ev(4,EventKind::RunChanged,-1,0,"failed: control admission closed")};
    d.status={st(StatusChange::Of::Device,0,"opening epoch 0",W,0,0),st(StatusChange::Of::Action,action_index("confirm"),"live","",0,1),
              st(StatusChange::Of::Run,0,"failed","control admission closed",-1,0)};
    d.batches_offered=1;
    check(j.append(d),"roundtrip append accepted");
    check(j.finish()&&!j.failed(),"roundtrip finish complete");
    check(j.finish(),"finish is idempotent once complete");
    check(!j.append(d),"append after finish refused");
    expect(f,"\"case\":\"roundtrip\",\"latched\":true,\"qualifies\":true,\"terminal\":1,\"devices\":1");
}
void creation(){
    const auto fresh=root+L"\\ordinary-"+std::to_wstring(GetCurrentProcessId());
    {c::JournalFile j(fresh,true,identity(false),limits(c::OrdinaryJournalCap));check(j.finish(),"ordinary journal creates its own new folder");}
    expect(fresh,"\"case\":\"ordinary\",\"latched\":false,\"qualifies\":true,\"terminal\":0,\"devices\":1");
    {c::JournalFile again(fresh,true,identity(false),limits(c::OrdinaryJournalCap));check(!again.append({})&&again.failed(),"existing ordinary folder refused, nothing overwritten");
     check(again.failure().find("folder exists")!=std::string::npos,"folder refusal reported");}
    const auto f=folder(L"existing",true);
    {HANDLE h=CreateFileW((f+L"\\controls-journal.jsonl").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);check(h!=INVALID_HANDLE_VALUE,"fixture file");
     DWORD n=0;WriteFile(h,"owner\n",6,&n,nullptr);CloseHandle(h);}
    {c::JournalFile j(f,false,identity(),limits());check(!j.append({})&&j.failure().find("file exists")!=std::string::npos,"existing journal file refused (CREATE_NEW)");}
    check(read_all(f+L"\\controls-journal.jsonl")=="owner\n","an existing file is never touched");
    {c::JournalFile lazy(folder(L"never-opened",false),false,identity(),limits());}   // constructor and destructor touch nothing
    check(GetFileAttributesW((root+L"\\never-opened").c_str())==INVALID_FILE_ATTRIBUTES,"constructor creates nothing");
}
void escapes(){
    const auto f=folder(L"escapes",true);
    c::JournalFile j(f,false,identity(),limits());
    std::string controls;for(int i=0;i<260;++i)controls+=char(1+i%31);
    auto id=identity();id->devices[0].name=controls;   // 260 controls: the escaped name bound
    c::JournalFile named(folder(L"escapes-name",true),false,id,limits());
    check(named.finish(),"a 260-unit control-character name stays within its bound");
    expect(root+L"\\escapes-name","\"case\":\"escapes-name\",\"latched\":true,\"qualifies\":false,\"terminal\":0,\"devices\":1,\"deviceName\":\""+hex(reinterpret_cast<const std::uint8_t*>(controls.data()),controls.size())+"\"");
    const std::string mixed="quote\" back\\ tab\t nl\n del\x7f nonbmp \xF0\x9F\x8E\xAE end";
    std::string big;for(int i=0;i<512;++i)big+=char(2);   // a bounded_diagnostic of controls: 3072 escaped
    c::Journal d;d.status={st(StatusChange::Of::Action,0,"read-failed",mixed,0,1),st(StatusChange::Of::Action,1,"read-failed",big,0,1)};
    d.events={ev(1,EventKind::EpochBegin,0,1,W),ev(2,EventKind::BatchRefused,-1,0,big)};
    check(j.append(d),"escapes and maximum fields accepted");
    check(j.finish(),"escapes finish complete");
    expect(f,"\"case\":\"escapes\",\"latched\":true,\"qualifies\":false,\"terminal\":0,\"devices\":1,\"reasons\":[\""+hex(reinterpret_cast<const std::uint8_t*>(mixed.data()),mixed.size())+"\",\""+
           hex(reinterpret_cast<const std::uint8_t*>(big.data()),big.size())+"\"]");
    c::JournalFile bad(folder(L"invalid-utf8",true),false,identity(),limits());
    c::Journal b;b.status={st(StatusChange::Of::Action,0,"live","bad \xff byte",0,1)};
    check(!bad.append(b)&&bad.failed(),"invalid UTF-8 refused, never written");
    check(!bad.finish(),"a refused journal never finishes complete");
    expect(root+L"\\invalid-utf8","\"case\":\"invalid-utf8\",\"latched\":true,\"qualifies\":false,\"incomplete\":true");
    c::JournalFile over(folder(L"over-bound",true),false,identity(),limits());
    c::Journal o;o.events={ev(1,EventKind::BatchRefused,-1,0,big+"\x02")};
    check(!over.append(o)&&over.failure().find("above its bound")!=std::string::npos,"a field above its escaped bound refused, never truncated");
    (void)over.finish();
}
void full_queue(){
    const auto f=folder(L"full-queue",true);
    c::JournalFile j(f,false,identity(),limits());
    std::string big;for(int i=0;i<512;++i)big+=char(3);
    c::Journal d;
    d.events.push_back(ev(1,EventKind::EpochBegin,0,1,W));
    for(std::uint64_t i=2;i<=1024;++i)d.events.push_back(ev(i,EventKind::BatchRefused,-1,0,big));
    for(int i=0;i<256;++i)d.status.push_back(st(StatusChange::Of::Action,i%ActionCount,"read-failed",big,0,1));
    check(j.append(d),"a full queue at maximum field sizes is written in chunks");
    check(j.finish(),"full queue finish complete");
    expect(f,"\"case\":\"full-queue\",\"latched\":true,\"qualifies\":false,\"terminal\":0,\"devices\":1,\"events\":1024,\"statuses\":256");
}
void short_write(){
    const auto f=folder(L"short-write",true);
    auto l=limits();int writes=0;
    l.write=[&](HANDLE h,const char* p,DWORD n,DWORD& done){
        if(++writes==3){(void)WriteFile(h,p,n/2+7,&done,nullptr);return true;}   // a short write, mid-line, in the middle of a chunk
        return WriteFile(h,p,n,&done,nullptr)!=FALSE;
    };
    c::JournalFile j(f,false,identity(),l);
    std::string big;for(int i=0;i<512;++i)big+=char(4);
    c::Journal d;for(std::uint64_t i=1;i<=200;++i)d.events.push_back(ev(i,EventKind::BatchRefused,-1,0,big));
    check(!j.append(d)&&j.failed(),"a short write latches failure");
    check(!j.append({}),"a latched sink never retries");
    check(!j.finish(),"finish after a fault is never complete");
    check(count(read_all(f+L"\\controls-journal.jsonl"),"\"type\":\"end\"")==0,"no success end after a fault");
    expect(f,"\"case\":\"short-write\",\"latched\":true,\"qualifies\":false,\"incomplete\":true,\"torn\":true");
}
void cap(){
    const auto f=folder(L"cap",true);
    c::JournalFile j(f,false,identity(),limits(96*1024));
    std::string big;for(int i=0;i<512;++i)big+=char(5);
    c::Journal d;for(std::uint64_t i=1;i<=100;++i)d.events.push_back(ev(i,EventKind::BatchRefused,-1,0,big));
    check(!j.append(d)&&j.failure().find("cap")!=std::string::npos,"the cap fails the journal");
    check(!j.finish(),"a capped journal never finishes complete");
    const auto text=read_all(f+L"\\controls-journal.jsonl");
    check(count(text,"\"reason\":\"cap\"")==1&&text.size()<=96*1024,"the reserved tail reports the cap within the cap");
    expect(f,"\"case\":\"cap\",\"latched\":true,\"qualifies\":false,\"incomplete\":true,\"endReason\":\"cap\"");
}
void empty_drains(){
    const auto f=folder(L"empty",true);
    c::JournalFile j(f,false,identity(),limits());
    c::Journal d;d.batches_offered=1;
    for(int i=0;i<4;++i)check(j.append(d),"empty drain accepted");
    c::Journal changed=d;changed.identical_skipped=7;
    check(j.append(changed),"changed counter written");
    check(j.finish(),"empty drains finish");
    const auto text=read_all(f+L"\\controls-journal.jsonl");
    check(count(text,"\"type\":\"counters\"")==2,"unchanged empty drains are skipped");
    check(text.find("\"emptyDrainsSkipped\":3")!=std::string::npos,"the skip count reaches the next written line");
    expect(f,"\"case\":\"empty\",\"latched\":true,\"qualifies\":false,\"terminal\":0,\"devices\":1,\"emptySkipped\":3");
}
void worker_rules(){
    const auto f=folder(L"off-worker",true);
    c::JournalFile j(f,false,identity(),limits());
    check(j.append({}),"first worker call");
    bool other=true;std::thread t([&]{other=j.append({});});t.join();
    check(!other&&j.failed(),"a call off the owning worker latches failure");
    (void)j.finish();
    const auto g=folder(L"unfinished",true);
    {c::JournalFile u(g,false,identity(),limits());c::Journal d;d.events={ev(1,EventKind::EpochBegin,0,1,W)};check(u.append(d),"unfinished append");}
    expect(g,"\"case\":\"unfinished\",\"latched\":true,\"qualifies\":false,\"incomplete\":true");
}
void concurrent_reader(){
    const auto f=folder(L"tail",true);
    c::JournalFile j(f,false,identity(),limits());
    std::atomic<bool> done{false};bool whole=true;int reads=0;
    std::thread reader([&]{
        while(!done.load()){
            const auto s=read_all(f+L"\\controls-journal.jsonl");++reads;
            const auto last=s.rfind('\n');if(last==std::string::npos)continue;
            for(std::size_t a=0;a<last;){const auto b=s.find('\n',a);if(s[a]!='{'||s[b-1]!='}')whole=false;a=b+1;}
        }
    });
    for(std::uint64_t i=1;i<=300;++i){c::Journal d;d.events={ev(i,EventKind::BatchRefused,-1,0,"tail "+std::to_string(i))};d.batches_offered=i;check(j.append(d),"tail append");}
    check(j.finish(),"tail finish");done=true;reader.join();
    check(whole&&reads>0,"a sharing reader sees whole lines up to the last newline");
    expect(f,"\"case\":\"tail\",\"latched\":true,\"qualifies\":false,\"terminal\":0,\"devices\":1");
}

// Core with one scripted wheel, as in the control_pump fixture.
struct Rig {
    fake::Provider provider;Core core;
    explicit Rig(bool latched):core(provider,config(latched)){
        fake::Device d;d.listed={W,P,"fake wheel"};d.caps=fake::wheel_caps();d.state.buttons[0]=0x80;provider.add(d);
        core.set_profile(read_profile("[Controls]\nSchema=1\nconfirm=button 0 dev="+W+" prod="+P+"\n"));
    }
    static CoreConfig config(bool latched){CoreConfig cfg;cfg.latched=latched;return cfg;}
    const Frame& step(std::int64_t us=10000){core.service_devices();provider.advance(us);return core.tick();}
    void lose(){std::lock_guard<std::mutex> l(provider.m);provider.at(0).script.push_back({Call::Gone,Call::NotCalled});}
};
// The reader's epoch rule over a sequence of cuts: a nonzero-epoch status needs its EpochBegin in the same or an
// earlier cut.
bool epoch_rule(const std::vector<JournalCut>& cuts){
    std::set<std::pair<int,std::uint32_t>> begun;
    for(const auto& k:cuts){
        for(const auto& e:k.events)if(e.kind==EventKind::EpochBegin)begun.insert({e.device,e.epoch});
        for(const auto& s:k.status)if(s.epoch!=0&&!begun.count({s.device,s.epoch}))return false;
    }
    return true;
}
void atomic_drain(){
    {   // Deterministic: the old split drain (events, then a tick, then statuses) breaks the rule; one cut never does.
        Rig r(false);r.step();r.step();
        std::vector<JournalCut> split;split.push_back(r.core.drain_journal());
        r.lose();r.step();
        JournalCut early;early.events=r.core.drain_events();   // events drained first ...
        r.step();r.step();                                      // ... the input thread adopts epoch 2 and queues its status ...
        early.status=r.core.drain_status();                     // ... statuses drained afterwards
        split.push_back(early);split.push_back(r.core.drain_journal());
        check(!epoch_rule(split),"a split drain can carry an epoch status ahead of its EpochBegin");
        Rig q(false);q.step();q.step();
        std::vector<JournalCut> whole;whole.push_back(q.core.drain_journal());
        q.lose();q.step();q.step();q.step();whole.push_back(q.core.drain_journal());
        check(epoch_rule(whole),"one cut keeps an epoch status with its EpochBegin");
    }
    {   // Interleaved: the input thread ticks and loses its wheel while a worker drains continuously.
        Rig r(false);std::atomic<bool> stop{false};std::vector<JournalCut> cuts;
        std::thread drainer([&]{while(!stop.load()){auto k=r.core.drain_journal();if(!k.events.empty()||!k.status.empty())cuts.push_back(std::move(k));}});
        for(int i=0;i<4000;++i){if(i%7==3)r.lose();r.step();}
        stop=true;drainer.join();cuts.push_back(r.core.drain_journal());
        int epochs=0;for(const auto& k:cuts)for(const auto& e:k.events)if(e.kind==EventKind::EpochBegin)++epochs;
        check(epochs>100,"the race exercised many epochs");
        check(epoch_rule(cuts),"every interleaved cut keeps an epoch status with its EpochBegin");
    }
}
// Pump plus FileIo over real inject files: one latched run, closed by the host order (close, tick, finish).
void pump_integration(){
    const auto f=folder(L"pump",true);
    const auto session=f+L"\\inject.on",batch=f+L"\\inject.txt";
    FILETIME now;GetSystemTimeAsFileTime(&now);
    const auto unix=((std::uint64_t(now.dwHighDateTime)<<32|now.dwLowDateTime)-116444736000000000ULL)/10000000ULL;
    auto write=[&](const std::wstring& path,const std::string& text){
        const auto tmp=path+L".tmp";HANDLE h=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);check(h!=INVALID_HANDLE_VALUE,"fixture write");
        DWORD n=0;WriteFile(h,text.data(),DWORD(text.size()),&n,nullptr);CloseHandle(h);check(MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING)!=FALSE,"fixture replace");
    };
    write(session,"nonce="+N+"\nexpires="+std::to_string(unix+600)+"\n");
    Rig r(true);
    c::FileIo io(session,batch,f,false,identity(),limits());
    c::Pump pump(r.core,io,{true,true,true,N});
    pump.pass();check(r.step().run==Run::Waiting,"fresh device admitted through the file session");
    write(batch,"nonce="+N+"\nbatch=1\ninject raw button 0 dev="+W+" value=1 ms=50\n");
    pump.pass();check(r.step().run==Run::Running,"the file batch reaches the real resolver");
    pump.pass();
    r.core.close_control();r.step();   // the host order: close, then the final neutral tick
    check(pump.finish()&&!io.journal().failed(),"the pump's final drain and sink close complete");
    expect(f,"\"case\":\"pump\",\"latched\":true,\"qualifies\":true,\"terminal\":1,\"devices\":1,\"terminalDetail\":\"failed: control admission closed\"");
}
}
int wmain(int argc,wchar_t** argv){
    try{
        if(argc!=2)throw std::runtime_error("usage: control_journal <fresh folder>");
        root=argv[1];check(CreateDirectoryW(root.c_str(),nullptr)!=FALSE,"fresh root folder");
        roundtrip();creation();escapes();full_queue();short_write();cap();empty_drains();worker_rules();concurrent_reader();atomic_drain();pump_integration();
        HANDLE h=CreateFileW((root+L"\\cases.json").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);check(h!=INVALID_HANDLE_VALUE,"manifest");
        const std::string m="[\n"+manifest+"\n]\n";DWORD n=0;WriteFile(h,m.data(),DWORD(m.size()),&n,nullptr);CloseHandle(h);
        std::cout<<"PASS "<<checks<<" control-journal checks\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
