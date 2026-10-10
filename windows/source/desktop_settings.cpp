#include "desktop_settings.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
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
constexpr const char* Template=
"; OutRun 2006 desktop build: wheel, pedals, shifter and button box through DirectInput.\n"
"; Sections and keys are those of OutRun2006Tweaks.ini: the [DirectInput], [DirectInput.Shifter]\n"
"; and [DirectInput.Aux] sections of a Tweaks install can be pasted here unchanged.\n"
"; runtime.log lists every controller (GUID, name, axes, buttons) and, for the first two\n"
"; minutes, every button pressed and the axis positions, to find the numbers below.\n"
"\n"
"[DirectInput]\n"
"UseDirectInputRemap = true\n"
"; auto: the force-feedback controller with the most axes (gamepads skipped), or a GUID.\n"
"DeviceGuid = auto\n"
"; Axes: 0=X 1=Y 2=Z 3=Rx 4=Ry 5=Rz 6=Slider0 7=Slider1.\n"
"SteeringAxis = 0\n"
"SteeringInvert = false\n"
"SteeringSensitivity = 1.0\n"
"AccelerationAxis = 2\n"
"AccelerationInvert = false\n"
"BrakeAxis = 5\n"
"BrakeInvert = false\n"
"; Separate pedal devices (empty: the wheel).\n"
"ThrottleDeviceGuid =\n"
"BrakeDeviceGuid =\n"
"; Buttons 0..127; 128 + hat*4 + direction (0 up, 1 right, 2 down, 3 left) for a POV; -1 unbound.\n"
"ButtonA = 0\n"
"ButtonB = 1\n"
"ButtonX = 2\n"
"ButtonY = 3\n"
"ButtonStart = 7\n"
"ButtonBack = 6\n"
"ButtonGearUp = 4\n"
"ButtonGearDown = 5\n"
"ButtonChangeView = 8\n"
"ButtonSelUp = 128\n"
"ButtonSelDown = 130\n"
"ButtonSelLeft = 131\n"
"ButtonSelRight = 129\n"
"\n"
"[DirectInput.Shifter]\n"
"; A shifter on its own device; empty DeviceGuid disables the section.\n"
"DeviceGuid =\n"
"GearMode = sequential\n"
"ButtonGearUp = 4\n"
"ButtonGearDown = 5\n"
"\n"
"[DirectInput.Aux]\n"
"; A button box or second rim; empty DeviceGuid disables the section.\n"
"DeviceGuid =\n"
"\n"
"[Controls]\n"
"SteeringDeadZone = 0.0\n";
}
bool Ini::load(const std::string& path){
    values_.clear();path_=path;
    std::ifstream f(path);if(!f)return false;
    std::string line,section;
    while(std::getline(f,line)){
        line=trim(line);
        if(line.empty()||line[0]==';'||line[0]=='#')continue;
        if(line[0]=='['){const auto e=line.find(']');section=lower(trim(line.substr(1,e==std::string::npos?std::string::npos:e-1)));continue;}
        const auto eq=line.find('=');if(eq==std::string::npos)continue;
        values_[section+"/"+lower(trim(line.substr(0,eq)))]=strip_comment(line.substr(eq+1));
    }
    return true;
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
bool Ini::flag(const std::string& section,const std::string& key,bool fallback)const{
    const auto v=lower(text(section,key));
    if(v=="true"||v=="1"||v=="yes"||v=="on")return true;
    if(v=="false"||v=="0"||v=="no"||v=="off")return false;
    return fallback;
}
Ini load_desktop_settings(const std::string& folder){
    Ini ini;
    const auto own=folder+"/OutRunDesktop.ini",tweaks=folder+"/OutRun2006Tweaks.ini";
    if(ini.load(own)){std::printf("settings: %s\n",own.c_str());return ini;}
    if(ini.load(tweaks)){std::printf("settings: %s (no OutRunDesktop.ini)\n",tweaks.c_str());return ini;}
    if(std::FILE* f=std::fopen(own.c_str(),"wb")){std::fputs(Template,f);std::fclose(f);}
    ini.load(own);
    std::printf("settings: %s (new)\n",own.c_str());
    return ini;
}
}
