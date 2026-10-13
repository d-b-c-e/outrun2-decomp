// AutoDetectResolution (src/enhancements/video.hpp): Tweaks' GameDefaultConfigOverride rule for the port's options.ini,
// from the inputs the host builds (xbox/source/main.cpp): g_options starts at host_default_resolution, and load_options
// records whether options.ini chose a resolution. No renderer.
#include "enhancements/video.hpp"
#include <cstdio>
#include <cstdlib>
namespace {
int checks=0;
void check(bool v,const char* what){++checks;if(!v){std::fprintf(stderr,"auto resolution: FAIL: %s\n",what);std::exit(1);}}
}
int main(){
    using outrun::enhancements::Resolution;using outrun::enhancements::auto_detect_resolution;
    using outrun::enhancements::host_default_resolution;
    // The host's start: 1920x1440, never an unset height (the 123a975 premise that A1 disproved).
    check(host_default_resolution==Resolution{1920u,1440u},"the host's default is 4:3 at 1440");
    Resolution r=host_default_resolution;
    check(auto_detect_resolution(r,false,1440u)&&r==host_default_resolution,
          "not chosen, a 1440 display: applies (and is logged) though the height is the default's");
    r=host_default_resolution;
    check(auto_detect_resolution(r,false,1080u)&&r.height==1080u,"not chosen: the display's height (the backend snaps it and sets the width)");
    r=host_default_resolution;
    check(auto_detect_resolution(r,false,2160u)&&r.height==2160u,"a 2160 display");
    r=host_default_resolution;
    check(auto_detect_resolution(r,false,720u)&&r.height==720u,"--windowed: 720");
    const Resolution chosen{1280u,720u};
    r=chosen;
    check(!auto_detect_resolution(r,true,1440u)&&r==chosen,"a resolution from options.ini always wins");
    r=host_default_resolution;
    check(!auto_detect_resolution(r,true,2160u)&&r==host_default_resolution,"options.ini chose the default's own value: kept");
    r=host_default_resolution;
    check(!auto_detect_resolution(r,false,0u)&&r==host_default_resolution,"off (or a latched run): the fixed 1920x1440");
    r=chosen;
    check(!auto_detect_resolution(r,true,0u)&&r==chosen,"off with a chosen resolution: unchanged");
    std::printf("auto resolution: %d checks passed\n",checks);
    return 0;
}
