// Input shim slice 2b (windows/docs/INPUT-SHIM.md section 15, P24 and P27): the test fence against a temporary game tree,
// driving the real production writers that run without the game. Every case compares the game tree's complete
// inventory (every file's bytes and every folder, so a new or missing path also fails) before and after, through
// success and fault paths, and requires the intended outputs to exist under the run folder.
#include "test_fence.hpp"
#include "render_test.hpp"
#include "frame_capture.hpp"
#include "enhancements/video.hpp"
#include "platform/frontend_profiles.hpp"
#include "system/setup_screen.hpp"
#include "system/exe_image.hpp"
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
#include <vector>
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
    ~Tree(){
        // Never throws (a destructor during unwinding): junctions first, so remove_all never walks through them into a
        // target, then everything through the \?\ form so over-long paths go too.
        try{
            std::error_code e;
            const fs::path raw=L"\\\\?\\"+base.wstring();
            for(auto it=fs::recursive_directory_iterator(raw,e);!e&&it!=fs::recursive_directory_iterator();it.increment(e))
                if(GetFileAttributesW(it->path().c_str())&FILE_ATTRIBUTE_REPARSE_POINT){RemoveDirectoryW(it->path().c_str());it.disable_recursion_pending();}
            fs::remove_all(raw,e);
        }catch(...){}
    }
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

// This process token's SeBackupPrivilege: "enabled", "held, disabled", "not held" or "unknown" (nene 10329, note 1).
std::string backup_privilege_state(){
    HANDLE token{};
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return "unknown";
    std::string state="unknown";
    LUID backup{};
    DWORD size=0;
    GetTokenInformation(token,TokenPrivileges,nullptr,0,&size);
    std::vector<std::uint8_t> buffer(size);
    if(LookupPrivilegeValueW(nullptr,L"SeBackupPrivilege",&backup)&&size&&GetTokenInformation(token,TokenPrivileges,buffer.data(),size,&size)){
        state="not held";
        const auto* p=reinterpret_cast<const TOKEN_PRIVILEGES*>(buffer.data());
        for(DWORD i=0;i<p->PrivilegeCount;++i)
            if(p->Privileges[i].Luid.LowPart==backup.LowPart&&p->Privileges[i].Luid.HighPart==backup.HighPart)
                state=(p->Privileges[i].Attributes&SE_PRIVILEGE_ENABLED)?"enabled":"held, disabled";
    }
    CloseHandle(token);
    return state;
}

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

// nene 10191, finding 3: a subfolder the fence cannot list refuses the run (never a partial seed without run.json).
// Deterministic case: a folder whose name ends in a space (made through \\?\) is listed in its parent, but its contents
// cannot be opened through a Win32 path (error 3), whatever the token's privileges.
void test_denied_listing(){
    {Tree t("unlistable");t.seed_owner();t.session(valid_session());
     const auto odd=L"\\\\?\\"+(t.game/"SaveGame").wstring()+L"\\odd ";
     check(CreateDirectoryW(odd.c_str(),nullptr)!=0,"made an unlistable subfolder");
     {const HANDLE f=CreateFileW((odd+L"\\inside.dat").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);check(f!=INVALID_HANDLE_VALUE,"with a file in it");CloseHandle(f);}
     const auto plan=fence::prepare_run(t.dbce.wstring(),t.game.wstring(),20);
     // Windows lists it but cannot open what is inside (error 3): the run refuses either way, never a partial seed.
     check(!plan.refusal.empty()&&(plan.refusal.find("cannot list")!=std::string::npos||plan.refusal.find("cannot copy")!=std::string::npos),
           "an unreadable subfolder refuses: "+plan.refusal);
     check(!fs::exists(fs::path(plan.run_root)/"run.json"),"and no receipt claims a complete seed");
     check(fs::exists(t.game/"SaveGame"/"common.dat")&&read(t.game/"options.ini")=="widescreen=0\nfps=60\n","the owner's state is untouched");
     DeleteFileW((odd+L"\\inside.dat").c_str());RemoveDirectoryW(odd.c_str());}
    // A listing that fails whatever the privileges: SaveGame's own path fits Win32's limit but "SaveGame\*" does not, so
    // FindFirstFileW fails (no long-path opt-in in this test). The run refuses with "cannot list".
    {Tree t("longpath");
     const auto prefix=L"\\\\?\\";
     const std::size_t base=t.base.wstring().size()+1;
     // game = base\<pad>\game, saves = game\SaveGame: choose pad so that saves has exactly 258 characters.
     const std::size_t fixed=std::wstring(L"\\game\\SaveGame").size();
     const std::size_t pad=258-base-fixed;
     const auto game=t.base/std::wstring(pad,L'p')/"game";
     const auto saves=game/"SaveGame";
     check(saves.wstring().size()==258,"saves path of 258 characters: "+std::to_string(saves.wstring().size()));
     check(CreateDirectoryW((prefix+(t.base/std::wstring(pad,L'p')).wstring()).c_str(),nullptr)&&CreateDirectoryW((prefix+game.wstring()).c_str(),nullptr)&&
           CreateDirectoryW((prefix+saves.wstring()).c_str(),nullptr),"made the long tree");
     {const HANDLE f=CreateFileW((prefix+(saves/"a.dat").wstring()).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);check(f!=INVALID_HANDLE_VALUE,"a save in it");CloseHandle(f);}
     t.session(valid_session());
     const auto plan=fence::prepare_run(t.dbce.wstring(),game.wstring(),24);
     check(plan.refusal.find("cannot list SaveGame")!=std::string::npos,"an unlistable SaveGame refuses at the listing: "+plan.refusal);
     check(!fs::exists(fs::path(plan.run_root)/"run.json"),"no receipt");
     DeleteFileW((prefix+(saves/"a.dat").wstring()).c_str());}
    // An access-denied subfolder, as in nene's reproduction. A token with SeBackupPrivilege enabled lists it anyway
    // (FindFirstFile opens folders for backup intent; merely holding the privilege disabled is not enough), so the case
    // runs only where the deny rule is effective, and a skip prints the privilege state.
    {Tree t("denied");t.seed_owner();t.session(valid_session());
     write(t.game/"SaveGame"/"locked"/"inside.dat","x");
     const auto before=inventory(t.game);
     wchar_t user[256]{};DWORD n=256;GetUserNameW(user,&n);
     const auto locked=(t.game/"SaveGame"/"locked").wstring();
     check(_wsystem((L"icacls \""+locked+L"\" /deny \""+std::wstring(user)+L"\":(RD) >nul").c_str())==0,"denied listing on a SaveGame subfolder");
     WIN32_FIND_DATAW d{};
     const HANDLE probe=FindFirstFileW((locked+L"\\*").c_str(),&d);
     const bool effective=probe==INVALID_HANDLE_VALUE;
     if(!effective)FindClose(probe);
     const auto plan=effective?fence::prepare_run(t.dbce.wstring(),t.game.wstring(),23):fence::Plan{};
     _wsystem((L"icacls \""+locked+L"\" /remove:d \""+std::wstring(user)+L"\" >nul").c_str());
     if(effective){
         check(plan.refusal.find("cannot list")!=std::string::npos,"an access-denied subfolder refuses: "+plan.refusal);
         check(inventory(t.game)==before,"denied listing: game tree unchanged");
     }else std::cout<<"  (access-denied case skipped: this token lists through a deny rule; SeBackupPrivilege "
                    <<backup_privilege_state()<<")\n";}
}

// nene 10191, finding 4: a refusal is never written through a link, even when the link targets the game tree.
void test_refusal_never_through_a_link(){
    Tree t("linkrefusal");t.seed_owner();
    write(t.game/"inject.on",valid_session());                    // the test folder IS the game folder, via a junction
    const auto linked=t.base/"linked-test-folder";
    check(_wsystem((L"cmd /c mklink /J \""+linked.wstring()+L"\" \""+t.game.wstring()+L"\" >nul").c_str())==0,"made a junction to the game tree");
    const auto before=inventory(t.game);
    const auto plan=fence::prepare_run(linked.wstring(),t.game.wstring(),21);
    check(plan.refusal.find("link")!=std::string::npos,"the linked test folder refuses: "+plan.refusal);
    check(!fence::write_refusal(plan,21),"and the refusal is not written through the link");
    check(inventory(t.game)==before,"the game tree is unchanged: no refusal note, no run folder");
}

// Plants a hard link to the game's options.ini at the note's name, between write_refusal's check and its create.
fs::path planted_target;
void plant_hard_link(const std::wstring& path){CreateHardLinkW(path.c_str(),planted_target.c_str(),nullptr);}

// nene 10329, note 2: refused-<pid>.txt is always a new file. A hard link to a game file loses only its name, a stale
// note is replaced, and a folder or a file symlink at the name refuses, touching nothing.
void test_refusal_never_into_an_existing_name(){
    Tree t("refusalname");t.seed_owner();
    fence::Plan plan;plan.latched=true;plan.dbce_folder=t.dbce.wstring();plan.refusal="a planted name";
    const auto note=[&](int pid){return t.dbce/("refused-"+std::to_string(pid)+".txt");};
    const auto before=inventory(t.game);
    check(CreateHardLinkW(note(31).c_str(),(t.game/"options.ini").c_str(),nullptr)!=0,"made a hard link to a game file");
    check(fence::write_refusal(plan,31),"hard link: the refusal is written");
    check(inventory(t.game)==before,"hard link: the game file is unchanged:"+diff(before,inventory(t.game)));
    check(read(note(31)).find("a planted name")!=std::string::npos,"hard link: the note holds the reason");
    {const HANDLE h=CreateFileW(note(31).c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
     BY_HANDLE_FILE_INFORMATION info{};
     check(h!=INVALID_HANDLE_VALUE&&GetFileInformationByHandle(h,&info)&&info.nNumberOfLinks==1,"hard link: the note is a new file of one link");
     if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
    write(note(32),"an earlier process's note\n");
    check(fence::write_refusal(plan,32),"stale note: the refusal is written");
    check(read(note(32)).find("a planted name")!=std::string::npos&&read(note(32)).find("earlier")==std::string::npos,"stale note: replaced");
    fs::create_directories(note(33));
    check(!fence::write_refusal(plan,33)&&fs::is_directory(note(33)),"a folder at the name refuses and is left as it was");
    // Something planted after the check (a hard link, which needs no privilege): the create fails rather than opening it.
    planted_target=t.game/"options.ini";
    fence::refusal_create_hook()=plant_hard_link;
    const bool raced=fence::write_refusal(plan,35);
    fence::refusal_create_hook()=nullptr;
    check(!raced&&fs::exists(note(35)),"a hard link planted after the check: the create fails");
    check(inventory(t.game)==before,"planted after the check: the game file is unchanged:"+diff(before,inventory(t.game)));
    // A file symlink needs the symbolic-link privilege or developer mode; without either the case is skipped, and says so.
    if(CreateSymbolicLinkW(note(34).c_str(),(t.game/"options.ini").c_str(),SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)){
        check(!fence::write_refusal(plan,34),"a file symlink at the name refuses");
        check((GetFileAttributesW(note(34).c_str())&FILE_ATTRIBUTE_REPARSE_POINT)!=0,"symlink: the link is left as it was");
        check(inventory(t.game)==before,"symlink: its target in the game tree is unchanged:"+diff(before,inventory(t.game)));
    }else std::cout<<"  (file-symlink case skipped: cannot create a symbolic link here, error "<<GetLastError()<<")\n";
}

// nene 10191, finding 1: the game folder's cache built for another range table is loaded but never rewritten when the
// game folder is read only. A well-formed cache is made with the real writer, then its range-set id is changed; a test
// hook stands in for the image verification (no genuine EXE image on a test machine).
void test_cache_for_another_range_table(){
    Tree t("cacheid");write(t.game/"OR2006C2C.EXE","x");t.session(valid_session());
    std::vector<std::uint8_t> image(outrun::platform::ExeImageSize,0);
    std::string error;
    check(outrun::platform::exe_image_write_cache((t.base/"made.cache").string(),image,&error),"a well-formed cache: "+error);
    auto bytes=read(t.base/"made.cache");
    for(std::size_t i=32;i<64;++i)bytes[i]=char(bytes[i]^0x5a);   // built for another range table
    write(t.game/"OR2006C2C.cache",bytes);
    write(t.base/"copy"/"OR2006C2C.cache",bytes);
    outrun::platform::exe_image_verify_hook()=[](const std::vector<std::uint8_t>&,std::string*){return true;};
    const auto before=inventory(t.game);
    const auto plan=fence::prepare_run(t.dbce.wstring(),t.game.wstring(),22);
    check(plan.refusal.empty()&&plan.exe_cache_present,"prepared with a cache present: "+plan.refusal);
    SetupPlatform none;
    check(exe_setup_prepare(fence::utf8(plan.game_root),fence::utf8(plan.run_root),none,&error,true),"read only: the cache is loaded: "+error);
    check(inventory(t.game)==before,"read only: the game folder's cache is not rewritten:"+diff(before,inventory(t.game)));
    // The contrast: the same loader allowed to rewrite does rewrite (so the read-only case really reached that branch).
    check(outrun::platform::exe_image_load_cache((t.base/"copy"/"OR2006C2C.cache").string(),&error,true),"rewrite allowed: loaded");
    check(read(t.base/"copy"/"OR2006C2C.cache")!=bytes,"rewrite allowed: the cache was rewritten");
    outrun::platform::exe_image_verify_hook()=nullptr;
}

// The opt-in rendering test (render_test.hpp): a strict request beside inject.on, presentation only, the frames into the
// run folder; absent, nothing changes. The game tree stays byte-identical in every case.
void test_render_test(){
    namespace rt=outrun::desktop;
    using rt::TripleScreens;using rt::TripleProjection;
    const std::string head=std::string(rt::RenderTestSchema)+"\nnonce="+Nonce+"\n";
    rt::RenderTest r;std::string why;
    check(rt::parse_render_test(head,Nonce,r,why)&&r.requested&&r.screens==TripleScreens::off&&r.views==TripleProjection::planar&&
          r.frames==0&&r.every==1&&r.after==0&&r.scale==2,"defaults: "+why);
    check(rt::parse_render_test(head+"screens=separate\r\nviews=angled\r\nframes=300\r\nevery=60\r\nafter=36000\r\nscale=4",Nonce,r,why)&&
          r.screens==TripleScreens::separate&&r.views==TripleProjection::angled&&r.frames==300&&r.every==60&&r.after==36000&&r.scale==4,
          "every key, CRLF and no final newline: "+why);
    check(rt::parse_render_test(head+"screens=surround\nviews=angled\n",Nonce,r,why)&&r.screens==TripleScreens::surround,"angled over surround");
    const std::vector<std::pair<std::string,std::string>> refused{
        {head,""},                                                        // no session nonce
        {"outrun.render-test@2\nnonce="+Nonce+"\n",Nonce},{"nonce="+Nonce+"\n",Nonce},
        {std::string(rt::RenderTestSchema)+"\n",Nonce},                     // no nonce line
        {std::string(rt::RenderTestSchema)+"\nnonce=other12345abcdef\n",Nonce},
        {head+"views=angled\n",Nonce},{head+"screens=off\nviews=angled\n",Nonce},
        {head+"screens=Separate\n",Nonce},{head+"views=wide\n",Nonce},
        {head+"frames=301\n",Nonce},{head+"frames=-1\n",Nonce},{head+"frames=\n",Nonce},{head+"frames=1e2\n",Nonce},
        {head+"every=0\n",Nonce},{head+"every=61\n",Nonce},{head+"after=36001\n",Nonce},
        {head+"scale=3\n",Nonce},{head+"scale=8\n",Nonce},{head+"frames=1\nframes=2\n",Nonce},
        {head+"folder=C:\\\\elsewhere\n",Nonce},{head+"screens\n",Nonce},{head+"=separate\n",Nonce},
        {head+"screens=separate\x01\n",Nonce},{head+"views=planar\xe9\n",Nonce},{head+std::string(1100,'#'),Nonce}};
    for(const auto& [bytes,nonce]:refused)
        check(!rt::parse_render_test(bytes,nonce,r,why)&&!r.requested&&!why.empty(),"refused: "+bytes.substr(0,60));

    {   Tree t("render");t.seed_owner();t.session(valid_session());
        const auto before=inventory(t.game);
        auto plan=fence::prepare_run(t.dbce.wstring(),t.game.wstring(),5151);
        check(plan.refusal.empty(),"render: prepared");
        check(rt::load_render_test(plan.dbce_folder,plan.run_root,plan.nonce,r,why)&&!r.requested,"absent: no rendering test");
        check(!fs::exists(fs::path(plan.run_root)/"render-test.txt")&&!fs::exists(fs::path(plan.run_root)/"frames"),"absent: nothing made");
        const std::string req=head+"screens=separate\nviews=angled\nframes=12\nevery=3\n";
        write(t.dbce/"render-test.txt",req);
        check(rt::load_render_test(plan.dbce_folder,plan.run_root,plan.nonce,r,why)&&r.requested&&r.frames==12&&r.every==3,"loaded: "+why);
        check(read(fs::path(plan.run_root)/"render-test.txt")==req&&fs::is_directory(fs::path(plan.run_root)/"frames"),
              "the request kept in the run folder, its frames folder made");
        check(!rt::load_render_test(plan.dbce_folder,plan.run_root,plan.nonce,r,why)&&!r.requested,"a second load never overwrites");
        check(inventory(t.game)==before,"render: the game tree is unchanged");
    }
    {   Tree t("render-bad");t.seed_owner();t.session(valid_session());
        auto plan=fence::prepare_run(t.dbce.wstring(),t.game.wstring(),5252);
        write(t.dbce/"render-test.txt",head+"frames=5\nscreens=everywhere\n");
        check(!rt::load_render_test(plan.dbce_folder,plan.run_root,plan.nonce,r,why)&&!r.requested&&why.find("screens")!=std::string::npos,
              "a bad request refuses: "+why);
        check(!fs::exists(fs::path(plan.run_root)/"render-test.txt")&&!fs::exists(fs::path(plan.run_root)/"frames"),"refused: nothing made");
        fs::remove(t.dbce/"render-test.txt");fs::create_directories(t.dbce/"render-test.txt");
        check(!rt::load_render_test(plan.dbce_folder,plan.run_root,plan.nonce,r,why),"a folder at the name refuses");
    }
    // The present path's burst (frame_capture.hpp): due frames and the box downscale.
    using outrun::xbox_runtime::frame_capture_due;using outrun::xbox_runtime::frame_capture_rgb;
    check(!frame_capture_due(4,0,3,2,5)&&frame_capture_due(5,0,3,2,5)&&!frame_capture_due(6,1,3,2,5)&&frame_capture_due(7,1,3,2,5),"due: after, then every");
    check(!frame_capture_due(9,3,3,2,5)&&!frame_capture_due(5,0,0,2,5)&&!frame_capture_due(5,0,3,0,5),"never past frames, nor with none or every 0");
    {   std::vector<std::uint8_t> px(4*2*8+8,0);   // 4 x 2 pixels, an 8-byte pad per row
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<4;++x){auto* p=&px[y*24+x*4];p[0]=std::uint8_t(10*x);p[1]=std::uint8_t(100+y);p[2]=200;p[3]=255;}
        unsigned w=0,h=0;
        const auto one=frame_capture_rgb(px.data(),24,4,2,1,w,h);
        check(w==4&&h==2&&one.size()==24&&one[3]==10&&one[4]==100&&one[5]==200&&one[21]==30&&one[22]==101,"scale 1: the first three bytes");
        const auto two=frame_capture_rgb(px.data(),24,4,2,2,w,h);
        check(w==2&&h==1&&two.size()==6&&two[0]==5&&two[1]==101&&two[2]==200&&two[3]==25,"scale 2: the rounded block mean");
        const auto four=frame_capture_rgb(px.data(),24,4,2,4,w,h);
        check(w==1&&h==0&&four.empty(),"scale 4 on 4 x 2: the partial edge block is dropped");
    }
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
    // test_cache_for_another_range_table installs an EXE image, after which exe_setup_prepare returns at once: last.
    try{test_latch_and_run();test_fault_paths();test_denied_listing();test_refusal_never_through_a_link();
        test_refusal_never_into_an_existing_name();test_render_test();test_switches();
        test_cache_for_another_range_table();}
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    std::cout<<"test fence: "<<checks<<" checks passed\n";
    return 0;
}
