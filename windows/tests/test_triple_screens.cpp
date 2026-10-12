// [Triple] Screens (triple_screens.hpp) and the display's unstretched fit (xbox/source/display_fit.hpp): the STD-022
// names, the Separate-monitors span rule (the Tweaks fork's TripleSpanFromMonitors) and the centred 16:9 frame.
#include "triple_screens.hpp"
#include "display_fit.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace outrun::desktop;
using outrun::xbox_runtime::display_fit;
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
    CHECK(fit_is(1366,768,1280,720,0,0,1365,768));   // 1365.8 rounds down to whole pixels, x = (1366 - 1365) / 2 = 0
    CHECK(fit_is(0,0,1280,720,0,0,0,0));
    CHECK(fit_is(1280,720,0,0,0,0,1280,720));
    std::printf("triple screens: %u checks\n",checks);
    return 0;
}
