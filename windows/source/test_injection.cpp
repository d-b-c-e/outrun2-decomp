#include "test_injection.hpp"
#include "dbce_inject_table.hpp"
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
namespace outrun::desktop::inject {
namespace {
namespace ctl=dbce::controls;
constexpr std::size_t MaxFileBytes=4096;
constexpr int MaxFileCommands=32,MaxSessionCommands=2000;
constexpr std::uint64_t MaxSessionSeconds=3600;

struct Session {
    std::mutex lock;                 // everything below; the table has its own
    bool armed{},testing{};
    ctl::Section applied;
    std::string nonce;
    std::uint64_t expires_ms{};      // on now_ms()'s clock; 0 = none (tests)
    int commands{};
    std::uint64_t clock{},next_poll{};
    std::wstring command_file;
    FILETIME last_write{};
    ObserveData* view{};
    bool view_failed{};
};
Session g;
ctl::InjectionTable g_table;

std::uint64_t now_ms(){return g.testing?g.clock:std::uint64_t(GetTickCount64());}
std::wstring dbce_path(const wchar_t* file){
    wchar_t base[MAX_PATH]{};const DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",base,MAX_PATH);
    if(!n||n>=MAX_PATH)return {};
    return std::wstring(base)+L"\\dbce\\outrun2006-decomp\\"+file;
}
std::string read_bytes(const std::wstring& path,std::size_t limit){
    std::ifstream f(path,std::ios::binary);std::string s(limit+1,'\0');
    f.read(s.data(),std::streamsize(s.size()));s.resize(std::size_t(f.gcount()));return s;
}
// Called with g.lock held.
bool live(std::vector<std::string>& log){
    if(!g.armed)return false;
    if(g.expires_ms&&now_ms()>=g.expires_ms){
        g.armed=false;
        log.push_back("inject: the test session expired; "+std::to_string(g_table.clearFor(""))+" running sample(s) dropped; injection off");
        return false;
    }
    return true;
}
bool add_line(const std::string& line,std::string& why,std::vector<std::string>& log){
    std::lock_guard<std::mutex> l(g.lock);
    if(!live(log)){why="no test session";return false;}
    if(g.commands>=MaxSessionCommands){why="the session's 2000 commands are used";return false;}
    ctl::InjectCommand c;
    if(!ctl::parseInject(line,&g.applied,c,why))return false;
    why=g_table.add(c,now_ms());
    if(!why.empty())return false;
    ++g.commands;
    return true;
}
int take_file(const std::string& text,std::vector<std::string>& log){
    if(text.size()>MaxFileBytes){log.push_back("inject: inject.txt is larger than 4096 bytes; nothing read");return 0;}
    std::string nonce;{std::lock_guard<std::mutex> l(g.lock);nonce=g.nonce;}
    std::istringstream in(text);std::string line;
    bool first=true;int commands=0,accepted=0;
    while(std::getline(in,line)){
        const auto t=ctl::trim(line);if(t.empty())continue;
        if(first){first=false;if(t!="nonce="+nonce){log.push_back("inject: inject.txt does not name this session's nonce; nothing read");return 0;}continue;}
        if(++commands>MaxFileCommands){log.push_back("inject: inject.txt has more than 32 commands; the rest were not read");break;}
        std::string why;
        if(add_line(t,why,log)){++accepted;log.push_back("inject: accepted: "+t);}
        else log.push_back("inject: refused \""+t+"\": "+why);
    }
    if(first)log.push_back("inject: inject.txt is empty");
    return accepted;
}
bool arm_with(const ctl::Section& applied){
    if(!applied.error.empty())return false;
    g_table.clearFor("");
    std::lock_guard<std::mutex> l(g.lock);
    g.applied=applied;g.commands=0;g.armed=true;
    return true;
}
}

bool parse_session(const std::string& text,std::uint64_t now_unix,std::string& nonce,std::uint64_t& expires_unix,std::string& why){
    nonce.clear();expires_unix=0;
    if(text.size()>512){why="inject.on is larger than 512 bytes";return false;}
    std::istringstream in(text);std::string line;bool expiry=false;
    while(std::getline(in,line)){
        const auto t=ctl::trim(line);
        if(t.rfind("nonce=",0)==0)nonce=t.substr(6);
        else if(t.rfind("expires=",0)==0){
            const auto v=t.substr(8);
            if(v.empty()||v.size()>12||v.find_first_not_of("0123456789")!=std::string::npos){why="expires= is not unix seconds";return false;}
            expires_unix=std::strtoull(v.c_str(),nullptr,10);expiry=true;
        }
    }
    if(nonce.size()<8||nonce.size()>64){why="inject.on names no nonce= of 8-64 letters/digits";return false;}
    for(char c:nonce)if(!((c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z'))){why="the nonce is not letters/digits";return false;}
    if(!expiry){why="inject.on names no expires=";return false;}
    if(expires_unix<=now_unix){why="the test session in inject.on has expired";return false;}
    if(expires_unix-now_unix>MaxSessionSeconds){why="the test session in inject.on ends more than an hour from now";return false;}
    return true;
}

std::vector<std::string> init(const ctl::Section& applied,bool sends_force){
    std::vector<std::string> log;
    const auto on=dbce_path(L"inject.on");
    if(on.empty()||GetFileAttributesW(on.c_str())==INVALID_FILE_ATTRIBUTES)return log;   // not requested
    auto refuse=[&](const std::string& why){log.push_back("inject: test injection refused: "+why);return log;};
    if(sends_force)return refuse("this build can send force; injected runs need the output mute");
    FILETIME ft;GetSystemTimeAsFileTime(&ft);
    const std::uint64_t now_unix=((std::uint64_t(ft.dwHighDateTime)<<32|ft.dwLowDateTime)/10000000ull)-11644473600ull;
    std::string nonce,why;std::uint64_t expires=0;
    if(!parse_session(read_bytes(on,512),now_unix,nonce,expires,why))return refuse(why);
    if(!applied.error.empty())return refuse("the canonical [Controls] does not apply: "+applied.error);
    if(applied.bound.empty())return refuse("the canonical [Controls] binds nothing");
    arm_with(applied);
    {std::lock_guard<std::mutex> l(g.lock);
     g.nonce=nonce;g.expires_ms=now_ms()+(expires-now_unix)*1000ull;
     g.command_file=dbce_path(L"inject.txt");
     WIN32_FILE_ATTRIBUTE_DATA a;   // only versions written after this start count
     g.last_write=GetFileAttributesExW(g.command_file.c_str(),GetFileExInfoStandard,&a)?a.ftLastWriteTime:FILETIME{};}
    log.push_back("inject: test injection ARMED (this build sends no force): profile \""+applied.profile+"\" revision "+applied.revision+
                  "; session ends in "+std::to_string(expires-now_unix)+" s");
    return log;
}

bool armed(){std::lock_guard<std::mutex> l(g.lock);return g.armed;}

void poll(){
    std::vector<std::string> log;
    std::wstring file;
    {std::lock_guard<std::mutex> l(g.lock);
     if(!live(log)||g.testing||g.command_file.empty()){for(auto& s:log)std::printf("%s\n",s.c_str());return;}
     const auto now=now_ms();if(now<g.next_poll)return;g.next_poll=now+100;
     WIN32_FILE_ATTRIBUTE_DATA a;
     if(!GetFileAttributesExW(g.command_file.c_str(),GetFileExInfoStandard,&a))return;
     if(CompareFileTime(&a.ftLastWriteTime,&g.last_write)<=0)return;
     g.last_write=a.ftLastWriteTime;   // each version is read once, whatever its fate
     file=g.command_file;
     if(a.nFileSizeHigh||a.nFileSizeLow>MaxFileBytes){log.push_back("inject: inject.txt is larger than 4096 bytes; nothing read");file.clear();}}
    if(!file.empty())take_file(read_bytes(file,MaxFileBytes),log);
    for(auto& s:log)std::printf("%s\n",s.c_str());
}

int deliver(const std::string& dev,DIJOYSTATE2& state,int buttons,int povs,std::vector<std::string>& log){
    {std::lock_guard<std::mutex> l(g.lock);if(!live(log))return 0;}
    if(!g_table.any())return 0;
    ctl::StateView v;
    static_assert(offsetof(DIJOYSTATE2,rglSlider)==offsetof(DIJOYSTATE2,lX)+6*sizeof(LONG),"DIJOYSTATE2 axes are not contiguous");
    v.axes=reinterpret_cast<long*>(&state.lX);
    v.buttons=state.rgbButtons;v.buttonCount=buttons<0?0:buttons>128?128:buttons;
    v.povs=reinterpret_cast<unsigned long*>(state.rgdwPOV);v.povCount=povs<0?0:povs>4?4:povs;
    // Every axis is read in 0..65535 here (DIPROP_RANGE at open).
    return g_table.apply(dev,v,now_ms(),[](int axis,long& lo,long& hi){if(axis<0||axis>7)return false;lo=0;hi=65535;return true;},log);
}

void device_gone(const std::string& dev,std::vector<std::string>& log){
    if(!g_table.any())return;
    if(const int n=g_table.clearFor(dev))log.push_back("inject: "+dev+" read failed or was closed; "+std::to_string(n)+" running sample(s) dropped");
}

bool observe(ObserveData d,std::vector<std::string>& log){
    std::lock_guard<std::mutex> l(g.lock);
    if(!g.armed||g.testing)return false;
    if(!g.view&&!g.view_failed){
        g.view_failed=true;
        const std::string name="Local\\DbceOutRunDecompInjectObserve-"+g.nonce;
        HANDLE map=CreateFileMappingA(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(ObserveData),name.c_str());
        const DWORD error=GetLastError();
        if(!map){log.push_back("inject: observation mapping "+name+" failed ("+std::to_string(error)+")");return false;}
        if(error==ERROR_ALREADY_EXISTS){CloseHandle(map);log.push_back("inject: observation mapping "+name+" already exists; refused, nothing published");return false;}
        auto* v=static_cast<ObserveData*>(MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,sizeof(ObserveData)));
        if(!v){log.push_back("inject: observation mapping "+name+" could not be mapped");CloseHandle(map);return false;}
        *v=ObserveData{};v->version=1;   // the mapping lives as long as the process
        g.view=v;g.view_failed=false;
        log.push_back("inject: observation published as "+name);
    }
    if(!g.view)return false;
    auto* seq=reinterpret_cast<volatile LONG*>(&g.view->packetId);
    const auto packet=g.view->packetId;
    InterlockedIncrement(seq);MemoryBarrier();
    d.version=1;d.packetId=packet+1u;d.tickMs=GetTickCount();
    *g.view=d;
    MemoryBarrier();InterlockedIncrement(seq);
    return true;
}

bool test_arm(const ctl::Section& applied){
    {std::lock_guard<std::mutex> l(g.lock);g.testing=true;g.nonce="test";g.expires_ms=0;}
    return arm_with(applied);
}
int test_file(const std::string& text,std::vector<std::string>& log){return take_file(text,log);}
void test_clock(std::uint64_t now_ms_value){std::lock_guard<std::mutex> l(g.lock);g.clock=now_ms_value;}
}
