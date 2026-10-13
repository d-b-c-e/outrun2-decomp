// The OutRun decomp's own reading of OutRunDesktop.ini's controls, for Wheelkit's Apply Controls fixtures (sana
// 40461/40764). It reads the INI as an ordinary start does (desktop_settings.cpp Ini) and makes the backend choice
// (shim_backend_selected). It builds the shim's profile bytes exactly as shim_input_configure_ordinary does: "[Controls]"
// followed by every [Controls] line, from every copy of the section. It runs the shared Core's read_profile with the
// contract parse, then applies OutRun's capability (outrun_capability). The result, printed as JSON, is what the game
// would take. There is no device and no game, and nothing is written.
//   outrun_controls_consumer <OutRunDesktop.ini>     exit 0 read (whatever it found), 1 the file cannot be read, 2 usage
#include "desktop_settings.hpp"
#include "outrun_adapter.hpp"
#include "shim_input.hpp"
#include "controls_store.hpp"
#include <cstdio>
#include <string>
namespace {
std::string quoted(const std::string& s){
    std::string o="\"";
    for(const unsigned char c:s){
        if(c=='"'||c=='\\'){o+='\\';o+=char(c);}
        else if(c<0x20){char u[8];std::snprintf(u,sizeof u,"\\u%04x",unsigned(c));o+=u;}
        else o+=char(c);
    }
    return o+"\"";
}
std::string hex(const dbce::shim::Sha256& h){
    static const char* d="0123456789abcdef";std::string o;
    for(const auto b:h){o+=d[(b>>4)&15];o+=d[b&15];}
    return o;
}
}
int main(int argc,char** argv){
    namespace desk=outrun::desktop;namespace sh=dbce::shim;namespace ctl=dbce::controls;
    if(argc!=2){std::fprintf(stderr,"usage: outrun_controls_consumer <OutRunDesktop.ini>\n");return 2;}
    desk::Ini ini;
    if(!ini.load(argv[1])){std::fprintf(stderr,"cannot read %s\n",argv[1]);return 1;}
    const bool shim=desk::shim_backend_selected(ini);
    std::string bytes="[Controls]\n";
    for(const auto& line:ini.section_lines("Controls"))bytes+=line+"\n";
    const auto profile=sh::read_profile(bytes,desk::ControlsReads);   // as an ordinary start reads it (STD-037)
    const auto capability=desk::outrun_capability();
    const auto reason=[&](const std::string& action)->const char*{
        const int i=sh::action_index(action);return i<0?"unknown action":capability.unsupported[std::size_t(i)];};
    const auto& s=profile.section;
    const auto steering=desk::wheel_steering(ini);
    std::string o="{\n \"schema\": \"outrun.controls-consumer@1\",\n";
    o+=" \"backend\": {\"text\": "+quoted(ini.text("Input","Backend",""))+", \"shim\": "+(shim?"true":"false")+"},\n";
    o+=" \"profile\": {\"ok\": "+std::string(profile.ok?"true":"false")+", \"error\": "+quoted(profile.error)+", \"bytes\": "+
       std::to_string(bytes.size())+", \"sha256\": "+quoted(hex(profile.file_hash))+"},\n";
    // Transmission: the adapter's H-pattern mapping is on for exactly "H-pattern" (outrun_adapter.cpp Adapter::Adapter).
    o+=" \"section\": {\"schema\": "+std::to_string(s.schema)+", \"profile\": "+quoted(s.profile)+", \"revision\": "+quoted(s.revision)+
       ", \"transmission\": "+quoted(s.transmission)+", \"hpattern\": "+(s.transmission=="H-pattern"?"true":"false")+
       ", \"error\": "+quoted(s.error)+"},\n";
    o+=" \"bound\": [";
    for(std::size_t i=0;i<s.bound.size();++i){
        const auto& e=s.bound[i];const char* why=reason(e.action);
        o+=std::string(i?",":"")+"\n  {\"action\": "+quoted(e.action)+", \"binding\": "+quoted(ctl::format(e.binding))+
           ", \"delivered\": "+(why?"false":"true")+", \"why\": "+(why?quoted(why):std::string("null"))+"}";
    }
    o+=s.bound.empty()?"],\n":"\n ],\n";
    o+=" \"unbound\": [";
    for(std::size_t i=0;i<s.unbound.size();++i)o+=std::string(i?", ":"")+quoted(s.unbound[i]);
    o+="],\n \"invalid\": [";
    for(std::size_t i=0;i<s.invalid.size();++i)o+=std::string(i?", ":"")+"{\"action\": "+quoted(s.invalid[i].first)+", \"why\": "+quoted(s.invalid[i].second)+"}";
    o+="],\n \"capability\": {\"delivered\": [";
    std::string unsupported;bool first=true,first_u=true;
    for(int i=0;i<sh::ActionCount;++i){
        const char* why=capability.unsupported[std::size_t(i)];
        if(!why){o+=std::string(first?"":", ")+quoted(sh::action_name(i));first=false;}
        else{unsupported+=std::string(first_u?"":", ")+quoted(sh::action_name(i))+": "+quoted(why);first_u=false;}
    }
    o+="], \"unsupported\": {"+unsupported+"}},\n";
    char wheel[96];std::snprintf(wheel,sizeof wheel,"{\"sensitivity\": %.9g, \"deadZone\": %.9g}",double(steering.sensitivity),double(steering.dead_zone));
    o+=" \"wheel\": "+std::string(wheel)+"\n}\n";
    std::fputs(o.c_str(),stdout);
    return 0;
}
