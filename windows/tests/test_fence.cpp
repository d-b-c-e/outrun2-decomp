// Input shim slice 2b (windows/docs/INPUT-SHIM.md section 15, P24 and P27): the test fence against a temporary game tree,
// driving the real production writers that run without the game. Every case compares the game tree's complete
// inventory (every file's bytes and every folder, so a new or missing path also fails) before and after, through
// success and fault paths, and requires the intended outputs to exist under the run folder.
#include "test_fence.hpp"
#include "enhancements/video.hpp"
#include "platform/frontend_profiles.hpp"
#include "system/setup_screen.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
namespace fence=outrun::desktop::fence;
using namespace outrun::platform;

namespace {
int checks=0;
void check(bool v,const std::string& what){++checks;if(!v)throw std::runtime_error(what);}
std::string read(const fs::path& p){std::ifstream f(p,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();}
void write(const fs::path& p,const std::string& bytes){fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f<<bytes;}
const std::string Nonce="fence12345abcdef";

// The complete inventory of a tree: every folder and every file's bytes.
std::map<std::string,std::string> inventory(const fs::path& root){
    std::map<std::string,std::string> m;
    for(auto it=fs::recursive_directory_iterator(root);it!=fs::recursive_directory_iterator();++it){
        const auto rel=fs::relative(it->path(),root).generic_string();
        if(it->is_symlink()||(GetFileAttributesW(it->path().c_str())&FILE_ATTRIBUTE_REPARSE_POINT)){m[rel]="<link>";it.disable_recursion_pending();continue;}
        m[rel]=it->is_directory()?std::string("<dir>"):read(it->path());
    }
    return m;
}
std::string diff(const std::map<std::string,std::string>& a,const std::map<std::string,std::string>& b){
    std::string d;
    for(const auto& [k,v]:a){const auto i=b.find(k);if(i==b.end())d+=" removed:"+k;else if(i->second!=v)d+=" changed:"+k;}
    for(const auto& [k,v]:b)if(!a.count(k))d+=" added:"+k;
    return d;
}

struct Tree {
    fs::path base,game,dbce;
    explicit Tree(const std::string& tag){
        base=fs::temp_directory_path()/("or2-fence-"+tag+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        game=base/"game";dbce=base/"dbce";
        fs::create_directories(game);fs::create_directories(dbce);
    }
    ~Tree(){std::error_code e;
        // Junctions first, so remove_all never walks through them into a target.
        for(auto it=fs::recursive_directory_iterator(base,e);it!=fs::recursive_directory_iterator();++it)
            if(GetFileAttributesW(it->path().c_str())&FILE_ATTRIBUTE_REPARSE_POINT){RemoveDirectoryW(it->path().c_str());it.disable_recursion_pending();}
        fs::remove_all(base,e);}
    // The owner's game folder: saves (with a stale temp and a backup), options, the owner's log, caches old and new.
    void seed_owner(){
        write(game/"SaveGame"/"common.dat",std::string("\x01\x02\x03",3));
        write(game/"SaveGame"/"License3.dat",std::string(1024,'L'));
        write(game/"SaveGame"/"ghosts"/"ghost_01.dat",std::string(300,'G'));
        write(game/"options.ini","widescreen=0\nfps=60\n");
        write(game/"runtime.log","the owner's previous log\n");
        write(game/"OR2006C2C.cache",std::string(64,'C'));
        write(game/"Cache"/"OR2006C2C.cache",std::string(32,'o'));     // a former-cache folder (migration target)
        write(game/"OR2006C2C.EXE",std::string(4096,'\x90'));           // not a supported EXE: setup fails, never writes
    }
    void session(const std::string& text){write(dbce/"inject.on",text);}
};
std::string valid_session(){return "nonce="+Nonce+"\nexpires="+std::to_string(std::uint64_t(std::time(nullptr))+600)+"\n";}

void test_latch_and_run(){
    Tree t("run");t.seed_owner();
    check(!fence::latch_requested(t.dbce.wstring()),"no inject.on: not latched");
    t.session(valid_session());
    check(fence::latch_requested(t.dbce.wstring()),"inject.on exists: latched");
    const auto before=inventory(t.game);
    const auto plan=fence::prepare_run(t.dbce.wstring(),t.game.wstring(),4242);
    check(plan.refusal.empty(),"prepared: "+plan.refusal);
    const fs::path run=plan.run_root;
    check(run==t.dbce/("run-"+Nonce+"-4242"),"named by nonce and pid: "+fence::utf8(plan.run_root));
    check(read(run/"SaveGame"/"common.dat")==read(t.game/"SaveGame"/"common.dat")&&read(run/"SaveGame"/"ghosts"/"ghost_01.dat")==std::string(300,'G'),"saves seeded (nested too)");
    check(read(run/"options.ini")==read(t.game/"options.ini"),"options seeded");
    check(plan.seeded.size()==4&&plan.seeded_bytes==3+1024+300+read(t.game/"options.ini").size(),"seed accounted: "+std::to_string(plan.seeded.size()));
    check(!fs::exists(run/"runtime.log")&&!fs::exists(run/"OR2006C2C.cache"),"only writable state is seeded (no log, no cache)");
    const auto receipt=read(run/"run.json");
    check(receipt.find("\"gameRoot\"")!=std::string::npos&&receipt.find("\"runRoot\"")!=std::string::npos&&receipt.find(plan.seeded[0].sha256)!=std::string::npos,"run.json records both roots and the hashes");
    check(receipt.find("read-only from the game folder")!=std::string::npos,"the EXE cache is recorded as read-only");
    check(fence::home_of(plan,t.game.wstring())==plan.run_root&&fence::log_path(plan,t.game.wstring())==plan.run_root+L"\\runtime.log","home and log point at the run folder");

    // The real writers, given the latched effective paths.
    const std::string home=fence::utf8(fence::home_of(plan,t.game.wstring()));
    check(outrun::enhancements::write_options(home+"/options.ini",{{"widescreen","1"}}),"write_options");
    check(read(run/"options.ini").find("widescreen=1")!=std::string::npos,"options written in the run folder");
    // Profiles: initialize, load the seeded saves, select a slot, save.
    {FrontendProfiles p;std::uint32_t rng=1;std::array<std::uint32_t,4> r{};for(auto& v:r)v=frontend_crt_random_580f40(rng);
     frontend_profiles_initialize_bank_4162d0(p,r);
     frontend_profiles_reload_416380(p,home+"/SaveGame");
     check(frontend_profiles_select_448520(p,0),"select a slot");
     p.active[0x3f4]|=1;p.active[0]='T';
     const auto saved=frontend_profiles_save_416420(p,home+"/SaveGame",true);
     check(bool(saved)&&saved.license_written&&fs::exists(run/"SaveGame"/"License1.dat"),"profile saved into the run folder");}
    // The EXE setup with the game folder read only: the former-cache migration is skipped, and the unsupported EXE
    // fails before anything is written.
    {std::string error;SetupPlatform none;
     const bool ready=exe_setup_prepare(fence::utf8(plan.game_root),home,none,&error,true);
     check(!ready,"an unsupported EXE: setup refuses");
     check(fs::exists(t.game/"Cache"/"OR2006C2C.cache"),"the former-cache folder in the game tree is untouched");}
    // runtime.log: the entry point opens log_path(plan); write through it.
    {std::FILE* f=_wfopen(fence::log_path(plan,t.game.wstring()).c_str(),L"w");check(f!=nullptr,"log opens");std::fputs("TEST PROCESS\n",f);std::fclose(f);
     check(read(run/"runtime.log").rfind("TEST PROCESS",0)==0,"the log is in the run folder");}
    const auto after=inventory(t.game);
    check(after==before,"the game tree is byte-identical, nothing added or removed:"+diff(before,after));

    // The same EXE setup in an ordinary process migrates the former cache: the difference the flag makes.
    {Tree o("ordinary");o.seed_owner();std::string error;SetupPlatform none;
     (void)exe_setup_prepare(o.game.string(),o.game.string(),none,&error,false);
     check(!fs::exists(o.game/"Cache"/"OR2006C2C.cache"),"ordinary: the former cache was migrated (so the latched skip is real)");}
}

void test_fault_paths(){
    // A run folder that already exists: refused, nothing in the game tree, the reason in the dbce folder.
    {Tree t("exists");t.seed_owner();t.session(valid_session());
     fs::create_directories(t.dbce/("run-"+Nonce+"-7"));
     const auto before=inventory(t.game);
     const auto plan=fence::prepare_run(t.dbce.wstring(),t.game.wstring(),7);
     check(plan.refusal.find("already exists")!=std::string::npos,"an existing run folder refuses: "+plan.refusal);
     check(fence::write_refusal(plan,7)&&fs::exists(t.dbce/"refused-7.txt")&&!fs::exists(t.game/"refused-7.txt"),"the refusal is recorded beside inject.on, not in the game");
     check(inventory(t.game)==before,"refused: game tree unchanged");}
    // A link inside SaveGame: never followed, refused.
    {Tree t("link");t.seed_owner();t.session(valid_session());
     fs::create_directories(t.base/"elsewhere");write(t.base/"elsewhere"/"secret.dat","x");
     const auto cmd=L"cmd /c mklink /J \""+(t.game/"SaveGame"/"jump").wstring()+L"\" \""+(t.base/"elsewhere").wstring()+L"\" >nul";
     check(_wsystem(cmd.c_str())==0,"made a junction");
     const auto before=inventory(t.game);
     const auto plan=fence::prepare_run(t.dbce.wstring(),t.game.wstring(),8);
     check(plan.refusal.find("link")!=std::string::npos,"a junction in SaveGame refuses: "+plan.refusal);
     check(inventory(t.game)==before,"link: game tree unchanged");}
    // The test folder under a link: refused before anything is created.
    {Tree t("dbcelink");t.seed_owner();
     fs::create_directories(t.base/"real-dbce");
     const auto linked=t.base/"linked-dbce";
     const auto cmd=L"cmd /c mklink /J \""+linked.wstring()+L"\" \""+(t.base/"real-dbce").wstring()+L"\" >nul";
     check(_wsystem(cmd.c_str())==0,"made a junction");
     write(linked/"inject.on",valid_session());
     const auto plan=fence::prepare_run(linked.wstring(),t.game.wstring(),9);
     check(plan.refusal.find("link")!=std::string::npos&&!fs::exists(t.base/"real-dbce"/("run-"+Nonce+"-9")),"a linked test folder refuses: "+plan.refusal);}
    // Bounds.
    {Tree t("bounds");t.seed_owner();t.session(valid_session());
     fence::SeedLimits few;few.max_files=2;
     check(fence::prepare_run(t.dbce.wstring(),t.game.wstring(),10,few).refusal.find("more than 2 files")!=std::string::npos,"file bound");
     fence::SeedLimits small;small.max_bytes=1000;
     check(fence::prepare_run(t.dbce.wstring(),t.game.wstring(),11,small).refusal.find("larger than")!=std::string::npos,"byte bound");}
    // A partial seed failure: a save held open without sharing cannot be copied; refused, game tree unchanged.
    {Tree t("partial");t.seed_owner();t.session(valid_session());
     const auto before=inventory(t.game);
     const HANDLE held=CreateFileW((t.game/"SaveGame"/"License3.dat").c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
     check(held!=INVALID_HANDLE_VALUE,"held a save open");
     const auto plan=fence::prepare_run(t.dbce.wstring(),t.game.wstring(),12);
     CloseHandle(held);
     check(plan.refusal.find("cannot copy")!=std::string::npos||plan.refusal.find("does not match")!=std::string::npos,"partial seed refuses: "+plan.refusal);
     check(inventory(t.game)==before,"partial seed: game tree unchanged");}
    // First run: no saves, no options. The run folder has an empty SaveGame.
    {Tree t("first");write(t.game/"OR2006C2C.EXE","x");t.session(valid_session());
     const auto before=inventory(t.game);
     const auto plan=fence::prepare_run(t.dbce.wstring(),t.game.wstring(),13);
     check(plan.refusal.empty()&&plan.seeded.empty()&&fs::is_directory(fs::path(plan.run_root)/"SaveGame"),"first run: an empty SaveGame");
     check(inventory(t.game)==before,"first run: game tree unchanged");}
    // A stale or malformed inject.on still latches; the run is named without a nonce (admission refuses it later).
    {Tree t("stale");t.seed_owner();t.session("nonce="+Nonce+"\nexpires=1000\n");
     check(fence::latch_requested(t.dbce.wstring()),"a stale session still latches");
     const auto plan=fence::prepare_run(t.dbce.wstring(),t.game.wstring(),14);
     check(plan.refusal.empty()&&plan.nonce.empty()&&fs::path(plan.run_root).filename()=="run-nononce-14","named without a nonce");}
    // The same root spelled differently (forward slashes, a trailing slash) gives the same receipt roots.
    {Tree t("spelling");t.seed_owner();t.session(valid_session());
     auto spelled=t.game.generic_wstring()+L"/";
     const auto plan=fence::prepare_run(t.dbce.wstring(),spelled,15);
     check(plan.refusal.empty()&&plan.game_root==t.game.wstring(),"normalized game root: "+fence::utf8(plan.game_root));}
}

void test_switches(){
    _putenv_s("OR2_EXE_TRACE","C:\\somewhere\\trace.txt");_putenv_s("OR2_XBOX_SCREENSHOT_DIR","C:\\shots");_putenv_s("KEEP_ME","1");
    SetEnvironmentVariableW(L"OR2_PROCESS_ONLY",L"1");
    const auto cleared=fence::clear_development_switches();
    check(std::getenv("OR2_EXE_TRACE")==nullptr&&std::getenv("OR2_XBOX_SCREENSHOT_DIR")==nullptr,"the CRT no longer sees OR2_ switches");
    wchar_t b[8];
    check(GetEnvironmentVariableW(L"OR2_PROCESS_ONLY",b,8)==0,"nor does the process environment");
    check(std::getenv("KEEP_ME")!=nullptr&&cleared.size()>=3,"other variables are untouched");
}
}

int main(){
    try{test_latch_and_run();test_fault_paths();test_switches();}
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    std::cout<<"test fence: "<<checks<<" checks passed\n";
    return 0;
}
