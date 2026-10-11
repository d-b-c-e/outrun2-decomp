// The test fence for a latched process (test_fence.hpp; INPUT-SHIM.md section 15, slice 2b).
#include "test_fence.hpp"
#include "shim.hpp"
#include "sha256.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
namespace outrun::desktop::fence {
namespace {
std::wstring widen(const std::string& s){
    if(s.empty())return {};
    const int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);
    std::wstring w(std::size_t(n),L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),w.data(),n);return w;
}
std::wstring normalize(std::wstring p){
    for(auto& c:p)if(c==L'/')c=L'\\';
    while(p.size()>3&&p.back()==L'\\')p.pop_back();
    return p;
}
bool exists(const std::wstring& p){return GetFileAttributesW(p.c_str())!=INVALID_FILE_ATTRIBUTES;}
bool read_small(const std::wstring& path,std::size_t limit,std::string& out){
    std::ifstream f(path,std::ios::binary);if(!f)return false;
    out.assign(limit+1,'\0');f.read(out.data(),std::streamsize(out.size()));out.resize(std::size_t(f.gcount()));
    return out.size()<=limit;
}
std::string json_escape(const std::string& s){
    std::string o;
    for(const char c:s){
        if(c=='"'||c=='\\'){o+='\\';o+=c;}
        else if(static_cast<unsigned char>(c)<0x20){char b[8];std::snprintf(b,sizeof b,"\\u%04x",unsigned(static_cast<unsigned char>(c)));o+=b;}
        else o+=c;
    }
    return o;
}
// Copies every file of `from` (recursively, at most 4 levels) to `to`, refusing links, counting against the limits.
bool seed_tree(const std::wstring& from,const std::wstring& to,const std::wstring& relative,int depth,const SeedLimits& limits,Plan& plan){
    if(depth>4){plan.refusal="SaveGame is nested deeper than 4 folders";return false;}
    WIN32_FIND_DATAW d{};
    const HANDLE h=FindFirstFileW((from+L"\\*").c_str(),&d);
    if(h==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND;   // an empty folder
    bool ok=true;
    do{
        const std::wstring name=d.cFileName;
        if(name==L"."||name==L"..")continue;
        const std::wstring src=from+L"\\"+name,dst=to+L"\\"+name,rel=relative.empty()?name:relative+L"\\"+name;
        if(d.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT){plan.refusal="a link in the owner's state is never followed: "+utf8(rel);ok=false;break;}
        if(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){
            if(!CreateDirectoryW(dst.c_str(),nullptr)){plan.refusal="cannot create "+utf8(dst);ok=false;break;}
            if(!seed_tree(src,dst,rel,depth+1,limits,plan)){ok=false;break;}
            continue;
        }
        const std::uint64_t size=(std::uint64_t(d.nFileSizeHigh)<<32)|d.nFileSizeLow;
        if(plan.seeded.size()+1>limits.max_files){plan.refusal="the owner's state has more than "+std::to_string(limits.max_files)+" files";ok=false;break;}
        if(plan.seeded_bytes+size>limits.max_bytes){plan.refusal="the owner's state is larger than "+std::to_string(limits.max_bytes>>20)+" MiB";ok=false;break;}
        if(!CopyFileExW(src.c_str(),dst.c_str(),nullptr,nullptr,nullptr,COPY_FILE_FAIL_IF_EXISTS)){
            plan.refusal="cannot copy "+utf8(rel)+" (error "+std::to_string(GetLastError())+")";ok=false;break;}
        std::uint64_t a=0,b=0;bool ra=false,rb=false;
        const auto ha=sha256_file(src,a,ra),hb=sha256_file(dst,b,rb);
        if(!ra||!rb||ha!=hb||a!=b){plan.refusal="the copy of "+utf8(rel)+" does not match its source";ok=false;break;}
        plan.seeded.push_back({rel,a,ha});
        plan.seeded_bytes+=a;
    }while(FindNextFileW(h,&d));
    FindClose(h);
    return ok;
}
}

std::string utf8(const std::wstring& w){
    if(w.empty())return {};
    const int n=WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),nullptr,0,nullptr,nullptr);
    std::string s(std::size_t(n),'\0');WideCharToMultiByte(CP_UTF8,0,w.data(),int(w.size()),s.data(),n,nullptr,nullptr);return s;
}

std::wstring dbce_folder(){
    wchar_t base[MAX_PATH]{};
    const DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",base,MAX_PATH);
    if(!n||n>=MAX_PATH)return {};
    return std::wstring(base)+L"\\dbce\\outrun2006-decomp";
}

bool latch_requested(const std::wstring& folder){return !folder.empty()&&exists(folder+L"\\inject.on");}

bool has_reparse_point_on_path(const std::wstring& path){
    std::wstring p=normalize(path);
    while(!p.empty()){
        const DWORD a=GetFileAttributesW(p.c_str());
        if(a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_REPARSE_POINT))return true;
        const auto slash=p.find_last_of(L'\\');
        if(slash==std::wstring::npos||slash<3)break;   // stop at the drive root
        p.resize(slash);
    }
    return false;
}

std::string sha256_file(const std::wstring& path,std::uint64_t& size,bool& ok){
    ok=false;size=0;
    std::ifstream f(path,std::ios::binary);if(!f)return {};
    std::ostringstream s;s<<f.rdbuf();
    const auto bytes=s.str();
    size=bytes.size();ok=true;
    return dbce::shim::hex(dbce::shim::sha256(bytes));
}

Plan prepare_run(const std::wstring& folder_in,const std::wstring& game_root_in,std::uint32_t pid,const SeedLimits& limits){
    Plan plan;
    plan.latched=true;
    plan.dbce_folder=normalize(folder_in);
    plan.game_root=normalize(game_root_in);
    if(plan.dbce_folder.empty()){plan.refusal="LOCALAPPDATA is not set";return plan;}
    if(has_reparse_point_on_path(plan.dbce_folder)){plan.refusal="the test folder or a folder above it is a link";return plan;}
    // The nonce names the run; a session that does not parse still latches (it is refused at admission).
    std::string session;
    if(read_small(plan.dbce_folder+L"\\inject.on",512,session)){
        dbce::shim::Session s;std::string why;
        const auto now=std::uint64_t(std::time(nullptr));
        if(dbce::shim::parse_session(session,now,s,why))plan.nonce=s.nonce;
    }
    plan.run_root=plan.dbce_folder+L"\\run-"+widen(plan.nonce.empty()?std::string("nononce"):plan.nonce)+L"-"+std::to_wstring(pid);
    if(!CreateDirectoryW(plan.run_root.c_str(),nullptr)){
        plan.refusal=GetLastError()==ERROR_ALREADY_EXISTS?"the run folder already exists: "+utf8(plan.run_root):"cannot create the run folder: "+utf8(plan.run_root);
        return plan;
    }
    if(!CreateDirectoryW((plan.run_root+L"\\SaveGame").c_str(),nullptr)){plan.refusal="cannot create the run's SaveGame";return plan;}
    // The owner's writable state: SaveGame (recursively) and options.ini. The game folder is only read.
    const std::wstring saves=plan.game_root+L"\\SaveGame";
    const DWORD sa=GetFileAttributesW(saves.c_str());
    if(sa!=INVALID_FILE_ATTRIBUTES){
        if(sa&FILE_ATTRIBUTE_REPARSE_POINT){plan.refusal="SaveGame is a link; it is never followed";return plan;}
        if(!seed_tree(saves,plan.run_root+L"\\SaveGame",L"SaveGame",0,limits,plan))return plan;
    }
    const std::wstring options=plan.game_root+L"\\options.ini";
    const DWORD oa=GetFileAttributesW(options.c_str());
    if(oa!=INVALID_FILE_ATTRIBUTES){
        if(oa&FILE_ATTRIBUTE_REPARSE_POINT){plan.refusal="options.ini is a link; it is never followed";return plan;}
        std::uint64_t size=0;bool ok=false;
        const auto before=sha256_file(options,size,ok);
        if(!ok){plan.refusal="cannot read options.ini";return plan;}
        if(plan.seeded.size()+1>limits.max_files||plan.seeded_bytes+size>limits.max_bytes){plan.refusal="the owner's state exceeds the seed limits";return plan;}
        if(!CopyFileExW(options.c_str(),(plan.run_root+L"\\options.ini").c_str(),nullptr,nullptr,nullptr,COPY_FILE_FAIL_IF_EXISTS)){plan.refusal="cannot copy options.ini";return plan;}
        std::uint64_t s2=0;bool ok2=false;
        if(sha256_file(plan.run_root+L"\\options.ini",s2,ok2)!=before||!ok2){plan.refusal="the copy of options.ini does not match its source";return plan;}
        plan.seeded.push_back({L"options.ini",size,before});plan.seeded_bytes+=size;
    }
    plan.exe_cache_present=exists(plan.game_root+L"\\OR2006C2C.cache");
    // The run receipt: both roots, the effective paths and every seeded file's hash.
    std::string j="{\n  \"schema\": 1,\n  \"pid\": "+std::to_string(pid)+",\n  \"nonce\": \""+json_escape(plan.nonce)+"\",\n";
    j+="  \"gameRoot\": \""+json_escape(utf8(plan.game_root))+"\",\n  \"runRoot\": \""+json_escape(utf8(plan.run_root))+"\",\n";
    j+="  \"effective\": {\"home\": \""+json_escape(utf8(plan.run_root))+"\", \"saves\": \""+json_escape(utf8(plan.run_root+L"\\SaveGame"))+
       "\", \"options\": \""+json_escape(utf8(plan.run_root+L"\\options.ini"))+"\", \"log\": \""+json_escape(utf8(plan.run_root+L"\\runtime.log"))+
       "\", \"crash\": \""+json_escape(utf8(plan.run_root+L"\\crash.txt"))+"\", \"exeCache\": \""+
       (plan.exe_cache_present?"read-only from the game folder":"generated into the run folder when needed")+"\"},\n  \"seeded\": [";
    for(std::size_t i=0;i<plan.seeded.size();++i)
        j+=std::string(i?",":"")+"\n    {\"path\": \""+json_escape(utf8(plan.seeded[i].relative))+"\", \"size\": "+std::to_string(plan.seeded[i].size)+
           ", \"sha256\": \""+plan.seeded[i].sha256+"\"}";
    j+="\n  ],\n  \"seededBytes\": "+std::to_string(plan.seeded_bytes)+"\n}\n";
    const HANDLE h=CreateFileW((plan.run_root+L"\\run.json").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE){plan.refusal="cannot write run.json";return plan;}
    DWORD written=0;
    const bool ok=WriteFile(h,j.data(),DWORD(j.size()),&written,nullptr)&&written==j.size()&&FlushFileBuffers(h);
    CloseHandle(h);
    if(!ok)plan.refusal="cannot write run.json";
    return plan;
}

bool write_refusal(const Plan& plan,std::uint32_t pid){
    if(plan.dbce_folder.empty())return false;
    const std::wstring path=plan.dbce_folder+L"\\refused-"+std::to_wstring(pid)+L".txt";
    const HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return false;
    const std::string text="test run refused before the game initialized: "+plan.refusal+"\r\n";
    DWORD written=0;
    const bool ok=WriteFile(h,text.data(),DWORD(text.size()),&written,nullptr)&&written==text.size();
    CloseHandle(h);
    return ok;
}

std::wstring home_of(const Plan& plan,const std::wstring& game_root){return plan.latched&&plan.refusal.empty()?plan.run_root:normalize(game_root);}
std::wstring log_path(const Plan& plan,const std::wstring& game_root){return home_of(plan,game_root)+L"\\runtime.log";}

std::vector<std::string> clear_development_switches(){
    std::vector<std::wstring> names;
    if(wchar_t* env=GetEnvironmentStringsW()){
        for(const wchar_t* p=env;*p;p+=wcslen(p)+1){
            const std::wstring e=p;
            const auto eq=e.find(L'=',1);
            if(eq==std::wstring::npos)continue;
            const std::wstring name=e.substr(0,eq);
            if(name.size()>4&&_wcsnicmp(name.c_str(),L"OR2_",4)==0)names.push_back(name);
        }
        FreeEnvironmentStringsW(env);
    }
    std::vector<std::string> cleared;
    for(const auto& n:names){
        _wputenv_s(n.c_str(),L"");                 // the CRT's copy, which getenv reads
        SetEnvironmentVariableW(n.c_str(),nullptr);  // the process environment
        cleared.push_back(utf8(n));
    }
    return cleared;
}
}
