// [Triple] Screens (triple_screens.hpp) and the display's unstretched fit (xbox/source/display_fit.hpp): the STD-022
// names, the Separate-monitors span rule (the Tweaks fork's TripleSpanFromMonitors) and the centred 16:9 frame.
#include "triple_screens.hpp"
#include "display_fit.hpp"
#include "driving/pc_camera.hpp"
#include <cmath>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace outrun::desktop;
using outrun::xbox_runtime::display_fit;
using outrun::xbox_runtime::planar_width;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"triple screens line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static bool same(const ScreenRect& a,const ScreenRect& b){return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;}
static bool fit_is(unsigned cw,unsigned ch,unsigned fw,unsigned fh,unsigned x,unsigned y,unsigned w,unsigned h){
    const auto f=display_fit(cw,ch,fw,fh);return f.x==x&&f.y==y&&f.width==w&&f.height==h;
}
int main(){
    // The selector: the three names, any case, blanks around; "Separate" alone; nothing else.
    CHECK(parse_triple_screens("Off")==TripleScreens::off);
    CHECK(parse_triple_screens(" surround ")==TripleScreens::surround);
    CHECK(parse_triple_screens("Separate monitors")==TripleScreens::separate);
    CHECK(parse_triple_screens("SEPARATE MONITORS\t")==TripleScreens::separate);
    CHECK(parse_triple_screens("separate")==TripleScreens::separate);
    CHECK(!parse_triple_screens(""));
    CHECK(!parse_triple_screens("on"));
    CHECK(!parse_triple_screens("1"));
    CHECK(!parse_triple_screens("Separate  monitors"));
    CHECK(!parse_triple_screens("Surround2"));
    CHECK(std::string(triple_screens_name(TripleScreens::separate))=="Separate monitors");
    CHECK(std::string(triple_screens_name(TripleScreens::off))=="Off");
    // The span: the owner's "Sim Racing" layout (three 2560x1440 at x = -2560, 0, 2560) in any order.
    const ScreenRect l{-2560,0,0,1440},c{0,0,2560,1440},r{2560,0,5120,1440};
    ScreenRect out{};
    CHECK(triple_span_from_monitors({l,c,r},out)&&same(out,{-2560,0,5120,1440}));
    out={};CHECK(triple_span_from_monitors({c,r,l},out)&&same(out,{-2560,0,5120,1440}));
    out={};CHECK(triple_span_from_monitors({r,l,c},out)&&same(out,{-2560,0,5120,1440}));
    CHECK(triple_span_from_monitors({{0,0,1920,1080},{1920,0,3840,1080},{3840,0,5760,1080}},out)&&same(out,{0,0,5760,1080}));
    // Refused: a gap, an overlap, a different width, a different height or top, two or four monitors, an empty one.
    const ScreenRect before{9,9,9,9};
    out=before;CHECK(!triple_span_from_monitors({l,c,{2568,0,5128,1440}},out)&&same(out,before));
    CHECK(!triple_span_from_monitors({l,c,{2552,0,5112,1440}},out));
    CHECK(!triple_span_from_monitors({l,c,{2560,0,4480,1440}},out));
    CHECK(!triple_span_from_monitors({l,c,{2560,0,5120,1600}},out));
    CHECK(!triple_span_from_monitors({l,c,{2560,100,5120,1540}},out));
    CHECK(!triple_span_from_monitors({l,c},out));
    CHECK(!triple_span_from_monitors({l,c,r,{5120,0,7680,1440}},out));
    CHECK(!triple_span_from_monitors({},out));
    CHECK(!triple_span_from_monitors({{0,0,0,0},{0,0,0,0},{0,0,0,0}},out));
    CHECK(!triple_span_from_monitors({{0,0,7680,1440}},out));   // Surround: one display
    CHECK(std::string(triple_span_refusal({l,c}))=="not exactly three monitors");
    // The display fit: never a stretch. 16:9 fills; wider gets centred columns (a three-monitor span: exactly the centre
    // monitor; an ultrawide); taller gets centred rows (16:10); 4:3 frames (the setup screen) likewise.
    CHECK(fit_is(2560,1440,1280,720,0,0,2560,1440));
    CHECK(fit_is(1280,720,1280,720,0,0,1280,720));
    CHECK(fit_is(7680,1440,1280,720,2560,0,2560,1440));
    CHECK(fit_is(5760,1080,16,9,1920,0,1920,1080));
    CHECK(fit_is(3440,1440,1280,720,440,0,2560,1440));
    CHECK(fit_is(2560,1600,1280,720,0,80,2560,1440));
    CHECK(fit_is(1920,1200,16,9,0,60,1920,1080));
    CHECK(fit_is(1920,1080,640,480,240,0,1440,1080));
    CHECK(fit_is(1366,768,1280,720,0,0,1365,768));   // 768 x 16/9 = 1365.33, nearest 1365; x = (1366 - 1365) / 2 = 0
    CHECK(fit_is(0,0,1280,720,0,0,0,0));
    CHECK(fit_is(1280,720,0,0,0,0,1280,720));
    // The planar span (step 2): only when not Off and wider than 16:9; the scene width at the chosen height.
    CHECK(planar_aspect(TripleScreens::separate,7680,1440)==float(7680.0/1440.0));
    CHECK(planar_aspect(TripleScreens::surround,7680,1440)==float(7680.0/1440.0));
    CHECK(planar_aspect(TripleScreens::surround,3440,1440)>0.f);            // Surround on an ultrawide: planar 21:9
    CHECK(planar_aspect(TripleScreens::off,7680,1440)==0.f);
    CHECK(planar_aspect(TripleScreens::separate,2560,1440)==0.f);           // refused span: the 16:9 primary
    CHECK(planar_aspect(TripleScreens::surround,2560,1600)==0.f);
    CHECK(planar_aspect(TripleScreens::surround,0,0)==0.f);
    CHECK(planar_width(1440,float(7680.0/1440.0))==7680u);
    CHECK(planar_width(1080,float(5760.0/1080.0))==5760u);
    CHECK(planar_width(1440,float(3440.0/1440.0))==3440u);
    CHECK(planar_width(720,float(7680.0/1440.0))==3840u);
    CHECK(planar_width(1080,float(3440.0/1440.0))==2580u);                 // 2580 exactly
    CHECK(planar_width(1081,float(3440.0/1440.0))%2u==0u);
    // The camera at 48:9 (483C10, Hor+): the vertical FOV +A0 is the 4:3 one; the horizontal +A4 follows the width,
    // 2 atan(tan(fov / 2) x (w / h) x 0.75).
    for(const float width:{640.f,480.f*16.f/9.f,480.f*float(7680.0/1440.0)}){
        std::vector<std::uint8_t> cam(0x3b4);outrun::driving::Bytes c(cam.data(),cam.size());
        const float fov=1.0f;c.putf(0xac,fov);
        outrun::driving::camera_fov_483c10(c,{width,480.f});
        const double h=2.0*std::atan(std::tan(double(fov)*0.5)*0.75);
        const double hz=2.0*std::atan(std::tan(double(fov)*0.5)*(double(width)/480.0)*0.75);
        CHECK(std::fabs(double(c.f32(0xa0))-h)<1e-6&&std::fabs(double(c.f32(0xa4))-hz)<1e-6);
    }
    std::printf("triple screens: %u checks\n",checks);
    return 0;
}
