// A recording's startup snapshot (startup_state.hpp).
#include "startup_state.hpp"
#include "ordinary_recording.hpp"   // file_sha256
#include "session_request.hpp"      // utf8
#include "sha256.hpp"
#include "shim.hpp"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
namespace outrun::desktop::startup {
namespace {
bool hex(const std::string& s,std::size_t n){
    if(s.size()!=n)return false;
    for(char c:s)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    return true;
}
std::string lower(std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
std::vector<std::string> split(const std::string& s,char sep){
    std::vector<std::string> out;std::size_t at=0;
    for(;;){const auto n=s.find(sep,at);out.push_back(s.substr(at,n==std::string::npos?std::string::npos:n-at));if(n==std::string::npos)break;at=n+1;}
    return out;
}
// One plain file or folder name: no separator, wildcard, drive or stream mark, not "." / "..", no trailing dot or
// blank, no control character, not a device name.
bool plain_name(const std::string& n){
    if(n.empty()||n.size()>200||n=="."||n=="..")return false;
    for(unsigned char c:n)if(c<0x20||c=='\\'||c=='/'||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|')return false;
    if(n.back()=='.'||n.back()==' ')return false;
    auto stem=lower(n.substr(0,n.find('.')));
    static const char* const Devices[]{"con","prn","aux","nul","com1","com2","com3","com4","com5","com6","com7","com8","com9",
                                       "lpt1","lpt2","lpt3","lpt4","lpt5","lpt6","lpt7","lpt8","lpt9"};
    for(const char* d:Devices)if(stem==d)return false;
    return true;
}
std::wstring wide(const std::string& relative){   // "/" separated, admitted: the native form under a folder
    std::wstring w=std::filesystem::u8path(relative).wstring();
    std::replace(w.begin(),w.end(),L'/',L'\\');
    return w;
}
Verified refused(const std::string& why){Verified v;v.why=why;return v;}
// Every file under `root` (relative, "/" separated, lower-cased for comparison), or why not (a link, too deep, too many).
bool listing(const std::wstring& root,const std::wstring& rel,int depth,std::set<std::string>& files,std::string& why){
    if(depth>6){why="the startup folder is nested too deep";return false;}
    WIN32_FIND_DATAW d;
    const auto pattern=root+(rel.empty()?L"":L"\\"+rel)+L"\\*";
    HANDLE h=FindFirstFileExW(pattern.c_str(),FindExInfoBasic,&d,FindExSearchNameMatch,nullptr,0);
    if(h==INVALID_HANDLE_VALUE){const DWORD e=GetLastError();if(e==ERROR_FILE_NOT_FOUND)return true;why="the startup folder cannot be listed (Win32 "+std::to_string(e)+")";return false;}
    bool ok=true;
    do{
        const std::wstring name=d.cFileName;
        if(name==L"."||name==L"..")continue;
        if(d.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT){why="the startup folder holds a link: "+session::utf8(name);ok=false;break;}
        const std::wstring child=rel.empty()?name:rel+L"\\"+name;
        if(d.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){if(!listing(root,child,depth+1,files,why)){ok=false;break;}continue;}
        auto u=session::utf8(child);std::replace(u.begin(),u.end(),'\\','/');
        files.insert(lower(u));
        if(files.size()>MaxFiles+1){why="the startup folder holds too many files";ok=false;break;}
    }while(FindNextFileW(h,&d));
    FindClose(h);
    return ok;
}
}

bool admitted(const std::string& relative){
    if(relative=="options.ini"||relative=="OutRunDesktop.ini")return true;
    const auto parts=split(relative,'/');
    if(parts.size()<2||parts.size()>6||parts[0]!="SaveGame")return false;   // SaveGame, at most 4 folders, then the file
    for(std::size_t i=1;i<parts.size();++i)if(!plain_name(parts[i]))return false;
    return true;
}

Verified parse(const std::string& bytes){
    if(bytes.size()>256*1024)return refused("the startup seal is larger than 256 KiB");
    if(!dbce::shim::strict_utf8(bytes))return refused("the startup seal is not valid UTF-8");
    std::vector<std::string> lines;
    for(auto& l:split(bytes,'\n')){if(!l.empty()&&l.back()=='\r')l.pop_back();lines.push_back(l);}
    while(!lines.empty()&&lines.back().empty())lines.pop_back();
    if(lines.size()<3||lines[0]!=Schema)return refused(std::string("the startup seal does not start with ")+Schema);
    Verified v;
    const auto id=split(lines[1],'\t'),exe=split(lines[2],'\t');
    if(id.size()!=2||id[0]!="requestId"||!hex(id[1],32))return refused("the startup seal's requestId line is not 32 lowercase hex digits");
    if(exe.size()!=2||exe[0]!="exeSha256"||!hex(exe[1],64))return refused("the startup seal's exeSha256 line is not 64 lowercase hex digits");
    v.snapshot.request_id=id[1];v.snapshot.exe_sha256=exe[1];
    std::set<std::string> seen;bool savegame_absent=false,savegame_files=false;std::uint64_t total=0;
    for(std::size_t i=3;i<lines.size();++i){
        const auto f=split(lines[i],'\t');
        const auto line="startup seal line "+std::to_string(i+1);
        Member m;
        if(f.size()==2&&f[0]=="absent"){
            if(f[1]!="options.ini"&&f[1]!="OutRunDesktop.ini"&&f[1]!="SaveGame")return refused(line+": only options.ini, OutRunDesktop.ini or SaveGame can be absent");
            m.relative=f[1];m.absent=true;if(f[1]=="SaveGame")savegame_absent=true;
        }else if(f.size()==4&&f[0]=="file"){
            if(!admitted(f[1]))return refused(line+": "+f[1].substr(0,80)+" is not in the startup inventory");
            if(f[2].empty()||f[2].size()>12||f[2].find_first_not_of("0123456789")!=std::string::npos)return refused(line+": the size is not a number");
            if(!hex(f[3],64))return refused(line+": the sha256 is not 64 lowercase hex digits");
            m.relative=f[1];m.size=std::strtoull(f[2].c_str(),nullptr,10);m.sha256=f[3];
            if(f[1].rfind("SaveGame/",0)==0)savegame_files=true;
            total+=m.size;
        }else return refused(line+" is neither file<TAB>path<TAB>size<TAB>sha256 nor absent<TAB>path");
        if(!seen.insert(lower(m.relative)).second)return refused(line+": "+m.relative.substr(0,80)+" is listed twice");
        v.snapshot.members.push_back(m);
    }
    if(savegame_absent&&savegame_files)return refused("the startup seal lists SaveGame as absent and SaveGame files");
    for(const char* top:{"options.ini","OutRunDesktop.ini"})
        if(!seen.count(lower(top)))return refused(std::string("the startup seal does not say whether ")+top+" was there");
    if(v.snapshot.members.size()>MaxFiles+3)return refused("the startup snapshot has too many members");
    if(total>MaxBytes)return refused("the startup snapshot is larger than the seed limits");
    v.ok=true;
    return v;
}

Verified verify(const std::filesystem::path& folder,const std::string& seal_sha256,const std::string& exe_sha256){
    const auto seal=folder/L"complete.tsv";
    const DWORD fa=GetFileAttributesW(folder.c_str());
    if(fa==INVALID_FILE_ATTRIBUTES||!(fa&FILE_ATTRIBUTE_DIRECTORY))return refused("the startup folder is not there");
    if(fa&FILE_ATTRIBUTE_REPARSE_POINT)return refused("the startup folder is a link");
    std::string bytes;
    {   HANDLE f=CreateFileW(seal.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(f==INVALID_HANDLE_VALUE)return refused("the startup seal cannot be read (Win32 "+std::to_string(GetLastError())+")");
        bytes.assign(256*1024+1,'\0');DWORD n=0;const bool ok=ReadFile(f,bytes.data(),DWORD(bytes.size()),&n,nullptr)!=0;CloseHandle(f);
        if(!ok)return refused("the startup seal cannot be read");
        bytes.resize(n);}
    if(dbce::shim::hex(dbce::shim::sha256(bytes))!=seal_sha256)return refused("the startup seal's bytes are not startupSealSha256");
    auto v=parse(bytes);
    if(!v.ok)return v;
    if(v.snapshot.exe_sha256!=exe_sha256)return refused("the startup snapshot names another exe");
    std::set<std::string> files;std::string why;
    if(!listing(folder.wstring(),L"",0,files,why))return refused(why);
    std::set<std::string> listed{"complete.tsv"};
    for(const auto& m:v.snapshot.members)if(!m.absent)listed.insert(lower(m.relative));
    for(const auto& f:files)if(!listed.count(f))return refused("the startup folder holds an unlisted file: "+f.substr(0,80));
    for(const auto& m:v.snapshot.members){
        if(m.absent)continue;
        if(!files.count(lower(m.relative)))return refused("the startup folder lacks "+m.relative.substr(0,80));
        std::uint64_t size=0;std::string sha;
        try{sha=file_sha256(folder/wide(m.relative),size);}catch(const std::exception& e){return refused(e.what());}
        if(size!=m.size||sha!=m.sha256)return refused(m.relative.substr(0,80)+" differs from the startup seal");
    }
    v.ok=true;
    return v;
}

std::string seed(const std::filesystem::path& folder,const Snapshot& snapshot,const std::filesystem::path& home){
    const auto win32=[](const std::string& what){return what+" (Win32 "+std::to_string(GetLastError())+")";};
    if(!CreateDirectoryW(home.c_str(),nullptr))return win32("the playback home cannot be made, or is already there");
    if(!CreateDirectoryW((home/L"SaveGame").c_str(),nullptr))return win32("the playback home's SaveGame cannot be made");
    for(const auto& m:snapshot.members){
        if(m.absent||m.relative=="OutRunDesktop.ini")continue;   // the settings are read from the snapshot itself
        const auto to=home/wide(m.relative);
        // The folders between home\SaveGame and the file, made in order (each new or already made by an earlier member).
        std::vector<std::filesystem::path> dirs;
        for(auto dir=to.parent_path();dir!=home/L"SaveGame"&&dir!=home;dir=dir.parent_path())dirs.push_back(dir);
        for(auto it=dirs.rbegin();it!=dirs.rend();++it)
            if(!CreateDirectoryW(it->c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return win32("the playback home cannot hold "+m.relative.substr(0,80));
        if(!CopyFileW((folder/wide(m.relative)).c_str(),to.c_str(),TRUE))return win32("cannot seed "+m.relative.substr(0,80));
        std::uint64_t size=0;std::string sha;
        try{sha=file_sha256(to,size);}catch(const std::exception& e){return e.what();}
        if(size!=m.size||sha!=m.sha256)return m.relative.substr(0,80)+" changed while it was seeded";
    }
    return {};
}

std::string playback_settings(const std::string& recorded,const std::string& owner){
    // Section-wise, by header line, exactly as Ini reads one (desktop_settings.cpp header): the trimmed line starts
    // with "[", the name runs to the first "]" and is trimmed, case-insensitive.
    const auto trim=[](const std::string& s){const auto a=s.find_first_not_of(" \t\r\n");if(a==std::string::npos)return std::string();
        return s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);};
    const auto header=[&](const std::string& line)->std::string{
        const auto t=trim(line);
        if(t.size()<2||t[0]!='[')return {};
        const auto e=t.find(']');
        return e==std::string::npos?std::string():lower(trim(t.substr(1,e-1)));};
    const auto presentation=[](const std::string& s){return s=="window"||s=="triple";};
    std::string out,kept;
    std::string section;
    for(const auto& line:split(recorded,'\n')){
        const auto h=header(line);if(!h.empty())section=h;
        if(!presentation(section))out+=line+"\n";
    }
    while(out.size()>=2&&out[out.size()-1]=='\n'&&(out[out.size()-2]=='\n'||(out.size()>=3&&out[out.size()-2]=='\r'&&out[out.size()-3]=='\n')))out.pop_back();
    section.clear();
    for(const auto& line:split(owner,'\n')){
        const auto h=header(line);if(!h.empty())section=h;
        if(presentation(section))kept+=line+"\n";
    }
    if(!kept.empty())out+=(out.empty()||out.back()=='\n'?"":"\n")+std::string("\n")+kept;
    return out;
}
}
