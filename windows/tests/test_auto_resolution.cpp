// AutoDetectResolution (src/enhancements/video.hpp): Tweaks' GameDefaultConfigOverride rule for the port's options.ini.
// An unset resolution (height 0) takes the display's height; a set one, or no display height, stays. No renderer.
#include "enhancements/video.hpp"
#include <cstdio>
#include <cstdlib>
namespace {
int checks=0;
void check(bool v,const char* what){++checks;if(!v){std::fprintf(stderr,"auto resolution: FAIL: %s\n",what);std::exit(1);}}
}
int main(){
    using outrun::enhancements::Resolution;using outrun::enhancements::auto_detect_resolution;
    const Resolution unset{};
    check(auto_detect_resolution(unset,1440u)==Resolution{0u,1440u},"unset: the display's height (the backend snaps it and sets the width)");
    check(auto_detect_resolution(unset,2160u).height==2160u&&auto_detect_resolution(unset,720u).height==720u,"any display height");
    const Resolution chosen{1280u,720u};
    check(auto_detect_resolution(chosen,1440u)==chosen,"a resolution from options.ini always wins");
    check(auto_detect_resolution(unset,0u)==unset,"off (or a latched run): the PC's default, unset");
    check(auto_detect_resolution(chosen,0u)==chosen,"off with a chosen resolution: unchanged");
    std::printf("auto resolution: %d checks passed\n",checks);
    return 0;
}
