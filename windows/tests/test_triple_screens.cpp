// [Triple] Screens (triple_screens.hpp) and the display's unstretched fit (xbox/source/display_fit.hpp): the STD-022
// names, the Separate-monitors span rule (the Tweaks fork's TripleSpanFromMonitors) and the centred 16:9 frame.
#include "triple_screens.hpp"
#include "triple_rig.hpp"
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
    // The rig geometry (triple_rig.hpp) against values the toolkit produced at e63bd4b (Dbce.TripleScreen.Core:
    // TripleRigBuilder.Build, ProjectionCalculator.Calculate at near 1, EyeDistanceForVerticalFov; dumped with "R"
    // round-trip formatting by a scratch console app). 1e-9 absolute on millimetres and on the bounds.
    {
        struct Ref { RigVec ll,lr,ul; double l,r,b,t; };
        const auto near_v=[](RigVec a,RigVec b){return length(a-b)<1e-9;};
        const auto check_rig=[&](const TripleRig& rig,const Ref (&ref)[3]){
            CHECK(rig_check(rig)==RigError::none);
            const auto panels=rig_panels(rig);
            for(unsigned i=0;i<3;++i){
                CHECK(near_v(panels[i].lower_left,ref[i].ll)&&near_v(panels[i].lower_right,ref[i].lr)&&near_v(panels[i].upper_left,ref[i].ul));
                PanelBounds b{};CHECK(panel_bounds(panels[i],{},b)==RigError::none);
                CHECK(std::fabs(b.left-ref[i].l)<1e-9&&std::fabs(b.right-ref[i].r)<1e-9&&std::fabs(b.bottom-ref[i].b)<1e-9&&std::fabs(b.top-ref[i].t)<1e-9);
                // 404310's form gives the same bounds back: top = t (1 + sy), bottom = t (sy - 1), right = u (1 + sx),
                // left = u (sx - 1), t = tan(fov / 2), u = aspect t.
                const auto v=panel_view(panels[i],b);const double tt=std::tan(v.fov_y/2.),uu=v.aspect*tt;
                CHECK(std::fabs(tt*(1.+v.shift_y)-b.top)<1e-12&&std::fabs(tt*(v.shift_y-1.)-b.bottom)<1e-12);
                CHECK(std::fabs(uu*(1.+v.shift_x)-b.right)<1e-12&&std::fabs(uu*(v.shift_x-1.)-b.left)<1e-12);
            }
        };
        const Ref gap0[3]{
            {{-596.501044182224,-199.24216778667196,5.693848243638968},{-354.2082982874168,-199.24216778667196,-660},{-596.501044182224,199.24216778667196,5.693848243638968},-0.3748178556779519,0.893427192817133,-0.35669391988924265,0.35669391988924265},
            {{-354.2082982874168,-199.24216778667196,-660},{354.2082982874168,-199.24216778667196,-660},{-354.2082982874168,199.24216778667196,-660},-0.5366792398294195,0.5366792398294195,-0.3018820724040484,0.3018820724040484},
            {{354.2082982874168,-199.24216778667196,-660},{596.501044182224,-199.24216778667196,5.693848243638968},{354.2082982874168,199.24216778667196,-660},-0.8934271928171329,0.3748178556779518,-0.3566939198892426,0.3566939198892426}};
        const Ref gap8[3]{
            {{-599.2372053288293,-199.24216778667196,13.21138920992621},{-356.9444594340222,-199.24216778667196,-652.4824590337128},{-599.2372053288293,199.24216778667196,13.21138920992621},-0.3891398809340173,0.8791051675610677,-0.35669391988924265,0.35669391988924265},
            {{-354.2082982874168,-199.24216778667196,-660},{354.2082982874168,-199.24216778667196,-660},{-354.2082982874168,199.24216778667196,-660},-0.5366792398294195,0.5366792398294195,-0.3018820724040484,0.3018820724040484},
            {{356.9444594340222,-199.24216778667196,-652.4824590337128},{599.2372053288293,-199.24216778667196,13.21138920992621},{356.9444594340222,199.24216778667196,-652.4824590337128},-0.8791051675610675,0.38913988093401725,-0.3566939198892426,0.3566939198892426}};
        const Ref asym[3]{
            {{-656.979035014061,-239.24216778667196,-10.706059761934114},{-357.5892443813424,-239.24216778667196,-652.7495377037069},{-656.979035014061,159.24216778667196,-10.706059761934114},-0.4466179606112264,0.7341751763347288,-0.39877031559738935,0.26542582393471026},
            {{-354.2082982874168,-239.24216778667196,-660},{354.2082982874168,-239.24216778667196,-660},{-354.2082982874168,159.24216778667196,-660},-0.5366792398294195,0.5366792398294195,-0.362488133010109,0.2412760117979878},
            {{356.9444594340222,-239.24216778667196,-652.4824590337128},{599.2372053288293,-239.24216778667196,13.21138920992621},{356.9444594340222,159.24216778667196,-652.4824590337128},-0.8791051675610675,0.38913988093401725,-0.42830404616956974,0.28508379360891545}};
        check_rig(fixture_rig(0.),gap0);
        check_rig(fixture_rig(8.),gap8);
        TripleRig a=fixture_rig(8.);a.left_angle_deg=65.;a.eye_height_mm=40.;
        check_rig(a,asym);
        CHECK(std::fabs(fixture_rig().panel_width_mm-708.4165965748336)<1e-9&&std::fabs(fixture_rig().panel_height_mm-398.4843355733439)<1e-9);
        CHECK(std::fabs(eye_distance_for_vertical_fov(fixture_rig().panel_height_mm,75.)-259.65744839947564)<1e-9);
        // The fixture's views: the centre straight ahead and symmetric, the sides yawed 70 degrees each way.
        const auto p=rig_panels(fixture_rig());PanelBounds b{};
        panel_bounds(p[1],{},b);const auto c=panel_view(p[1],b);
        CHECK(std::fabs(c.yaw)<1e-12&&std::fabs(c.shift_x)<1e-12&&std::fabs(c.shift_y)<1e-12&&std::fabs(c.aspect-16./9.)<1e-9);
        panel_bounds(p[0],{},b);CHECK(std::fabs(panel_view(p[0],b).yaw+70.*3.14159265358979323846/180.)<1e-12);
        panel_bounds(p[2],{},b);CHECK(std::fabs(panel_view(p[2],b).yaw-70.*3.14159265358979323846/180.)<1e-12);
        // Refusals, as the toolkit's argument checks.
        TripleRig bad=fixture_rig();bad.right_angle_deg=90.;CHECK(rig_check(bad)==RigError::angle);
        bad=fixture_rig();bad.bezel_gap_mm=-1.;CHECK(rig_check(bad)==RigError::gap);
        bad=fixture_rig();bad.eye_distance_mm=0.;CHECK(rig_check(bad)==RigError::eye_distance);
        CHECK(panel_bounds(p[1],{0.,0.,-1000.},b)==RigError::eye_behind);
    }
    std::printf("triple screens: %u checks\n",checks);
    return 0;
}
