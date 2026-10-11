// R3 tape (source/outrun_replay.hpp) on synthetic latched frames through the real adapter, codec and export reader.
// A first run's export is written in the finalizer's format; the tape keeps its race rows with the run Running; a second
// run's live frames are neutral (latched, nothing injected), and played ticks must give the adapter words the first run
// gave. No game, device or Core worker.
#include "outrun_replay.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace dbce::shim;
using namespace outrun::desktop;
using outrun::platform::NativeInputState;
namespace rec=dbce::shim::recorder;

namespace {
int checks=0;
std::filesystem::path keep;   // optional: both exports kept here for compare_replay.py
void check(bool v,const std::string& what){++checks;if(!v)throw std::runtime_error(what);}
std::filesystem::path scratch(){
    static const auto dir=std::filesystem::temp_directory_path()/("outrun-replay-test-"+std::to_string(GetCurrentProcessId())+"-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);return dir;
}
// The finalizer's export: "ROW <n>", then "<channel> <round-trip value>" per channel, keys in ordinal order.
void write_export(const std::filesystem::path& p,const std::vector<rec::Channels>& rows){
    std::ofstream f(p,std::ios::binary);f.imbue(std::locale::classic());f<<std::setprecision(17);
    for(const auto& r:rows){f<<"ROW "<<r.size()<<"\n";for(const auto& v:r)f<<v.first<<" "<<v.second<<"\n";}
}
// A latched frame: every action TestNeutral unless the caller injects it.
Frame latched(std::uint64_t seq,Run run){
    Frame f;f.seq=seq;f.tick_us=std::int64_t(seq)*16667;f.boundary_us=f.tick_us;f.run=run;f.latched=true;f.suppress_native=true;
    for(auto& a:f.action)a.status=Status::TestNeutral;
    return f;
}
void inject(Frame& f,const char* id,float value){auto& a=f.action[std::size_t(action_index(id))];a.status=Status::Live;a.injected=true;a.value=value;}
GameView view(std::uint32_t mode){GameView v;v.mode=mode;v.car=mode==16u||mode==18u;v.gear=2;return v;}

struct First { std::vector<rec::Channels> rows; std::vector<Signals> race; };
// Run 1: waiting, menus with presses, a race with steer/throttle/shift, then the session completes in the race.
First first_run(){
    First r;Adapter adapter("Sequential");
    for(std::uint64_t i=0;i<20;++i){
        const Run run=i<2?Run::Waiting:i<14?Run::Running:Run::Completed;
        auto f=latched(100+i,run);const auto v=view(i<5?3u:16u);
        if(run==Run::Running&&i<5)inject(f,"confirm",i==3?1.f:0.f);
        if(run==Run::Running&&i>=5){inject(f,"steer",i<9?0.6f:-0.6f);inject(f,"throttle",1.f);inject(f,"brake",i==11?0.5f:0.f);}
        if(i==7){inject(f,"shiftUp",1.f);f.down|=1ull<<action_index("shiftUp");}
        NativeInputState sink{};adapter.apply(f,v,sink);
        r.rows.push_back(channels(Snapshot{f,adapter.signals(),sink,v}));
        if(run==Run::Running&&v.car)r.race.push_back(adapter.signals());
    }
    return r;
}

void test_selection_and_playback(){
    const auto one=first_run();const auto path=scratch()/"first.tsv";write_export(path,one.rows);
    std::vector<rec::Channels> second;   // the replay run's capture rows
    Tape tape(path);
    check(tape.source_rows()==20,"every exported row read");
    check(tape.size()==9&&tape.row(0).seq==105&&tape.row(8).seq==113,"tape keeps the race rows with the run Running, through the last one");
    for(std::size_t i=0;i<tape.size();++i)for(const auto& a:tape.row(i).action)check(a.sample==-1,"tape actions carry no sample index");
    Adapter adapter("Sequential");std::uint64_t seq=500;std::int64_t row=0;
    // Menus in the second run: live frames reach the adapter, nothing is played.
    for(int i=0;i<4;++i){
        const auto live=latched(seq++,Run::Running);const auto& f=tape.play(live,false,row);
        check(&f==&live&&row==-1&&!tape.started(),"no playback before the live race entry");
        NativeInputState sink{};adapter.apply(f,view(3),sink);second.push_back(channels(Snapshot{f,adapter.signals(),sink,view(3),row}));
    }
    // A race tick while the live run is not Running does not start the tape.
    {const auto live=latched(seq++,Run::Waiting);const auto& f=tape.play(live,true,row);check(&f==&live&&row==-1&&!tape.started(),"no playback before the live run is Running");}
    for(std::size_t k=0;k<tape.size();++k){
        const auto live=latched(seq++,Run::Running);const auto& f=tape.play(live,true,row);
        check(tape.started()&&row==std::int64_t(k),"one tape row per live race tick");
        check(f.seq==live.seq&&f.run==Run::Running&&f.suppress_native,"a played frame keeps the live frame's identity, run and suppression");
        NativeInputState sink{};adapter.apply(f,view(16),sink);second.push_back(channels(Snapshot{f,adapter.signals(),sink,view(16),row}));
        const auto& want=one.race[k];const auto& got=adapter.signals();
        check(got.record_steer==want.record_steer&&got.record_accel==want.record_accel&&got.record_brake==want.record_brake&&
              got.record_buttons==want.record_buttons,"played tick gives the first run's adapter words");
        if(k==0){   // the capture row of a played tick decodes, and names its tape row
            const auto c=channels(Snapshot{f,adapter.signals(),sink,view(16),row});
            check(c.at("replay.tapeRow")==0.0,"capture names the tape row");
            const auto back=rec::decode(frame_channels(c));
            check(back.action[std::size_t(action_index("steer"))].value==f.action[std::size_t(action_index("steer"))].value,"played row decodes");
        }
    }
    check(tape.ended()&&tape.played()==tape.size()&&tape.end_reason()=="tape complete","the whole tape played");
    const auto live=latched(seq++,Run::Running);const auto& f=tape.play(live,true,row);
    check(&f==&live&&row==-1,"after the tape the live (neutral) frame");
    NativeInputState sink{};adapter.apply(f,view(16),sink);second.push_back(channels(Snapshot{f,adapter.signals(),sink,view(16),row}));
    check(adapter.signals().record_steer==0&&adapter.signals().record_accel==0,"neutral after the tape");
    if(!keep.empty()){std::filesystem::create_directories(keep);write_export(keep/"first.tsv",one.rows);write_export(keep/"replay.tsv",second);}
}

void test_live_run_ends_playback(){
    const auto path=scratch()/"first-2.tsv";write_export(path,first_run().rows);
    Tape tape(path);std::int64_t row=0;std::uint64_t seq=900;
    for(int k=0;k<3;++k){const auto live=latched(seq++,Run::Running);(void)tape.play(live,true,row);}
    check(row==2,"three rows played");
    const auto expired=latched(seq++,Run::Expired);const auto& f=tape.play(expired,true,row);
    check(&f==&expired&&row==-1&&tape.ended()&&tape.end_reason()=="live run expired","a live run that leaves Running ends playback");
    const auto again=latched(seq++,Run::Running);const auto& g=tape.play(again,true,row);
    check(&g==&again&&row==-1&&tape.played()==3,"an ended tape never resumes");
}

// Astra 16889: once started, a live tick outside the race (run still Running) ends playback and the output is neutral.
void test_leaving_the_race_ends_playback(){
    const auto path=scratch()/"first-3.tsv";write_export(path,first_run().rows);
    Tape tape(path);std::int64_t row=0;std::uint64_t seq=1200;Adapter adapter("Sequential");
    {const auto live=latched(seq++,Run::Running);const auto& f=tape.play(live,true,row);NativeInputState s{};adapter.apply(f,view(16),s);
     check(row==0&&adapter.signals().record_steer!=0,"one race row played, steering");}
    const auto menu=latched(seq++,Run::Running);const auto& f=tape.play(menu,false,row);
    check(&f==&menu&&row==-1&&tape.ended()&&tape.end_reason()=="left the race"&&tape.played()==1,"leaving the race ends playback");
    NativeInputState s{};adapter.apply(f,view(3),s);
    check(adapter.signals().record_steer==0&&adapter.signals().record_accel==0,"neutral once the race is left");
    const auto back=latched(seq++,Run::Running);const auto& g=tape.play(back,true,row);
    check(&g==&back&&row==-1&&tape.played()==1,"a left race never resumes playback");
}

void test_refusals(){
    std::vector<rec::Channels> menus;Adapter adapter("Sequential");
    for(std::uint64_t i=0;i<4;++i){const auto f=latched(10+i,Run::Running);const auto v=view(3);NativeInputState s{};adapter.apply(f,v,s);
        menus.push_back(channels(Snapshot{f,adapter.signals(),s,v}));}
    const auto none=scratch()/"menus.tsv";write_export(none,menus);
    bool refused=false;try{Tape t(none);}catch(const std::exception&){refused=true;}
    check(refused,"a tape without race rows with the run Running is refused");
    const auto bad=scratch()/"bad.tsv";{std::ofstream f(bad,std::ios::binary);f<<"ROW 2\nframe.lo 1\n";}
    refused=false;try{Tape t(bad);}catch(const std::exception&){refused=true;}
    check(refused,"a malformed export is refused");
    refused=false;try{Tape t(scratch()/"absent.tsv");}catch(const std::exception&){refused=true;}
    check(refused,"a missing export is refused");
}
}

int main(int argc,char** argv){
    if(argc==2)keep=argv[1];
    try{test_selection_and_playback();test_live_run_ends_playback();test_leaving_the_race_ends_playback();test_refusals();}
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    std::error_code ec;std::filesystem::remove_all(scratch(),ec);
    std::cout<<"outrun replay tape: "<<checks<<" checks passed\n";
    return 0;
}
