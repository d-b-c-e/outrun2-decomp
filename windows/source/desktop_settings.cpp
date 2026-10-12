#include "desktop_settings.hpp"
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
"WindowedBorderless = true\n";
constexpr const char* ImportHeader=
"; OutRun 2006 desktop build (OutRunDesktop.exe). The wheel bindings of this folder's\n"
"; OutRun2006Tweaks.ini are imported into [Controls] at the first start with the devices connected.\n"
"\n"
"[Wheel]\n"
"SteeringSensitivity = 1.0\n"
"SteeringDeadZone = 0.0\n"
"\n"
"[Window]\n"
"WindowedBorderless = true\n";
}
bool Ini::load(const std::string& path){
    values_.clear();sections_.clear();path_=path;
    std::ifstream f(path);if(!f)return false;
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
    return true;
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
bool write_section(const std::string& path,const std::string& section,const std::vector<std::string>& body,std::string& error){
    std::string text;
    {std::ifstream f(path,std::ios::binary);if(f){std::ostringstream s;s<<f.rdbuf();text=s.str();}}
    const std::string eol=text.find("\r\n")!=std::string::npos?"\r\n":"\n";
    // Lines with their own line ends, so untouched lines are copied byte for byte.
    std::vector<std::string> lines;
    for(std::size_t pos=0;pos<text.size();){
        const auto nl=text.find('\n',pos);const auto next=nl==std::string::npos?text.size():nl+1;
        lines.push_back(text.substr(pos,next-pos));pos=next;
    }
    if(!lines.empty()&&lines.back().back()!='\n')lines.back()+=eol;
    std::vector<std::string> block{"["+section+"]"+eol};
    for(const auto& l:body)block.push_back(l+eol);
    // The section runs from its header to the next header or the end; its trailing blank lines stay in place.
    const auto want=lower(section);
    std::size_t start=lines.size(),end=lines.size();
    for(std::size_t i=0;i<lines.size();++i){
        const auto h=header(lines[i]);if(h.empty())continue;
        if(start!=lines.size()){end=i;break;}
        if(h==want)start=i;
    }
    if(start==lines.size()){
        if(!lines.empty())lines.push_back(eol);
        lines.insert(lines.end(),block.begin(),block.end());
    }else{
        while(end>start+1&&trim(lines[end-1]).empty())--end;
        lines.erase(lines.begin()+std::ptrdiff_t(start),lines.begin()+std::ptrdiff_t(end));
        lines.insert(lines.begin()+std::ptrdiff_t(start),block.begin(),block.end());
    }
    text.clear();for(const auto& l:lines)text+=l;
    const auto temp=path+".tmp";
    {std::ofstream f(temp,std::ios::binary|std::ios::trunc);if(!f){error="cannot write "+temp;return false;}
     f<<text;if(!f){error="cannot write "+temp;return false;}}
    std::remove(path.c_str());
    if(std::rename(temp.c_str(),path.c_str())!=0){error="cannot replace "+path;return false;}
    return true;
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
