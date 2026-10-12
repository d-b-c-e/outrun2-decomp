// Play with Recording on the ordinary shim backend (ordinary_recording.hpp).
#include "ordinary_recording.hpp"
#include "control_journal.hpp"
#include "sha256.hpp"
#include "startup_state.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>
namespace outrun::desktop {
namespace {
using dbce::shim::hosting::RecOutcome;
using dbce::shim::hosting::Clock;
constexpr const char* Context="OutRunDesktop ordinary shim session (Play with Recording); devices.json carries the device table and caps";
constexpr auto SaveBudget=std::chrono::seconds(60);   // the saver's one drain (the queue is written as the game runs)
std::string iso_now(){
    SYSTEMTIME t;GetSystemTime(&t);char b[40];
    std::snprintf(b,sizeof b,"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);
    return b;
}
std::uint64_t creation_time(){
    FILETIME c{},e{},k{},u{};
    if(!GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u))return 0;
    return (std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;
}
std::string jstr(const std::string& s){std::string j;if(!dbce::shim::control::journal_detail::json_string(j,s,4096))j="null";return j;}
}
// SHA-256 of a file, streamed (a capture can be hundreds of MB); also the startup snapshot's (startup_state.cpp).
std::string file_sha256(const std::filesystem::path& p,std::uint64_t& bytes){
    bytes=0;
    HANDLE f=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(f==INVALID_HANDLE_VALUE)throw std::runtime_error("cannot read "+session::utf8(p.filename()));
    BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE h{};
    std::string out;
    try{
        if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)!=0)throw std::runtime_error("SHA-256 unavailable");
        if(BCryptCreateHash(alg,&h,nullptr,0,nullptr,0,0)!=0)throw std::runtime_error("SHA-256 unavailable");
        std::vector<unsigned char> buf(1<<20);
        for(;;){
            DWORD n=0;
            if(!ReadFile(f,buf.data(),DWORD(buf.size()),&n,nullptr))throw std::runtime_error("cannot read "+session::utf8(p.filename()));
            if(!n)break;
            if(BCryptHashData(h,buf.data(),n,0)!=0)throw std::runtime_error("SHA-256 failed");
            bytes+=n;
        }
        unsigned char d[32];
        if(BCryptFinishHash(h,d,32,0)!=0)throw std::runtime_error("SHA-256 failed");
        static const char* X="0123456789abcdef";
        for(unsigned char c:d){out+=X[c>>4];out+=X[c&15];}
    }catch(...){if(h)BCryptDestroyHash(h);if(alg)BCryptCloseAlgorithmProvider(alg,0);CloseHandle(f);throw;}
    BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(alg,0);CloseHandle(f);
    return out;
}
namespace {
// A new file, written and flushed: never over an existing one.
void write_new_file(const std::filesystem::path& p,const std::string& bytes){
    HANDLE f=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)throw std::runtime_error(session::utf8(p.filename())+" exists or cannot be created");
    DWORD n=0;const bool ok=WriteFile(f,bytes.data(),DWORD(bytes.size()),&n,nullptr)&&n==bytes.size()&&FlushFileBuffers(f);
    CloseHandle(f);
    if(!ok)throw std::runtime_error(session::utf8(p.filename())+" could not be written");
}
const char* state_name(OrdinaryRecorder::State s){
    switch(s){
    case OrdinaryRecorder::State::Recording:return "recording";
    case OrdinaryRecorder::State::Saving:return "saving";
    case OrdinaryRecorder::State::Complete:return "complete";
    case OrdinaryRecorder::State::Failed:return "failed";
    default:return "none";
    }
}
}

struct OrdinaryRecorder::Shared {
    bool armed{};
    Arm arm;
    std::unique_ptr<ShimRecorder> rec;
    std::filesystem::path capture;
    std::uint64_t pid{},creation{};
    std::string started_utc;
    mutable std::mutex m;
    std::condition_variable cv;
    State state{State::None};
    std::string why,stopped_by;
    bool saver_done{};
    Drained cached{RecOutcome::Cancelled,false};
    std::string artifact;                      // artifactSha256 of the admitted capture (the saver, under m)
    State published{State::None};              // the input thread's last status.json
    std::string published_why;
    bool published_once{},published_saved{};
    std::atomic<std::uint64_t> missed_final{0},rows{0};
    std::atomic<std::int64_t> first_us{0},last_us{0};   // the rows' span (the badge and the duration limit)
    std::atomic<bool> saved{false};
    std::uint64_t max_bytes{};std::size_t slots{};   // the recorder's own limits (the proof's budget)
    bool inline_save{},inline_claimed{};             // no saver thread: the one drain runs inline, claimed once (under m)
    void save(Clock::time_point deadline);           // the one drain, the admission, the cached result
    bool claim_inline(std::unique_lock<std::mutex>& l);   // true (and l unlocked): this caller runs the inline save
    // The recorder-complete admission (romi 33165/33281/33423), on the saver after its one drain: empty when the six
    // raw members pass and recorder-complete.json was written beside them; otherwise why not (and no proof is written).
    std::string admit(const ShimRecorder::Drained& d);
};

std::string OrdinaryRecorder::Shared::admit(const ShimRecorder::Drained& d){
    const auto missed=missed_final.load();
    if(missed)return "the consumed-input seam was missed on "+std::to_string(missed)+" ticks";
    if(d.outcome!=RecOutcome::Complete||!d.joined)
        return std::string("the capture did not complete (")+dbce::shim::hosting::name(d.outcome)+(d.joined?"":", worker not joined")+
               (rec->refusal().empty()?"":"; "+rec->refusal())+")";
    if(!rec->drained_report())return "no worker report";
    const auto& r=*rec->drained_report();
    // The producer's tail is final, every row it offered was accepted and written, nothing was refused or dropped.
    if(!r.may_seal()||!r.capture_counts_final||r.first_rejected)return "the worker report is not final";
    if(r.attempted!=r.accepted||r.accepted!=r.written||r.written!=rec->pushed()||rec->rejected())return "rows offered, accepted and written differ";
    if(r.queue_full||r.invalid||r.after_failure||r.after_end||r.wrong_producer||r.after_end_wrong_producer||r.discarded)
        return "the worker refused or dropped rows";
    if(r.declared_last!=r.last_written||r.last_written!=rec->last())return "the declared tail is not the last row written";
    if(double(r.written)>arm.hz*arm.seconds)return "more rows than the preflight budget";
    // Exactly the six members, the contract and profile bytes as written.
    static const char* const Members[]={"budget.tsv","bundle.meta","devices.json","profile.ini","source.jsonl","worker.tsv"};
    std::vector<std::filesystem::path> files;
    for(const auto& e:std::filesystem::directory_iterator(capture)){
        const auto name=session::utf8(e.path().filename());
        if(!e.is_regular_file()||std::find_if(std::begin(Members),std::end(Members),[&](const char* m){return name==m;})==std::end(Members))
            return "unexpected capture entry "+name;
        files.push_back(e.path());
    }
    if(files.size()!=std::size(Members))return "capture members missing";
    std::sort(files.begin(),files.end(),[](const auto& x,const auto& y){return session::utf8(x.filename())<session::utf8(y.filename());});
    const auto small=[&](const wchar_t* n){
        std::ifstream f(capture/n,std::ios::binary);std::string b((std::istreambuf_iterator<char>(f)),{});
        if(!f.good()&&!f.eof())throw std::runtime_error("cannot read capture member");
        return b;
    };
    if(small(L"bundle.meta")!=CaptureContract2)return "bundle.meta is not contract @2";
    if(small(L"profile.ini")!=arm.profile)return "profile.ini is not the profile Core parsed";
    std::string members,digest;
    for(const auto& f:files){
        std::uint64_t n=0;const auto h=file_sha256(f,n);
        const auto name=session::utf8(f.filename());
        members+=std::string(members.empty()?"":",")+"\n  {\"name\":"+jstr(name)+",\"bytes\":"+std::to_string(n)+",\"sha256\":\""+h+"\"}";
        digest+=name+"\t"+std::to_string(n)+"\t"+h+"\n";
    }
    const auto u=[](std::uint64_t v){return std::to_string(v);};
    const auto b=[](bool v){return std::string(v?"true":"false");};
    const auto g=[](double v){char t[32];std::snprintf(t,sizeof t,"%.17g",v);return std::string(t);};
    // A playback's own capture says so (sana 37229: the managed validator admits stoppedBy "tape-end" only then); a
    // recording's proof is unchanged (no action: record, as before).
    const std::string action=arm.request.action==session::Action::Playback?",\n \"action\":\"playback\"":"";
    const std::string j="{\n \"schema\":\"outrun.recorder-complete@1\",\n \"requestId\":"+jstr(arm.request.request_id)+action+
        ",\n \"contract\":\"dbce.shim.outrun-capture@2\",\n \"outcome\":\"Complete\",\"joined\":true,\n \"stoppedBy\":"+jstr(stopped_by)+
        ",\n \"report\":{\"attempted\":"+u(r.attempted)+",\"accepted\":"+u(r.accepted)+",\"written\":"+u(r.written)+
        ",\"queueFull\":"+u(r.queue_full)+",\"invalid\":"+u(r.invalid)+",\"afterFailure\":"+u(r.after_failure)+
        ",\"afterEnd\":"+u(r.after_end)+",\"wrongProducer\":"+u(r.wrong_producer)+",\"afterEndWrongProducer\":"+u(r.after_end_wrong_producer)+
        ",\"declaredLast\":"+u(r.declared_last)+",\"lastWritten\":"+u(r.last_written)+",\"discarded\":"+u(r.discarded)+
        ",\"firstRejected\":"+b(r.first_rejected)+",\"captureCountsFinal\":"+b(r.capture_counts_final)+",\"drainTimedOut\":"+b(r.drain_timed_out)+
        ",\"producerDone\":"+b(r.producer_done)+",\"workerDone\":"+b(r.worker_done)+"}"+
        ",\n \"pushed\":"+u(rec->pushed())+",\"rejected\":"+u(rec->rejected())+",\"missedFinal\":0"+
        ",\n \"budget\":{\"hz\":"+g(arm.hz)+",\"seconds\":"+g(arm.seconds)+",\"maxBytes\":"+u(max_bytes)+",\"slots\":"+u(slots)+"}"+
        ",\n \"profileSha256\":\""+dbce::shim::hex(dbce::shim::sha256(arm.profile))+"\",\"exeSha256\":"+jstr(arm.exe_sha256)+
        ",\n \"processId\":"+u(pid)+",\"processCreationFileTime\":"+u(creation)+
        ",\n \"startedUtc\":"+jstr(started_utc)+",\"completedUtc\":"+jstr(iso_now())+
        ",\n \"members\":["+members+"\n ],\n \"artifactSha256\":\""+dbce::shim::hex(dbce::shim::sha256(digest))+"\"\n}\n";
    // Inside the capture, beside the six it describes (romi 33423: one ArtifactSeal root; recording-complete.tsv seals
    // the six and this proof).
    write_new_file(capture/L"recorder-complete.json",j);
    artifact=dbce::shim::hex(dbce::shim::sha256(digest));
    return {};
}

OrdinaryRecorder::OrdinaryRecorder():s_(std::make_shared<Shared>()){}

OrdinaryRecorder::OrdinaryRecorder(Arm a):s_(std::make_shared<Shared>()){
    auto& s=*s_;
    s.armed=true;s.arm=std::move(a);
    s.capture=s.arm.request.recording/L"capture";
    CaptureSettings c;
    c.folder=s.capture;c.profile=s.arm.profile;c.run_nonce=s.arm.request.request_id;c.adapter_version=s.arm.adapter_version;
    c.hz=s.arm.hz;c.seconds=s.arm.seconds;c.contract=CaptureContract2;c.context=Context;
    s.max_bytes=c.limits.bytes;s.slots=c.slots;   // the budget the proof names: the settings the recorder runs with (romi 33720)
    s.rec=std::make_unique<ShimRecorder>(std::move(c));
    s.pid=GetCurrentProcessId();s.creation=creation_time();s.started_utc=iso_now();
    s.state=State::Recording;
}

bool OrdinaryRecorder::armed()const{return s_->armed;}
const session::Request* OrdinaryRecorder::request()const{return s_->armed?&s_->arm.request:nullptr;}
bool OrdinaryRecorder::recording()const{std::lock_guard<std::mutex> l(s_->m);return s_->state==State::Recording;}
void OrdinaryRecorder::set_devices(std::vector<dbce::shim::DeviceReport> d){if(s_->rec)s_->rec->set_devices(std::move(d));}
void OrdinaryRecorder::push(const Snapshot& snap){
    if(!recording())return;
    s_->rec->push(snap);
    const auto us=snap.frame.tick_us;
    if(!s_->rows.load())s_->first_us=us;
    s_->last_us=us;
    s_->rows.store(s_->rec->pushed());
    // The recorder refused (its start, the folder, the preflight) or the worker refused a row (a full queue, a failed
    // writer): this capture can no longer complete, so it stops now and shows failed once drained (sana 33815).
    if(!s_->rec->refusal().empty()||s_->rec->rejected())stop("refused");
}
std::uint64_t OrdinaryRecorder::pushed()const{return s_->rec?s_->rec->pushed():0;}
bool OrdinaryRecorder::at_limit()const{
    if(!s_->armed)return false;
    const auto rows=s_->rows.load();
    return double(rows)>=s_->arm.hz*s_->arm.seconds||
           (rows&&double(s_->last_us.load()-s_->first_us.load())>=(s_->arm.seconds-1.0)*1e6);
}
void OrdinaryRecorder::count_missed_final(){++s_->missed_final;}
OrdinaryRecorder::State OrdinaryRecorder::state()const{std::lock_guard<std::mutex> l(s_->m);return s_->state;}
std::string OrdinaryRecorder::why()const{std::lock_guard<std::mutex> l(s_->m);return s_->why;}
bool OrdinaryRecorder::saved()const{return s_->saved.load();}

void OrdinaryRecorder::fail(const std::string& why){
    {
        std::lock_guard<std::mutex> l(s_->m);
        if(s_->state!=State::Recording)return;
        s_->state=State::Failed;s_->why=why;s_->saver_done=true;s_->cached={RecOutcome::Cancelled,true};
    }
    s_->cv.notify_all();
}

void OrdinaryRecorder::stop(const char* by){
    {
        std::lock_guard<std::mutex> l(s_->m);
        if(s_->state!=State::Recording)return;
        s_->state=State::Saving;s_->stopped_by=by;
    }
    try{s_->rec->finish();}catch(...){}   // the producer's tail, on the input thread (the producer); a refusal shows in the drain
    // The one drain, on a thread of its own with the whole budget (a timeout is final): never polled, never repeated.
    // A saver thread that cannot start throws nothing into the game: the drain is left to the exit's wait or the host's
    // stop, which run it inline, once (romi 33720).
    bool started=false;
    if(s_->arm.saver_thread){
        try{std::thread([s=s_]{s->save(Clock::now()+SaveBudget);}).detach();started=true;}catch(...){}
    }
    if(!started){std::lock_guard<std::mutex> l(s_->m);s_->inline_save=true;}
}

void OrdinaryRecorder::Shared::save(Clock::time_point deadline){
    // Nothing escapes: on the detached saver an exception would end the game (sana 33815).
    ShimRecorder::Drained d{RecOutcome::Cancelled,false};
    std::string reason;
    try{d=rec->drain(deadline);}
    catch(const std::exception& e){reason=std::string("the drain failed: ")+e.what();}
    catch(...){reason="the drain failed";}
    if(reason.empty()){
        try{reason=admit(d);}catch(const std::exception& e){reason=std::string("the completion proof could not be written: ")+e.what();}
        catch(...){reason="the completion proof could not be written";}
    }
    const bool ok=reason.empty();
    {
        std::lock_guard<std::mutex> l(m);
        // The host's later stop sees the admission's verdict: a capture the admission refused is never Complete.
        cached={ok?RecOutcome::Complete:(d.outcome==RecOutcome::Complete?RecOutcome::Cancelled:d.outcome),d.joined};
        state=ok?State::Complete:State::Failed;why=reason;saver_done=true;
    }
    cv.notify_all();
}

bool OrdinaryRecorder::Shared::claim_inline(std::unique_lock<std::mutex>& l){
    if(!inline_save||inline_claimed||saver_done)return false;
    inline_claimed=true;l.unlock();
    return true;
}

bool OrdinaryRecorder::observe_receipt(){
    auto& s=*s_;
    if(!s.armed||s.saved.load())return s.saved.load();
    std::string artifact;
    {std::lock_guard<std::mutex> l(s.m);if(s.state!=State::Complete)return false;artifact=s.artifact;}
    const auto path=s.arm.request.result/L"recording-verified.json";
    HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;
    std::string text(64*1024+1,'\0');DWORD n=0;
    const bool read=ReadFile(f,text.data(),DWORD(text.size()),&n,nullptr)!=0;
    CloseHandle(f);
    if(!read||n>64*1024)return false;
    text.resize(n);
    const auto fields=flat_json(text);
    if(!fields)return false;
    const auto is=[&](const char* k,const std::string& v){const auto at=fields->find(k);return at!=fields->end()&&at->second==v;};
    if(!is("schema","outrun.recording-verified@1")||!is("requestId",s.arm.request.request_id)||!is("processId",std::to_string(s.pid))||
       !is("processCreationFileTime",std::to_string(s.creation))||!is("exeSha256",s.arm.exe_sha256)||!is("artifactSha256",artifact)||
       !is("recordingSaved","true"))return false;
    // The seal it names is the one in the capture (romi 33448).
    std::string seal;
    try{std::uint64_t bytes=0;seal=file_sha256(s.capture/L"recording-complete.tsv",bytes);}catch(const std::exception&){return false;}
    if(!is("sealSha256",seal))return false;
    s.saved.store(true);
    return true;
}

std::string OrdinaryRecorder::badge()const{
    if(!s_->armed)return {};
    State st;std::string why;
    {std::lock_guard<std::mutex> l(s_->m);st=s_->state;why=s_->why;}
    const auto clock=[](double seconds){char t[16];const int v=int(seconds);std::snprintf(t,sizeof t,"%d:%02d",v/60,v%60);return std::string(t);};
    switch(st){
    case State::Recording:{
        const auto rows=s_->rows.load();
        const double span=rows?double(s_->last_us.load()-s_->first_us.load())/1e6:0.0;
        return "REC "+clock(span)+" (max "+clock(s_->arm.seconds-1.0)+")  F11 stops";   // the span limit (at_limit), sana 34555
    }
    case State::Saving:return "REC saving...";
    case State::Complete:return saved()?"REC saved":"REC complete, verifying";
    case State::Failed:return "REC failed: "+(why.size()>100?why.substr(0,100)+"...":why);
    default:return {};
    }
}

bool OrdinaryRecorder::wait_saved(Clock::time_point deadline){
    std::unique_lock<std::mutex> l(s_->m);
    if(!s_->armed||s_->state==State::Recording)return !s_->armed;
    if(s_->claim_inline(l)){s_->save(deadline);return true;}   // no saver thread: the one drain, here
    return s_->cv.wait_until(l,deadline,[&]{return s_->saver_done;});
}

void OrdinaryRecorder::finish(){if(s_->armed)stop("exit");}

OrdinaryRecorder::Drained OrdinaryRecorder::drain(Clock::time_point deadline){
    if(!s_->armed)return {RecOutcome::Complete,true};   // nothing captured, as NoCapture
    stop("exit");
    std::unique_lock<std::mutex> l(s_->m);
    if(s_->claim_inline(l)){s_->save(deadline);l.lock();}   // no saver thread: the one drain, in the host's stop
    if(!s_->cv.wait_until(l,deadline,[&]{return s_->saver_done;}))return {RecOutcome::TimedOut,false};   // the saver still runs
    return s_->cached;
}

void OrdinaryRecorder::publish_status(bool force){
    if(!s_->armed)return;
    State st;std::string why,by;
    {std::lock_guard<std::mutex> l(s_->m);st=s_->state;why=s_->why;by=s_->stopped_by;}
    const bool saved=s_->saved.load();
    if(!force&&s_->published_once&&st==s_->published&&why==s_->published_why&&saved==s_->published_saved)return;
    const char* name=st==State::Complete&&saved?"saved":state_name(st);
    if(write_session_status(s_->arm.request,name,why,by,s_->started_utc)){
        s_->published=st;s_->published_why=why;s_->published_saved=saved;s_->published_once=true;
    }
}

bool write_session_status(const session::Request& q,const char* state,const std::string& why,const std::string& stopped_by,
                          const std::string& started_utc){
    const std::string j=std::string("{\n \"schema\":\"outrun.session-status@1\",\n \"requestId\":")+jstr(q.request_id)+
        ",\n \"action\":\""+(q.action==session::Action::Playback?"playback":"record")+"\",\n \"state\":\""+state+"\",\n \"why\":"+jstr(why)+
        ",\n \"stoppedBy\":"+jstr(stopped_by)+",\n \"processId\":"+std::to_string(GetCurrentProcessId())+
        ",\"processCreationFileTime\":"+std::to_string(creation_time())+",\n \"startedUtc\":"+jstr(started_utc)+
        ",\"updatedUtc\":"+jstr(iso_now())+"\n}\n";
    return replace_file_atomically(q.result/L"status.json",j);
}

bool replace_file_atomically(const std::filesystem::path& path,const std::string& bytes){
    const auto tmp=path.wstring()+L".tmp-"+std::to_wstring(GetCurrentProcessId());
    HANDLE f=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;
    DWORD n=0;const bool ok=WriteFile(f,bytes.data(),DWORD(bytes.size()),&n,nullptr)&&n==bytes.size()&&FlushFileBuffers(f);
    CloseHandle(f);
    if(!ok){DeleteFileW(tmp.c_str());return false;}
    // A reader holding status.json open makes the rename fail briefly (inject-publish-by-rename): retry about a second.
    for(int i=0;i<20;++i){
        if(MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return true;
        Sleep(50);
    }
    DeleteFileW(tmp.c_str());
    return false;
}

std::optional<std::map<std::string,std::string>> flat_json(const std::string& t){
    std::map<std::string,std::string> out;
    std::size_t i=0;
    const auto ws=[&]{while(i<t.size()&&(t[i]==' '||t[i]=='\t'||t[i]=='\r'||t[i]=='\n'))++i;};
    // A JSON string; only escapes that decode to ASCII are accepted.
    const auto str=[&](std::string& v)->bool{
        if(i>=t.size()||t[i]!='"')return false;
        ++i;v.clear();
        while(i<t.size()&&t[i]!='"'){
            const unsigned char c=static_cast<unsigned char>(t[i]);
            if(c<0x20)return false;
            if(c!='\\'){v+=char(c);++i;continue;}
            if(++i>=t.size())return false;
            switch(t[i]){
            case '"':v+='"';break;case '\\':v+='\\';break;case '/':v+='/';break;
            case 'b':v+='\b';break;case 'f':v+='\f';break;case 'n':v+='\n';break;case 'r':v+='\r';break;case 't':v+='\t';break;
            case 'u':{
                if(i+4>=t.size())return false;
                unsigned code=0;
                for(int k=1;k<=4;++k){
                    const char h=t[i+std::size_t(k)];code<<=4;
                    if(h>='0'&&h<='9')code|=unsigned(h-'0');else if(h>='a'&&h<='f')code|=unsigned(h-'a'+10);
                    else if(h>='A'&&h<='F')code|=unsigned(h-'A'+10);else return false;
                }
                if(code>=0x80)return false;
                v+=char(code);i+=4;break;
            }
            default:return false;
            }
            ++i;
        }
        if(i>=t.size())return false;
        ++i;return true;
    };
    ws();
    if(i>=t.size()||t[i]!='{')return std::nullopt;
    ++i;ws();
    if(i<t.size()&&t[i]=='}'){++i;ws();return i==t.size()?std::optional(out):std::nullopt;}
    for(;;){
        std::string key,value;
        ws();
        if(!str(key))return std::nullopt;
        ws();
        if(i>=t.size()||t[i]!=':')return std::nullopt;
        ++i;ws();
        if(i>=t.size())return std::nullopt;
        if(t[i]=='"'){if(!str(value))return std::nullopt;}
        else if(t.compare(i,4,"true")==0){value="true";i+=4;}
        else if(t.compare(i,5,"false")==0){value="false";i+=5;}
        else if(t[i]=='-'||(t[i]>='0'&&t[i]<='9')){
            const auto from=i;if(t[i]=='-')++i;
            const auto digits=i;
            while(i<t.size()&&t[i]>='0'&&t[i]<='9')++i;
            if(i==digits||(t[digits]=='0'&&i-digits>1))return std::nullopt;   // integers only, no leading zeros
            value=t.substr(from,i-from);
        }else return std::nullopt;                                            // null, a fraction, an object or an array
        if(!out.emplace(key,value).second)return std::nullopt;                // a key twice
        ws();
        if(i<t.size()&&t[i]==','){++i;continue;}
        if(i<t.size()&&t[i]=='}'){++i;break;}
        return std::nullopt;
    }
    ws();
    if(i!=t.size())return std::nullopt;
    return out;
}
}

namespace outrun::desktop::session {
namespace {
// A folder made, or one already there (never a file, never through a reparse point).
bool folder(const std::filesystem::path& p){
    if(CreateDirectoryW(p.c_str(),nullptr))return true;
    if(GetLastError()!=ERROR_ALREADY_EXISTS)return false;
    const DWORD a=GetFileAttributesW(p.c_str());
    return a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_DIRECTORY)&&!(a&FILE_ATTRIBUTE_REPARSE_POINT);
}
std::wstring running_exe(){
    std::wstring p(32768,L'\0');
    const DWORD n=GetModuleFileNameW(nullptr,p.data(),DWORD(p.size()));
    p.resize(n<p.size()?n:0);
    return p;
}
}

Intake take(const std::filesystem::path& file,const std::filesystem::path& game_folder,bool shim_backend,std::uint64_t now_unix){
    Intake r;
    if(file.empty())return r;
    // <pid>-<n>: the game takes once per process (n = 1); unique per take, so a refused claim is never lost to a name in use.
    static std::atomic<unsigned> takes{0};
    const auto pid=std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++takes);
    const auto win32=[](DWORD e){return " (Win32 "+std::to_string(e)+")";};
    // Claimed first, by a rename in its own folder (one step on one volume): the request's name is gone at once, so no
    // later launch can take it, whatever happens to its copy below (sana 34298; the 12:56 pilot's result folder was on
    // another volume, where a move is a copy and a delete that can fail apart). Only the exact name is ever taken.
    const std::wstring claimed=file.wstring()+L".taken-"+pid;
    if(!MoveFileExW(file.c_str(),claimed.c_str(),MOVEFILE_WRITE_THROUGH)){
        const DWORD e=GetLastError();
        if(e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND)return r;   // no request: ordinary play
        r.kind=Intake::Kind::Exit;r.why="the session request cannot be claimed"+win32(e);
        return r;
    }
    std::string bytes;bool read=false;DWORD read_error=0;
    if(HANDLE f=CreateFileW(claimed.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);f!=INVALID_HANDLE_VALUE){
        bytes.assign(4097,'\0');DWORD n=0;
        read=ReadFile(f,bytes.data(),DWORD(bytes.size()),&n,nullptr)!=0;
        if(!read)read_error=GetLastError();
        CloseHandle(f);
        bytes.resize(read?n:0);
    }else read_error=GetLastError();
    // A refused request is kept beside the claim as .refused-<pid>-<n>: never taken again, never deleted. If that rename
    // fails the claim itself stays, already inert (sana 34430: evidence is kept, not removed).
    const auto aside=[&]{
        const std::wstring refused=file.wstring()+L".refused-"+pid;
        (void)MoveFileExW(claimed.c_str(),refused.c_str(),MOVEFILE_WRITE_THROUGH);
    };
    const auto parsed=read?parse(bytes,now_unix,game_folder):Parsed{false,{},"the session request cannot be read"+win32(read_error),Action::Playback};
    if(!parsed.ok){
        aside();
        r.kind=parsed.refused_as==Action::Record?Intake::Kind::Refused:Intake::Kind::Exit;r.why=parsed.why;
        return r;   // the folders it named were not admitted: no status is written there
    }
    r.request=parsed.request;
    const bool record=parsed.request.action==Action::Record;
    // Before the copy is in its result folder: refused with no status.json (the folder may be another session's).
    const auto refused=[&](const std::string& why){aside();r.kind=record?Intake::Kind::Refused:Intake::Kind::Exit;r.why=why;return r;};
    // After: refused with status.json "failed" in its own result folder.
    const auto refuse=[&](const std::string& why){
        r.kind=record?Intake::Kind::Refused:Intake::Kind::Exit;r.why=why;
        (void)write_session_status(r.request,"failed",why,"","");
        return r;
    };
    if(!folder(r.request.result))return refused("the result folder cannot be made"+win32(GetLastError()));
    // The exclusive copy of the bytes read into the result folder: never over an earlier request (that folder is
    // another session's, and its status is never written over).
    const auto consumed=r.request.result/L"session-request.txt";
    HANDLE c=CreateFileW(consumed.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(c==INVALID_HANDLE_VALUE){
        const DWORD e=GetLastError();
        if(e==ERROR_FILE_EXISTS||e==ERROR_ALREADY_EXISTS)return refused("the result folder already holds a request (another session's)");
        return refused("the request cannot be copied into its result folder"+win32(e));
    }
    DWORD written=0;
    const bool wrote=WriteFile(c,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(c);
    const DWORD write_error=wrote?0:GetLastError();
    CloseHandle(c);
    if(!wrote){(void)DeleteFileW(consumed.c_str());return refused("the request copy could not be written"+win32(write_error));}
    (void)DeleteFileW(claimed.c_str());   // consumed; a claim that cannot be deleted stays inert (only the exact name is taken)
    if(!record){
        // Playback (startup_state.hpp; sana 37135/37176): the recording's own exe, the tape's exact bytes, the verified
        // startup snapshot, a fresh capture folder for the replay's own capture, then a new private home seeded from the
        // snapshot (the owner's saves and settings are never written). Any refusal: never ordinary play.
        const auto exe=running_exe();
        if(exe.empty())return refuse("the running exe's path is unknown");
        try{std::uint64_t size=0;r.exe_sha256=file_sha256(exe,size);}catch(const std::exception& e){return refuse(std::string("the running exe cannot be hashed: ")+e.what());}
        if(r.exe_sha256!=r.request.exe_sha256)return refuse("the running exe is not the recording's (exeSha256)");
        try{std::uint64_t size=0;if(file_sha256(r.request.tape,size)!=r.request.tape_sha256)return refuse("the tape's bytes are not tapeSha256");}
        catch(const std::exception& e){return refuse(std::string("the tape cannot be read: ")+e.what());}
        const auto v=startup::verify(r.request.startup,r.request.startup_seal_sha256,r.request.exe_sha256);
        if(!v.ok)return refuse("the startup snapshot is refused: "+v.why);
        // The recorded settings, read once and checked against the seal: the process uses these bytes, not the file again.
        for(const auto& m:v.snapshot.members){
            if(m.absent||m.relative!="OutRunDesktop.ini")continue;
            const auto p=r.request.startup/L"OutRunDesktop.ini";
            HANDLE f=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(f==INVALID_HANDLE_VALUE)return refuse("the recorded settings cannot be read"+win32(GetLastError()));
            std::string s(std::size_t(m.size)+1,'\0');DWORD n=0;const bool ok=ReadFile(f,s.data(),DWORD(s.size()),&n,nullptr)!=0;CloseHandle(f);
            s.resize(ok?n:0);
            if(!ok||s.size()!=m.size||dbce::shim::hex(dbce::shim::sha256(s))!=m.sha256)return refuse("the recorded settings changed after the startup check");
            r.recorded_settings=std::move(s);r.has_recorded_settings=true;
        }
        if(!folder(r.request.recording))return refuse("the recording folder cannot be made"+win32(GetLastError()));
        if(GetFileAttributesW((r.request.recording/L"capture").c_str())!=INVALID_FILE_ATTRIBUTES)return refuse("the recording folder already holds a capture");
        r.home=r.request.result/L"home";
        if(const auto why=startup::seed(r.request.startup,v.snapshot,r.home);!why.empty())return refuse("the playback home: "+why);
        r.kind=Intake::Kind::Playback;
        return r;
    }
    if(!shim_backend)return refuse("Play with Recording needs [Input] Backend = Shim");
    if(!folder(r.request.recording))return refuse("the recording folder cannot be made"+win32(GetLastError()));
    if(GetFileAttributesW((r.request.recording/L"capture").c_str())!=INVALID_FILE_ATTRIBUTES)return refuse("the recording folder already holds a capture");
    const auto exe=running_exe();
    if(exe.empty())return refuse("the running exe's path is unknown");
    try{std::uint64_t size=0;r.exe_sha256=file_sha256(exe,size);}catch(const std::exception& e){return refuse(std::string("the running exe cannot be hashed: ")+e.what());}
    r.kind=Intake::Kind::Record;
    return r;
}
}
