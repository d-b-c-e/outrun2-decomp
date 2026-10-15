// The native wheel path's rescan policy (source/wheel_rescan.hpp): a rig whose bound devices are all open and
// connected never rescans; a device change rescans at the next input sample; a binding without an open, connected
// device (absent, refused to open, lost) rescans every 2 s. No device, no game.
#include "wheel_rescan.hpp"
#include <cstdio>
#include <cstdlib>
namespace {
int checks=0;
void check(bool v,const char* what){++checks;if(!v){std::fprintf(stderr,"wheel rescan: FAIL: %s\n",what);std::exit(1);}}
using namespace outrun::desktop;
}
int main(){
    const WheelBoundDevice live{true,true},lost{true,false},closed{false,false};
    check(!wheel_bound_missing(0,{}),"no applicable binding: nothing missing");
    check(!wheel_bound_missing(3,{live,live,live}),"every binding applied, open and connected: nothing missing");
    check(wheel_bound_missing(3,{live,live}),"a binding not applied (its device absent at the rescan or refused to open)");
    check(wheel_bound_missing(2,{live,lost}),"a device whose read failed (lost) since its last rescan");
    check(wheel_bound_missing(2,{closed,live}),"a lost device the rescan closed");

    const auto t0=WheelClock::now(),due=t0+WheelMissingRescan;
    for(const auto at:{t0,due,due+std::chrono::seconds(60)})
        check(!wheel_rescan_due(false,false,at,due),"steady rig: no rescan, before or long after the 2 s mark");
    check(wheel_rescan_due(true,false,t0,due),"a device change rescans at once, with every binding live");
    check(wheel_rescan_due(true,true,t0,due),"a device change rescans at once while a binding is missing");
    check(!wheel_rescan_due(false,true,due-std::chrono::milliseconds(1),due),"a missing binding waits for the 2 s mark");
    check(wheel_rescan_due(false,true,due,due),"a missing binding rescans at the 2 s mark");
    check(WheelMissingRescan==std::chrono::seconds(2),"the missing-device rescan period stays 2 s");
    std::printf("wheel rescan: %d checks passed\n",checks);
    return 0;
}
