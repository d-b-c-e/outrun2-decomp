// Play with Recording (source/ordinary_recording.hpp, session_request.hpp take): the recorder over the real frame
// recorder and session writer, in a temp folder of its own. One drain on the saver, its result cached for the host's
// stop; the six-member admission and recorder-complete.json; every refusal is failed, never complete; the receipt
// reader; the start's intake of a request file.
#include "ordinary_recording.hpp"
#include "consumed_replay.hpp"
#include "sha256.hpp"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iterator>
#include <string>
using namespace outrun::desktop;
namespace fs=std::filesystem;
namespace {
int checks=0;
void check(bool v,const std::string& what){++checks;if(!v){std::fprintf(stderr,"ordinary recording: FAIL: %s\n",what.c_str());std::exit(1);}}
std::string slurp(const fs::path& p){std::ifstream f(p,std::ios::binary);return std::string((std::istreambuf_iterator<char>(f)),{});}
void spit(const fs::path& p,const std::string& s){std::ofstream f(p,std::ios::binary);f<<s;}
bool has(const std::string& text,const std::string& part){return text.find(part)!=std::string::npos;}
// A folder of the test's own: the PID and the tick count (a PID alone can be reused).
const fs::path Root=fs::temp_directory_path()/("outrun-ordinary-recording-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
int cases=0;
const std::string Profile="[Controls]\nsteer=axis:X\n";
const std::string Exe(64,'e');
OrdinaryRecorder::Arm arm(double hz=60,double seconds=300){
    const auto dir=Root/("case"+std::to_string(++cases));
    fs::create_directories(dir/"result");fs::create_directories(dir/"recording");
    OrdinaryRecorder::Arm a;
    a.request.action=session::Action::Record;
    char id[33];std::snprintf(id,sizeof id,"%032x",cases);a.request.request_id=id;
    a.request.result=dir/"result";a.request.recording=dir/"recording";
    a.profile=Profile;a.exe_sha256=Exe;a.adapter_version="ordinary recording test";a.hz=hz;a.seconds=seconds;
    return a;
}
std::vector<dbce::shim::DeviceReport> devices(){
    dbce::shim::DeviceReport d;d.identity.name="Test Wheel";d.identity.dev="{00000000-0000-0000-0000-000000000001}";
    d.identity.prod="{00000000-0000-0000-0000-000000000002}";d.opened=true;d.epoch=1;d.status="open";
    return {d};
}
Snapshot row(std::uint64_t seq,std::int64_t us){
    Snapshot s;s.frame.seq=seq;s.frame.tick_us=us;s.frame.boundary_us=us;   // as a Core frame: boundary >= tick
    s.frame.action[0].value=0.25f;s.frame.action[0].status=dbce::shim::Status::Live;
    s.signals.frame_seq=seq;s.signals.mode=16;s.signals.in_gameplay=true;
    s.view.mode=16;s.view.car=true;s.view.gear=3;s.view.speed=50.f;
    s.has_consumed=true;s.consumed.steering=-10;s.consumed.accelerator=200;s.consumed.connected=true;
    s.keyboard.any=seq%2==0;
    return s;
}
bool saved_within(OrdinaryRecorder& r){return r.wait_saved(dbce::shim::hosting::Clock::now()+std::chrono::seconds(30));}
const auto Soon=[]{return dbce::shim::hosting::Clock::now()+std::chrono::seconds(5);};
std::string digest_of(const fs::path& capture){
    std::string lines;
    for(const char* n:{"budget.tsv","bundle.meta","devices.json","profile.ini","source.jsonl","worker.tsv"}){
        const auto b=slurp(capture/n);
        lines+=std::string(n)+"\t"+std::to_string(b.size())+"\t"+dbce::shim::hex(dbce::shim::sha256(b))+"\n";
    }
    return dbce::shim::hex(dbce::shim::sha256(lines));
}
std::string receipt(const OrdinaryRecorder::Arm& a,const std::string& artifact,const std::string& seal,const std::string& pid,
                    const std::string& extra=""){
    FILETIME c{},e{},k{},u{};GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);
    const auto creation=(std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;
    return "{\"schema\":\"outrun.recording-verified@1\",\"requestId\":\""+a.request.request_id+"\",\"processId\":"+pid+
           ",\"processCreationFileTime\":"+std::to_string(creation)+",\"exeSha256\":\""+a.exe_sha256+"\",\"artifactSha256\":\""+artifact+
           "\",\"sealSha256\":\""+seal+"\",\"recordingSaved\":true,\"rows\":120,\"scope\":\"recording saved\""+extra+"}\n";
}
}

// The managed finalizer on a real native capture while this process is alive (romi 33720: the wire between the two):
// --seal-recording writes the seal and <result>\recording-verified.json, then the native side must accept that receipt.
int run(const std::wstring& command,const fs::path& log){
    SECURITY_ATTRIBUTES sa{sizeof sa,nullptr,TRUE};
    HANDLE out=CreateFileW(log.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    STARTUPINFOW si{};si.cb=sizeof si;si.dwFlags=STARTF_USESTDHANDLES;si.hStdOutput=out;si.hStdError=out;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};std::wstring line=command;
    const bool ok=CreateProcessW(nullptr,line.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=0;
    CloseHandle(out);
    if(!ok)return -1;
    DWORD code=1;
    if(WaitForSingleObject(pi.hProcess,120000)!=WAIT_OBJECT_0){TerminateProcess(pi.hProcess,1);code=2;}
    else GetExitCodeProcess(pi.hProcess,&code);
    CloseHandle(pi.hProcess);CloseHandle(pi.hThread);
    return int(code);
}
void finalizer_end_to_end(const fs::path& finalizer){
    const auto a=arm();OrdinaryRecorder r(a);r.set_devices(devices());
    for(std::uint64_t i=0;i<180;++i)r.push(row(5000+i,std::int64_t(i)*16667));
    r.stop("f11");
    check(saved_within(r)&&r.state()==OrdinaryRecorder::State::Complete,"end to end: the native capture completes: "+r.why());
    FILETIME c{},e{},k{},u{};GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);
    const auto creation=(std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;
    const auto capture=a.request.recording/"capture";
    const auto args=[&](const wchar_t* verb,const fs::path& receipt){
        return L"\""+finalizer.wstring()+L"\" "+verb+L" \""+capture.wstring()+L"\" "+std::wstring(a.request.request_id.begin(),a.request.request_id.end())+
               L" "+std::wstring(Exe.begin(),Exe.end())+L" "+std::to_wstring(GetCurrentProcessId())+L" "+std::to_wstring(creation)+L" \""+receipt.wstring()+L"\"";
    };
    const auto seal_log=a.request.result/"seal.log";
    const int sealed=run(args(L"--seal-recording",a.request.result/"recording-verified.json"),seal_log);
    std::printf("ordinary recording: finalizer --seal-recording exit %d: %s",sealed,slurp(seal_log).c_str());
    check(sealed==0,"end to end: the managed finalizer seals the native capture");
    check(fs::exists(capture/"recording-complete.tsv"),"end to end: the seal is in the capture");
    check(r.observe_receipt()&&r.saved()&&r.badge()=="REC saved","end to end: the native side accepts the managed receipt");
    const auto read_log=a.request.result/"read.log";
    const int read=run(args(L"--read-recording",a.request.result/"recording-reread.json"),read_log);
    std::printf("ordinary recording: finalizer --read-recording exit %d: %s",read,slurp(read_log).c_str());
    check(read==0,"end to end: the sealed capture reads back");
    // The managed --export-recording (sana aefe991), when this finalizer has it: the native consumed tape reads its rows.
    const auto exported=a.request.result/"export.tsv";
    const auto export_log=a.request.result/"export.log";
    const int code=run(args(L"--export-recording",exported),export_log);
    std::printf("ordinary recording: finalizer --export-recording exit %d: %s",code,slurp(export_log).c_str());
    if(code==0){
        ConsumedTape tape(exported);
        check(tape.size()==180&&tape.row(0).seq==5000&&tape.row(179).seq==5179,"end to end: the export's 180 rows load as a consumed tape");
        check(tape.row(7).replayed.input.steering==-10&&tape.row(7).replayed.input.accelerator==200&&tape.row(7).replayed.input.connected,
              "end to end: the exported consumed input decodes");
    }else std::printf("ordinary recording: this finalizer has no --export-recording (an older build): the export check is not run\n");
}

int run_all(int argc,char** argv);
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    try{return run_all(argc,argv);}
    catch(const std::exception& e){std::fprintf(stderr,"ordinary recording: FAIL: an exception after %d checks: %s\n",checks,e.what());return 1;}
}
int run_all(int argc,char** argv){
    fs::path finalizer,cross;
    for(int i=1;i+1<argc;++i){
        if(std::string(argv[i])=="--finalizer")finalizer=fs::u8path(argv[i+1]);
        if(std::string(argv[i])=="--cross-volume")cross=fs::u8path(argv[i+1]);
    }
    fs::create_directories(Root);
    if(!finalizer.empty()){finalizer_end_to_end(finalizer);std::printf("ordinary recording: end to end with %s\n",session::utf8(finalizer).c_str());}
    // Unarmed: nothing at all, as NoCapture was.
    {OrdinaryRecorder r;
     check(!r.armed()&&!r.recording()&&r.badge().empty()&&!r.at_limit()&&!r.request(),"unarmed: no recording, no badge");
     const auto d=r.drain(Soon());check(d.outcome==dbce::shim::hosting::RecOutcome::Complete&&d.joined,"unarmed: the host's stop resolves");}

    // The happy path: 120 consecutive rows, F11, one drain, the proof inside the capture, the host's later stop cached.
    {const auto a=arm();OrdinaryRecorder r(a);OrdinaryRecorder host_copy=r;
     check(r.armed()&&r.recording()&&r.state()==OrdinaryRecorder::State::Recording,"armed: recording from the start");
     r.publish_status(true);
     check(has(slurp(a.request.result/"status.json"),"\"state\":\"recording\""),"status.json says recording");
     r.set_devices(devices());
     for(std::uint64_t i=0;i<120;++i)r.push(row(1000+i,std::int64_t(i)*16667));
     check(r.pushed()==120&&!r.at_limit(),"120 rows pushed, under the limit");
     check(has(r.badge(),"REC 0:01")&&has(r.badge(),"max 4:59")&&has(r.badge(),"F11"),"the badge counts the rows' span: "+r.badge());
     r.stop("f11");
     check(!r.recording(),"stopped");
     r.push(row(1120,120*16667));
     check(r.pushed()==120,"a push after the stop is not a row");
     check(saved_within(r),"the saver finished");
     check(r.state()==OrdinaryRecorder::State::Complete&&r.why().empty(),"complete: "+r.why());
     const auto capture=a.request.recording/"capture";
     std::size_t entries=0;for(const auto& e:fs::directory_iterator(capture)){(void)e;++entries;}
     check(entries==7,"the capture holds the six members and recorder-complete.json");
     const auto proof=slurp(capture/"recorder-complete.json");
     check(has(proof,"\"schema\":\"outrun.recorder-complete@1\"")&&has(proof,"\"requestId\":\""+a.request.request_id+"\"")&&
           has(proof,"\"stoppedBy\":\"f11\"")&&has(proof,"\"written\":120")&&has(proof,"\"afterEnd\":0")&&has(proof,"\"missedFinal\":0"),"the proof's fields");
     check(has(proof,"\"artifactSha256\":\""+digest_of(capture)+"\""),"artifactSha256 is the six sorted member lines' digest");
     check(has(proof,"\"exeSha256\":\""+Exe+"\"")&&has(proof,"\"profileSha256\":\""+dbce::shim::hex(dbce::shim::sha256(Profile))+"\""),"the exe and profile bound");
     check(has(proof,"\"budget\":{\"hz\":60,\"seconds\":300,\"maxBytes\":536870912,\"slots\":510}"),"the budget is the recorder's own settings");
     check(slurp(capture/"bundle.meta")==CaptureContract2,"contract @2");
     check(has(slurp(capture/"source.jsonl"),"final.steering")&&has(slurp(capture/"source.jsonl"),"keyboard.drives"),"the rows carry final.* and keyboard.*");
     // The host's stop: the cached result, the same however often it asks (the drain ran once, on the saver).
     for(int i=0;i<2;++i){const auto d=host_copy.drain(Soon());check(d.outcome==dbce::shim::hosting::RecOutcome::Complete&&d.joined,"the host's stop reads the cached Complete");}
     r.publish_status();
     check(has(slurp(a.request.result/"status.json"),"\"state\":\"complete\""),"status.json says complete");
     check(r.badge()=="REC complete, verifying","complete is not saved: "+r.badge());
     // The receipt: absent, then wrong, then right.
     check(!r.observe_receipt(),"no receipt: not saved");
     const auto artifact=digest_of(capture);
     spit(capture/"recording-complete.tsv","the managed seal\n");
     const auto seal=dbce::shim::hex(dbce::shim::sha256(slurp(capture/"recording-complete.tsv")));
     const auto pid=std::to_string(GetCurrentProcessId());
     spit(a.request.result/"recording-verified.json",receipt(a,artifact,seal,"1"));
     check(!r.observe_receipt(),"another process's receipt: not saved");
     spit(a.request.result/"recording-verified.json",receipt(a,artifact,std::string(64,'0'),pid));
     check(!r.observe_receipt(),"a receipt naming another seal: not saved");
     spit(a.request.result/"recording-verified.json",receipt(a,std::string(64,'1'),seal,pid));
     check(!r.observe_receipt(),"a receipt naming another artifact: not saved");
     spit(a.request.result/"recording-verified.json",receipt(a,artifact,seal,pid,",\"nested\":{\"a\":1}"));
     check(!r.observe_receipt(),"a nested receipt: not saved");
     spit(a.request.result/"recording-verified.json",receipt(a,artifact,seal,pid));
     check(r.observe_receipt()&&r.saved()&&r.badge()=="REC saved","the receipt naming this request, process, artifact and seal: saved");
     r.publish_status();
     check(has(slurp(a.request.result/"status.json"),"\"state\":\"saved\""),"status.json says saved");}

    // No saver thread (it could not start): the stop throws nothing, the one drain runs inline at the exit's wait, and
    // the host's stop reads its result; without the wait, the host's stop runs it.
    {auto a=arm();a.saver_thread=false;OrdinaryRecorder r(a);r.set_devices(devices());
     for(std::uint64_t i=0;i<10;++i)r.push(row(i+1,std::int64_t(i)*16667));
     r.stop("f11");
     check(r.state()==OrdinaryRecorder::State::Saving,"no saver thread: still saving after the stop");
     check(saved_within(r)&&r.state()==OrdinaryRecorder::State::Complete,"the exit's wait ran the drain inline: "+r.why());
     const auto d=r.drain(Soon());check(d.outcome==dbce::shim::hosting::RecOutcome::Complete&&d.joined,"the host's stop reads it");}
    {auto a=arm();a.saver_thread=false;OrdinaryRecorder r(a);r.set_devices(devices());
     for(std::uint64_t i=0;i<10;++i)r.push(row(i+1,std::int64_t(i)*16667));
     const auto d=r.drain(dbce::shim::hosting::Clock::now()+std::chrono::seconds(30));
     check(d.outcome==dbce::shim::hosting::RecOutcome::Complete&&d.joined&&r.state()==OrdinaryRecorder::State::Complete,
           "no saver thread and no wait: the host's stop runs the one drain");
     check(has(slurp(a.request.recording/"capture"/"recorder-complete.json"),"\"stoppedBy\":\"exit\""),"stopped by the exit");}

    // A tick that never reached the consumed-input seam: failed, no proof, and the host's stop is not Complete.
    {const auto a=arm();OrdinaryRecorder r(a);r.set_devices(devices());
     for(std::uint64_t i=0;i<10;++i)r.push(row(i+1,std::int64_t(i)*16667));
     r.count_missed_final();r.stop("exit");
     check(saved_within(r)&&r.state()==OrdinaryRecorder::State::Failed&&has(r.why(),"missed"),"a missed seam fails: "+r.why());
     check(!fs::exists(a.request.recording/"capture"/"recorder-complete.json"),"no proof for a failed capture");
     check(r.drain(Soon()).outcome!=dbce::shim::hosting::RecOutcome::Complete,"the host's stop is not Complete");
     check(has(r.badge(),"REC failed"),"the badge says failed");}

    // A gap in the frames: the worker refuses it, the capture fails.
    {const auto a=arm();OrdinaryRecorder r(a);r.set_devices(devices());
     r.push(row(1,0));r.push(row(2,16667));r.push(row(4,50000));r.push(row(5,66667));
     r.stop("f11");
     check(saved_within(r)&&r.state()==OrdinaryRecorder::State::Failed,"a frame gap fails: "+r.why());
     check(!fs::exists(a.request.recording/"capture"/"recorder-complete.json"),"no proof after a gap");}

    // A stray file in the capture: the admission refuses it.
    {const auto a=arm();OrdinaryRecorder r(a);r.set_devices(devices());
     for(std::uint64_t i=0;i<5;++i)r.push(row(i+1,std::int64_t(i)*16667));
     spit(a.request.recording/"capture"/"stray.txt","x");
     r.stop("stop-request");
     check(saved_within(r)&&r.state()==OrdinaryRecorder::State::Failed&&has(r.why(),"stray.txt"),"an unexpected member fails: "+r.why());
     check(!fs::exists(a.request.recording/"capture"/"recorder-complete.json"),"no proof beside a stray file");}

    // A capture folder already there: the first row is refused, nothing completes.
    // The recorder's refusal stops the recording at once (sana 33815): no REC 0:00 until F11; the folder is left as it was.
    {const auto a=arm();fs::create_directories(a.request.recording/"capture");spit(a.request.recording/"capture"/"earlier.txt","kept");
     OrdinaryRecorder r(a);r.set_devices(devices());
     r.push(row(1,0));
     check(!r.recording()&&r.state()!=OrdinaryRecorder::State::Recording,"a refused first row stops the recording without F11");
     check(saved_within(r)&&r.state()==OrdinaryRecorder::State::Failed&&has(r.why(),"exists"),"an existing capture folder fails: "+r.why());
     check(has(r.badge(),"REC failed"),"the badge says failed, not REC 0:00");
     r.stop("f11");r.push(row(2,16667));
     check(slurp(a.request.recording/"capture"/"earlier.txt")=="kept"&&!fs::exists(a.request.recording/"capture"/"recorder-complete.json"),
           "the existing folder untouched, never a proof");}
    // No device table: the recorder refuses its start, and the recording stops at once.
    {const auto a=arm();OrdinaryRecorder r(a);
     r.push(row(1,0));
     check(!r.recording()&&saved_within(r)&&r.state()==OrdinaryRecorder::State::Failed,"a refused start stops at once: "+r.why());}

    // No row at all (stopped at once): failed, not an empty complete capture.
    {const auto a=arm();OrdinaryRecorder r(a);r.set_devices(devices());r.stop("f11");
     check(saved_within(r)&&r.state()==OrdinaryRecorder::State::Failed,"a capture without rows fails: "+r.why());}

    // No shim host: failed before any capture; the host's stop never waits.
    {const auto a=arm();OrdinaryRecorder r(a);r.fail("the shared input shim did not start");
     check(r.state()==OrdinaryRecorder::State::Failed&&saved_within(r)&&!r.recording(),"fail() before a capture");
     r.push(row(1,0));check(r.pushed()==0,"no row after fail()");}

    // The limits: rows (hz x seconds), and the rows' span one second short of the duration.
    {const auto a=arm(10,2);OrdinaryRecorder r(a);r.set_devices(devices());
     for(std::uint64_t i=0;i<19;++i)r.push(row(i+1,std::int64_t(i)*1000));
     check(!r.at_limit(),"19 of 20 rows: under the limit");
     r.push(row(20,19000));check(r.at_limit(),"20 rows: the row limit");
     r.stop("limit");check(saved_within(r)&&r.state()==OrdinaryRecorder::State::Complete,"a capture stopped at its limit completes: "+r.why());
     check(has(slurp(a.request.recording/"capture"/"recorder-complete.json"),"\"stoppedBy\":\"limit\""),"stopped by the limit");}
    {const auto a=arm(60,10);OrdinaryRecorder r(a);r.set_devices(devices());
     r.push(row(1,0));r.push(row(2,8'000'000));check(!r.at_limit(),"8 s of 10: under");
     r.push(row(3,9'000'000));check(r.at_limit(),"9 s of 10: the duration limit");
     r.stop("limit");(void)saved_within(r);}

    // The receipt reader.
    {check(flat_json("{}")&&flat_json("{}")->empty(),"an empty object");
     const auto f=flat_json(" {\"a\":\"x\\\"y\\\\z\\u0041\",\"b\":-12,\"c\":true,\"d\":false}\r\n");
     check(f&&f->at("a")=="x\"y\\zA"&&f->at("b")=="-12"&&f->at("c")=="true"&&f->at("d")=="false","strings, escapes, integers and booleans");
     for(const char* bad:{"","[]","{\"a\":1,\"a\":2}","{\"a\":{}}","{\"a\":[1]}","{\"a\":1.5}","{\"a\":null}","{\"a\":01}","{\"a\":1}x",
                          "{\"a\":\"\\u00e9\"}","{\"a\":1,}","{\"a\" 1}","{\"a\":\"open}"})
        check(!flat_json(bad),std::string("refused: ")+bad);}

    // The start's intake of a request file.
    {const auto dir=Root/"intake";fs::create_directories(dir);
     const auto game=dir/"game";fs::create_directories(game);
     const auto file=dir/"session-request.txt";
     const auto now=std::uint64_t(std::time(nullptr));
     const auto request=[&](const char* action,const std::string& id,const char* extra=""){
        return std::string(session::RequestSchema)+"\nrequestId="+id+"\naction="+action+"\nexpiresUnix="+std::to_string(now+300)+
               "\nresult="+session::utf8(dir/("result-"+id))+"\nrecording="+session::utf8(dir/("recording-"+id))+"\n"+extra;
     };
     check(session::take(file,game,true,now).kind==session::Intake::Kind::None,"no request: ordinary play");
     const auto refusals=[&](const fs::path& d){int n=0;const auto p="session-request.txt.refused-"+std::to_string(GetCurrentProcessId())+"-";
        for(const auto& e:fs::directory_iterator(d))if(session::utf8(e.path().filename()).rfind(p,0)==0)++n;return n;};
     const auto claims=[&](const fs::path& d){int n=0;for(const auto& e:fs::directory_iterator(d))if(session::utf8(e.path().filename()).rfind("session-request.txt.taken-",0)==0)++n;return n;};
     const std::string id1(32,'1');spit(file,request("record",id1));
     auto t=session::take(file,game,true,now);
     check(t.kind==session::Intake::Kind::Record&&t.request.request_id==id1&&t.exe_sha256.size()==64,"a record request admitted: "+t.why);
     check(!fs::exists(file)&&slurp(dir/("result-"+id1)/"session-request.txt")==request("record",id1)&&fs::is_directory(dir/("recording-"+id1))&&
           !fs::exists(dir/("recording-"+id1)/"capture"),"consumed: its exact bytes in its result folder; the recording folder made, no capture yet");
     check(claims(dir)==0,"the claim is removed once the copy is in place");
     const std::string id2(32,'2');spit(file,request("record",id2));
     t=session::take(file,game,false,now);
     check(t.kind==session::Intake::Kind::Refused&&has(t.why,"Backend = Shim"),"the native backend refuses a record request: "+t.why);
     check(!fs::exists(file)&&has(slurp(dir/("result-"+id2)/"status.json"),"\"state\":\"failed\""),"consumed, status failed");
     const std::string id3(32,'3');spit(file,request("playback",id3,("packageSealSha256="+std::string(64,'a')+"\ntape="+session::utf8(dir/"t.tsv")+"\n").c_str()));
     t=session::take(file,game,true,now);
     check(t.kind==session::Intake::Kind::Exit&&has(slurp(dir/("result-"+id3)/"status.json"),"\"state\":\"failed\""),"a playback request: exit, status failed");
     spit(file,"garbage");
     t=session::take(file,game,true,now);
     check(t.kind==session::Intake::Kind::Exit&&!fs::exists(file)&&
           refusals(dir)==1,"an unreadable request: exit, kept as .refused-<pid>-<n>");
     const std::string id4(32,'4');spit(file,request("record",id4));
     t=session::take(file,game,true,now+1000);
     check(t.kind==session::Intake::Kind::Refused&&!fs::exists(file)&&!fs::exists(dir/("result-"+id4)),"an expired record request: refused, set aside, no folder made");
     const std::string id5(32,'5');fs::create_directories(dir/("recording-"+id5)/"capture");spit(file,request("record",id5));
     t=session::take(file,game,true,now);
     check(t.kind==session::Intake::Kind::Refused&&has(t.why,"already holds a capture"),"a recording folder with a capture: refused: "+t.why);
     const std::string id6(32,'6');fs::create_directories(dir/("result-"+id6));spit(dir/("result-"+id6)/"session-request.txt","earlier");
     spit(dir/("result-"+id6)/"status.json","another session's status");
     spit(file,request("record",id6));
     t=session::take(file,game,true,now);
     check(t.kind==session::Intake::Kind::Refused&&has(t.why,"another session")&&slurp(dir/("result-"+id6)/"session-request.txt")=="earlier"&&!fs::exists(file),
           "an earlier request in the result folder is never replaced: "+t.why);
     check(slurp(dir/("result-"+id6)/"status.json")=="another session's status","and that session's status.json is never written over");
     check(claims(dir)==0,"a refused claim is kept as .refused, never left under the claim name");
     check(refusals(dir)==3,"every refusal kept under its own name (unreadable, expired, another session's folder)");}

    // The request on one volume (%TEMP%) and its result and recording folders on another (the 12:56 pilot: C: to E:).
    if(!cross.empty()){
        wchar_t a[MAX_PATH]{},b[MAX_PATH]{};
        fs::create_directories(cross);
        check(GetVolumePathNameW(Root.c_str(),a,MAX_PATH)&&GetVolumePathNameW(cross.c_str(),b,MAX_PATH),"the two volumes resolved");
        const bool other=_wcsicmp(a,b)!=0;
        std::printf("ordinary recording: cross-volume: request on %ls, folders on %ls%s\n",a,b,other?"":" (THE SAME VOLUME: not a cross-volume check)");
        check(other,"--cross-volume names a folder on another volume than %TEMP%");
        const auto dir=Root/"cross";fs::create_directories(dir);fs::create_directories(dir/"game");
        const auto here=cross/("ordinary-recording-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        fs::create_directories(here);
        const auto file=dir/"session-request.txt";const auto now=std::uint64_t(std::time(nullptr));
        const std::string id(32,'c');
        const auto text=std::string(session::RequestSchema)+"\r\nrequestId="+id+"\r\naction=record\r\nexpiresUnix="+std::to_string(now+300)+
            "\r\nresult="+session::utf8(here/"result")+"\r\nrecording="+session::utf8(here/"recording")+"\r\n";
        spit(file,text);
        const auto t=session::take(file,dir/"game",true,now);
        check(t.kind==session::Intake::Kind::Record,"cross-volume: a record request admitted: "+t.why);
        check(slurp(here/"result"/"session-request.txt")==text&&!fs::exists(file),"cross-volume: its exact bytes in the result folder, none left to take");
        int left=0;for(const auto& e:fs::directory_iterator(dir))if(session::utf8(e.path().filename()).rfind("session-request.txt",0)==0)++left;
        check(left==0,"cross-volume: no claim or copy left beside the request");
        // The whole recording on the other volume too: one real capture, complete.
        OrdinaryRecorder::Arm a2;a2.request=t.request;a2.profile=Profile;a2.exe_sha256=t.exe_sha256;a2.adapter_version="cross-volume";
        OrdinaryRecorder r(a2);r.set_devices(devices());
        for(std::uint64_t i=0;i<30;++i)r.push(row(i+1,std::int64_t(i)*16667));
        r.stop("f11");
        check(saved_within(r)&&r.state()==OrdinaryRecorder::State::Complete,"cross-volume: the capture completes: "+r.why());
        std::error_code e2;fs::remove_all(here,e2);   // this test's own folder on the other volume only
    }

    std::error_code ec;fs::remove_all(Root,ec);   // the test's own folder only
    std::printf("ordinary recording: %d checks passed\n",checks);
    return 0;
}
