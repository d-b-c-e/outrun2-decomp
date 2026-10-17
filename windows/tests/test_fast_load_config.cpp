// Read the production Ini parser without creating/writing a settings file.
#include "desktop_settings.hpp"
#include "enhancements/fast_load.hpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
namespace {
unsigned checks{};
void check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"fast_load_config: %s\n",why);std::exit(1);}}
void config(const std::string& ini,bool accepted,bool enabled){
    outrun::desktop::Ini settings;settings.load_text(ini,"memory-only.ini");
    outrun::enhancements::FastLoad policy;
    check(policy.configure(settings.text("Performance","FramerateFastLoad","0"))==accepted,"startup parse acceptance");
    check(policy.enabled()==enabled,"startup enabled state");
    check(settings.text("Controls","Transmission")=="Manual","controls retained");
    check(settings.text("Window","FrameLatency")=="2","window setting retained");
}
}
int main(){
    const std::string retained="[Controls]\nTransmission=Manual\n[Window]\nFrameLatency=2\n";
    config(retained,true,false);
    config(retained+"[Performance]\nFramerateFastLoad=0\n",true,false);
    config(retained+"[pErFoRmAnCe]\nFramerateFastLoad = 2 ; experimental\n",true,true);
    config(retained+"[Performance]\nFramerateFastLoad = 1\n",true,true);
    for(const auto value:{"3","-1","2junk","02","01","true","","2147483648"})
        config(retained+"[Performance]\nFramerateFastLoad="+value+"\n",false,false);
    std::printf("fast_load_config: %u checks passed\n",checks);
}
