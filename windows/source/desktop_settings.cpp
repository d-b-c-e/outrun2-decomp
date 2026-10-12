#include "desktop_settings.hpp"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
namespace outrun::desktop {
namespace {
std::string lower(std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
std::string trim(const std::string& s){
    const auto a=s.find_first_not_of(" \t\r\n");if(a==std::string::npos)return {};
    return s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);
}
// A value ends at a comment that follows whitespace ("DeviceGuid = {...}   ; DS-8X").
std::string strip_comment(const std::string& v){
    for(std::size_t i=1;i<v.size();++i)if((v[i]==';'||v[i]=='#')&&(v[i-1]==' '||v[i-1]=='\t'))return trim(v.substr(0,i));
    return trim(v);
}
// "[name]" -> lower-case name, else "".
std::string header(const std::string& line){
    const auto t=trim(line);
    if(t.size()<2||t[0]!='[')return {};
    const auto e=t.find(']');
    return e==std::string::npos?std::string():lower(trim(t.substr(1,e-1)));
}
constexpr const char* Template=
"; OutRun 2006 desktop build (OutRunDesktop.exe).\n"
"\n"
"[Controls]\n"
"; The control bindings, in the wheel toolkit's ini-controls-1 form (one line per action, e.g.\n"
"; steer = axis 0 dev={...} prod={...} range=0..65535 rest=32768 travel=+1 name=\"...\").\n"
"; Wheelkit writes them when it applies a rig profile; runtime.log lists every controller with its\n"
"; GUIDs and, for the first two minutes, the buttons pressed and the axes moved.\n"
"Schema = 1\n"
"Profile =\n"
"Revision =\n"
"Transmission = Sequential\n"
"\n"
"[Wheel]\n"
"SteeringSensitivity = 1.0\n"
"SteeringDeadZone = 0.0\n"
"\n"
"[Window]\n"
"; false: a 1280x720 window (or start with --windowed).\n"
"WindowedBorderless = true\n"
"; frames queued ahead of the display: 2 (default), 1 the lowest latency, 0 the plain chain as before.\n"
"FrameLatency = 2\n"
"\n"
"[Telemetry]\n"
"; Forza \"Data Out\" to 127.0.0.1:Port (in SimHub, a Forza Horizon 5 game on that port), plus the\n"
"; OutRun2006Telemetry shared memory of the OutRun 2006 mod. Layout = FM7 sends the mod's 311-byte FM7 Dash instead.\n"
"Enabled = true\n"
"Port = 8000\n";
constexpr const char* ImportHeader=
"; OutRun 2006 desktop build (OutRunDesktop.exe). The wheel bindings of this folder's\n"
"; OutRun2006Tweaks.ini are imported into [Controls] at the first start with the devices connected.\n"
"\n"
"[Wheel]\n"
"SteeringSensitivity = 1.0\n"
"SteeringDeadZone = 0.0\n"
"\n"
"[Window]\n"
"WindowedBorderless = true\n"
"; frames queued ahead of the display: 2 (default), 1 the lowest latency, 0 the plain chain as before.\n"
"FrameLatency = 2\n"
"\n"
"[Telemetry]\n"
"; Forza \"Data Out\" to 127.0.0.1:Port (in SimHub, a Forza Horizon 5 game on that port), plus the\n"
"; OutRun2006Telemetry shared memory of the OutRun 2006 mod. Layout = FM7 sends the mod's 311-byte FM7 Dash instead.\n"
"Enabled = true\n"
"Port = 8000\n";
}
bool Ini::load(const std::string& path){
    values_.clear();sections_.clear();path_=path;
    std::ifstream f(path);if(!f)return false;
    std::ostringstream all;all<<f.rdbuf();
    load_text(all.str(),path);
    return true;
}
void Ini::load_text(const std::string& text,const std::string& path){
    values_.clear();sections_.clear();path_=path;
    std::istringstream f(text);
    std::string line,section;
    while(std::getline(f,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        const auto h=header(line);
        if(!h.empty()){section=h;sections_[section];continue;}
        sections_[section].push_back(line);
        const auto t=trim(line);
        if(t.empty()||t[0]==';'||t[0]=='#')continue;
        const auto eq=t.find('=');if(eq==std::string::npos)continue;
        values_[section+"/"+lower(trim(t.substr(0,eq)))]=strip_comment(t.substr(eq+1));
    }
}
bool Ini::has_section(const std::string& section)const{return sections_.count(lower(section))!=0;}
std::vector<std::string> Ini::section_lines(const std::string& section)const{
    const auto i=sections_.find(lower(section));
    return i==sections_.end()?std::vector<std::string>{}:i->second;
}
std::string Ini::text(const std::string& section,const std::string& key,const std::string& fallback)const{
    const auto i=values_.find(lower(section)+"/"+lower(key));
    return i==values_.end()?fallback:i->second;
}
int Ini::integer(const std::string& section,const std::string& key,int fallback)const{
    const auto v=text(section,key);if(v.empty())return fallback;
    char* end=nullptr;const long n=std::strtol(v.c_str(),&end,10);
    return end&&end!=v.c_str()?int(n):fallback;
}
float Ini::number(const std::string& section,const std::string& key,float fallback)const{
    const auto v=text(section,key);if(v.empty())return fallback;
    char* end=nullptr;const float n=std::strtof(v.c_str(),&end);
    return end&&end!=v.c_str()?n:fallback;
}
WheelSteering wheel_steering(float sensitivity,float dead_zone){
    WheelSteering w;
    if(std::isfinite(sensitivity))w.sensitivity=sensitivity;
    if(std::isfinite(dead_zone))w.dead_zone=std::clamp(dead_zone,0.f,0.9f);
    return w;
}
WheelSteering wheel_steering(const Ini& settings){
    return wheel_steering(settings.number("Wheel","SteeringSensitivity",1.f),settings.number("Wheel","SteeringDeadZone",0.f));
}
bool Ini::flag(const std::string& section,const std::string& key,bool fallback)const{
    const auto v=lower(text(section,key));
    if(v=="true"||v=="1"||v=="yes"||v=="on")return true;
    if(v=="false"||v=="0"||v=="no"||v=="off")return false;
    return fallback;
}
namespace {
// The file's bytes. A missing file is empty; one that exists but cannot be read whole is refused (never treated as
// empty, which would replace it with a single section).
bool read_whole(const std::string& path,std::string& text,std::string& error){
    text.clear();
    const HANDLE h=CreateFileA(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE){
        if(GetLastError()==ERROR_FILE_NOT_FOUND)return true;
        error="cannot read "+path+" (Win32 "+std::to_string(GetLastError())+"); nothing written";return false;
    }
    LARGE_INTEGER size{};bool ok=GetFileSizeEx(h,&size)&&size.QuadPart>=0&&size.QuadPart<=16*1024*1024;
    if(ok){
        text.resize(std::size_t(size.QuadPart));
        DWORD got=0;
        ok=text.empty()||(ReadFile(h,text.data(),DWORD(text.size()),&got,nullptr)&&got==text.size());
    }
    CloseHandle(h);
    if(!ok){text.clear();error="cannot read "+path+" whole; nothing written";}
    return ok;
}
// Written whole to a sibling temp file, flushed, then moved over the original in one step (MoveFileEx
// REPLACE_EXISTING | WRITE_THROUGH): a failure anywhere leaves the original exactly as it was.
bool replace_whole(const std::string& path,const std::string& text,std::string& error){
    const auto temp=path+".tmp";
    const HANDLE h=CreateFileA(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE){error="cannot write "+temp+"; "+path+" is unchanged";return false;}
    DWORD written=0;
    const bool ok=(text.empty()||(WriteFile(h,text.data(),DWORD(text.size()),&written,nullptr)&&written==text.size()))&&FlushFileBuffers(h);
    CloseHandle(h);
    if(!ok){DeleteFileA(temp.c_str());error="cannot write "+temp+"; "+path+" is unchanged";return false;}
    if(!settings_publish()(temp,path)){
        error="cannot replace "+path+" (Win32 "+std::to_string(GetLastError())+"); it is unchanged";DeleteFileA(temp.c_str());return false;}
    return true;
}
bool publish_by_move(const std::string& temp,const std::string& path){
    return MoveFileExA(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
}
}
SettingsPublish& settings_publish(){static SettingsPublish publish=publish_by_move;return publish;}
namespace {
// [section]'s body replaced by edit(its current lines) in one read and one replacement of the file.
template<class Edit> bool rewrite_section(const std::string& path,const std::string& section,Edit edit,std::string& error){
    std::string text;
    if(!read_whole(path,text,error))return false;
    const std::string eol=text.find("\r\n")!=std::string::npos?"\r\n":"\n";
    // Lines with their own line ends, so untouched lines are copied byte for byte.
    std::vector<std::string> lines;
    for(std::size_t pos=0;pos<text.size();){
        const auto nl=text.find('\n',pos);const auto next=nl==std::string::npos?text.size():nl+1;
        lines.push_back(text.substr(pos,next-pos));pos=next;
    }
    if(!lines.empty()&&lines.back().back()!='\n')lines.back()+=eol;
    // A section that appears more than once is refused: Ini reads every copy (the last value wins), so a write to one
    // copy could report Saved and still not be what the game reads (romi 31196).
    const auto want=lower(section);
    {std::size_t copies=0;for(const auto& l:lines)if(header(l)==want)++copies;
     if(copies>1){error="["+section+"] appears "+std::to_string(copies)+" times in "+path+"; nothing written (merge them first)";return false;}}
    // The section runs from its header to the next header or the end; its trailing blank lines stay in place.
    std::size_t start=lines.size(),end=lines.size();
    for(std::size_t i=0;i<lines.size();++i){
        const auto h=header(lines[i]);if(h.empty())continue;
        if(start!=lines.size()){end=i;break;}
        if(h==want)start=i;
    }
    if(start!=lines.size())while(end>start+1&&trim(lines[end-1]).empty())--end;
    std::vector<std::string> current;   // the section's lines as they are, without their line ends
    if(start!=lines.size())for(std::size_t i=start+1;i<end;++i){
        auto l=lines[i];while(!l.empty()&&(l.back()=='\n'||l.back()=='\r'))l.pop_back();current.push_back(l);}
    std::vector<std::string> block{"["+section+"]"+eol};
    for(const auto& l:edit(current))block.push_back(l+eol);
    if(start==lines.size()){
        if(!lines.empty())lines.push_back(eol);
        lines.insert(lines.end(),block.begin(),block.end());
    }else{
        lines.erase(lines.begin()+std::ptrdiff_t(start),lines.begin()+std::ptrdiff_t(end));
        lines.insert(lines.begin()+std::ptrdiff_t(start),block.begin(),block.end());
    }
    text.clear();for(const auto& l:lines)text+=l;
    return replace_whole(path,text,error);
}
}
bool write_section(const std::string& path,const std::string& section,const std::vector<std::string>& body,std::string& error){
    return rewrite_section(path,section,[&](const std::vector<std::string>&){return body;},error);
}
bool write_key(const std::string& path,const std::string& section,const std::string& key,const std::string& value,std::string& error){
    return rewrite_section(path,section,[&](std::vector<std::string> lines){
        bool found=false;
        for(auto& l:lines){
            const auto t=trim(l);
            if(t.empty()||t[0]==';'||t[0]=='#')continue;
            const auto eq=t.find('=');
            if(eq==std::string::npos||lower(trim(t.substr(0,eq)))!=lower(key))continue;
            l=key+" = "+value;found=true;
        }
        if(!found)lines.push_back(key+" = "+value);
        return lines;
    },error);
}
Ini load_desktop_settings(const std::string& folder,bool create_if_missing){
    Ini ini;
    const auto own=folder+"/OutRunDesktop.ini";
    if(ini.load(own)){std::printf("settings: %s\n",own.c_str());return ini;}
    if(!create_if_missing){std::printf("settings: %s absent (read only in a test process)\n",own.c_str());return ini;}
    Ini tweaks;
    const bool legacy=tweaks.load(folder+"/OutRun2006Tweaks.ini")&&tweaks.has_section("DirectInput");
    if(std::FILE* f=std::fopen(own.c_str(),"wb")){std::fputs(legacy?ImportHeader:Template,f);std::fclose(f);}
    ini.load(own);
    std::printf("settings: %s (new%s)\n",own.c_str(),legacy?"; OutRun2006Tweaks.ini bindings to import":"");
    return ini;
}
}
