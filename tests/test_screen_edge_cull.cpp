// ScreenEdgeCullFix: 4F0E40's x bounds for course objects (cones, gates) at a widescreen edge.
#include "platform/race_traffic.hpp"
#include <cstdio>
#include <cstdlib>
using namespace outrun::platform;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"screen edge cull line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
// 4F0E40's x test (single precision): culled when low > x + r or x - r > high.
bool visible(float x,float r,const ObjectScreenBounds& b){return !(b.low>x+r)&&!(x-r>b.high);}
}
int main(){
    const float k_low=-320.0f,k_high=320.0f;                     // 5DA2D4 / 6281C8
    const float wide=240.0f*16.0f/9.0f;                           // the port's 16:9 half width (426.67)
    // A cone just past the 4:3 edge but still on a 16:9 screen (x 350, radius 10).
    CHECK(!visible(350.0f,10.0f,object_screen_bounds_4f0e40(k_low,k_high,wide,false)));   // fix off: culled at 4:3
    CHECK(visible(350.0f,10.0f,object_screen_bounds_4f0e40(k_low,k_high,wide,true)));     // fix on: visible
    CHECK(visible(-350.0f,10.0f,object_screen_bounds_4f0e40(k_low,k_high,wide,true)));    // the left edge too
    CHECK(!visible(440.0f,10.0f,object_screen_bounds_4f0e40(k_low,k_high,wide,true)));    // off the wide screen: culled
    // 4:3 control: the PC's own bounds whatever the switch says.
    for(bool fix:{false,true}){
        const auto b=object_screen_bounds_4f0e40(k_low,k_high,320.0f,fix);
        CHECK(b.low==k_low&&b.high==k_high&&!visible(350.0f,10.0f,b)&&visible(300.0f,10.0f,b));
    }
    std::printf("screen edge cull: %u checks\n",checks);
    return 0;
}
