// [Triple] Screens (triple_screens.hpp) and the display's unstretched fit (xbox/source/display_fit.hpp): the STD-022
// names, the Separate-monitors span rule (the Tweaks fork's TripleSpanFromMonitors) and the centred 16:9 frame.
#include "triple_screens.hpp"
#include "triple_rig.hpp"
#include "display_fit.hpp"
#include "platform/ui_spacing.hpp"
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
    // [Graphics] UIScalingMode (display_fit.hpp): "0" / "1" / "2" (blanks around; empty = 2); anything else gives 2 with a
    // note, and 1 notes that it is partial (step 2a). The 2D rectangle: 2 = the centred 4:3 on 16:9 and on a span, as
    // before (none on a 4:3 frame), no spacing; 0 = none (stretched) on 16:9, the centre panel on a span (a third of its
    // aspect), so the HUD stays on the centre monitor; 1 = the centred 4:3 plus the spacing to the frame's edges, or to
    // the centre panel's on a span (never past it), and nothing on a 4:3 frame.
    {   using outrun::xbox_runtime::parse_ui_scaling_mode;using outrun::xbox_runtime::ui_scaling_rect;
        std::string note;
        CHECK(parse_ui_scaling_mode("",note)==2u&&note.empty());
        CHECK(parse_ui_scaling_mode(" 2 ",note)==2u&&note.empty());
        CHECK(parse_ui_scaling_mode("0\r",note)==0u&&note.empty());
        CHECK(parse_ui_scaling_mode("1",note)==1u&&note.find("is partial")!=std::string::npos);
        CHECK(parse_ui_scaling_mode("3",note)==2u&&note=="UIScalingMode = 3 refused (0, 1 or 2); 2 (keep centred) applies");
        CHECK(parse_ui_scaling_mode("02",note)==2u&&note.find("refused")!=std::string::npos);
        auto rect=[](unsigned mode,bool wide,float span,bool on,double aspect){
            const auto r=ui_scaling_rect(mode,wide,span);return r.on==on&&(!on||std::fabs(r.aspect-aspect)<1e-12)&&(mode==1u||r.spacing==0.);};
        CHECK(rect(2,true,0.f,true,4./3.)&&rect(2,false,0.f,false,0.)&&rect(2,false,48.f/9.f,true,4./3.)&&rect(2,true,48.f/9.f,true,4./3.));
        CHECK(rect(0,true,0.f,false,0.)&&rect(0,false,0.f,false,0.));
        CHECK(rect(0,true,48.f/9.f,true,double(48.f/9.f)/3.)&&std::fabs(ui_scaling_rect(0,true,48.f/9.f).aspect-16./9.)<1e-6);
        const auto m169=ui_scaling_rect(1,true,0.f,16./9.),m219=ui_scaling_rect(1,true,0.f,21./9.),mspan=ui_scaling_rect(1,true,48.f/9.f,16./9.);
        CHECK(m169.on&&m169.aspect==4./3.&&std::fabs(m169.spacing-320./3.)<1e-9);                  // (853.3 - 640) / 2
        CHECK(m219.on&&std::fabs(m219.spacing-240.)<1e-9);                                         // (1120 - 640) / 2
        CHECK(mspan.on&&std::fabs(mspan.spacing-320./3.)<1e-4);                                     // the centre panel's 16:9, not the span's
        CHECK(std::fabs(640.+2.*mspan.spacing-480.*double(48.f/9.f)/3.)<1e-3);                     // 4:3 + both spacings = the centre panel
        CHECK(!ui_scaling_rect(1,false,0.f,4./3.).on&&ui_scaling_rect(1,false,0.f,4./3.).spacing==0.);
        // Tweaks' own HUD shift (margin_px / screen_scale.x, drawn at screen_scale.y): 320 (1 - 4 / (3A)); three quarters
        // of the margin at 16:9 (80 against 106.7), 137.1 at 21:9; the centre panel's on a span; 0 outside mode 1.
        CHECK(std::fabs(m169.hud-80.)<1e-9&&std::fabs(m219.hud-960./7.)<1e-9&&std::fabs(mspan.hud-80.)<1e-4);
        CHECK(std::fabs(m169.hud-0.75*m169.spacing)<1e-9);
        CHECK(ui_scaling_rect(2,true,0.f).hud==0.&&ui_scaling_rect(0,true,48.f/9.f).hud==0.&&ui_scaling_rect(1,false,0.f,4./3.).hud==0.);
        // The footage-layer rule (pc_sprite_2d.hpp, Tweaks' drawFootage): detect moves the left third left and the right
        // third right; left / right force; off or no spacing moves nothing.
        using outrun::platform::UiSpacing;using outrun::platform::ui_spacing_shift;
        CHECK(ui_spacing_shift(UiSpacing::detect,100.f,106.f)==-106.f&&ui_spacing_shift(UiSpacing::detect,500.f,106.f)==106.f);
        // Tweaks' integer cutoffs (SCREEN_ONE_THIRD 213, SCREEN_TWO_THIRD 426): 212.9 left, 213 stays; 425.9 stays, 426 right.
        CHECK(ui_spacing_shift(UiSpacing::detect,320.f,106.f)==0.f&&ui_spacing_shift(UiSpacing::detect,213.f,106.f)==0.f);
        CHECK(ui_spacing_shift(UiSpacing::detect,212.9f,106.f)==-106.f&&ui_spacing_shift(UiSpacing::detect,425.9f,106.f)==0.f);
        CHECK(ui_spacing_shift(UiSpacing::detect,426.f,106.f)==106.f&&ui_spacing_shift(UiSpacing::detect,427.f,106.f)==106.f);
        CHECK(ui_spacing_shift(UiSpacing::left,500.f,106.f)==-106.f&&ui_spacing_shift(UiSpacing::right,100.f,106.f)==106.f);
        CHECK(ui_spacing_shift(UiSpacing::off,100.f,106.f)==0.f&&ui_spacing_shift(UiSpacing::detect,100.f,0.f)==0.f);
        {   using outrun::platform::UiSpacingGuard;using outrun::platform::ui_spacing_scope;
            CHECK(ui_spacing_scope()==UiSpacing::off);
            {   const UiSpacingGuard a(UiSpacing::detect);CHECK(ui_spacing_scope()==UiSpacing::detect);
                {const UiSpacingGuard b(UiSpacing::off);CHECK(ui_spacing_scope()==UiSpacing::off);}
                CHECK(ui_spacing_scope()==UiSpacing::detect);}
            CHECK(ui_spacing_scope()==UiSpacing::off);}
    }
    // [Triple] Views: Planar or Angled, strictly (anything else gives nothing: logged, Planar applies); Angled applies
    // only in a normal process with the borderless window over a span, each refusal named.
    CHECK(parse_triple_projection("Planar")==TripleProjection::planar);
    CHECK(parse_triple_projection(" angled\t")==TripleProjection::angled);
    CHECK(parse_triple_projection("ANGLED")==TripleProjection::angled);
    CHECK(!parse_triple_projection(""));
    CHECK(!parse_triple_projection("Three"));
    CHECK(!parse_triple_projection("on"));
    CHECK(!parse_triple_projection("Angled views"));
    CHECK(std::string(triple_projection_name(TripleProjection::angled))=="Angled");
    CHECK(std::string(triple_projection_name(TripleProjection::planar))=="Planar");
    CHECK(triple_projection_refusal(false,true,TripleScreens::separate,true,48.f/9.f)==nullptr);     // three monitors spanned
    CHECK(triple_projection_refusal(false,true,TripleScreens::surround,false,48.f/9.f)==nullptr);    // a Surround desktop
    CHECK(std::string(triple_projection_refusal(true,true,TripleScreens::separate,true,48.f/9.f))=="not applied in a test session");
    CHECK(std::string(triple_projection_refusal(false,false,TripleScreens::separate,false,0.f))=="needs the borderless window");
    CHECK(std::string(triple_projection_refusal(false,true,TripleScreens::off,false,0.f))=="needs Surround or Separate monitors");
    // sana 35719: Separate monitors whose span failed fell back to the primary monitor; an ultrawide primary (21:9)
    // still gives a planar aspect, but no three panels.
    CHECK(std::string(triple_projection_refusal(false,true,TripleScreens::separate,false,21.f/9.f))=="Separate monitors found no three-monitor span");
    CHECK(std::string(triple_projection_refusal(false,true,TripleScreens::surround,false,0.f))=="needs a window wider than 16:9");
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
    // The three-view clip corrections (triple_views): for game projections at several near planes and FOVs, every
    // panel's clip position computed through the centre's (clip_c C[i]) equals the direct one (v R_i P_i), for points
    // off the panel plane, at the near plane, beside and behind the eye (w <= 0 for the centre); the centre's C is the
    // identity; through the D3D9 pixel-centre fix too (triple_apply undoes and re-applies it); a projection that is not a
    // centred perspective is refused (romi 30680, nene 30684).
    {
        const auto near_rel=[](const std::array<double,4>& a,const std::array<double,4>& b){
            double scale=1e-30,err=0.;for(int k=0;k<4;++k){scale=std::max(scale,std::fabs(b[std::size_t(k)]));err=std::max(err,std::fabs(a[std::size_t(k)]-b[std::size_t(k)]));}
            return err<=1e-9*scale;};
        const auto row=[](const std::array<double,4>& v,const Mat4& m){std::array<double,4> r{};
            for(int j=0;j<4;++j)r[std::size_t(j)]=v[0]*m[j]+v[1]*m[4+j]+v[2]*m[8+j]+v[3]*m[12+j];return r;};
        const auto fix=[](std::array<double,4> p,double fx,double fy){p[0]+=fx*p[3];p[1]-=fy*p[3];return p;};
        for(const double nz:{0.05,0.25,1.0})for(const double fov:{0.6,1.0,1.4}){
            const double t=nz*std::tan(fov/2.),r=t*16./9.;
            const Mat4 p_c=perspective_off_center_rh(-r,r,-t,t,nz,4000.);
            TripleViews v{};CHECK(triple_views(p_c,fixture_rig(8.),v));
            CHECK(v.correction[1]==mat_identity());
            const std::array<std::array<double,4>,7> points{{{300.,20.,-800.,1.},{-0.01,0.02,-nz,1.},{-1000.,-5.,-100.,1.},
                {900.,40.,-30.,1.},{-20.,3.,5.,1.},{0.,0.,1.,1.},{50.,-60.,-3900.,1.}}};
            for(int i=0;i<3;++i)for(const auto& pt:points){
                const auto direct=row(row(pt,v.rotation[i]),v.projection[i]);
                const auto clip_c=row(pt,p_c);
                CHECK(near_rel(row(clip_c,v.correction[i]),direct));
                const double fx=1./640.,fy=1./480.;
                CHECK(near_rel(triple_apply(v.correction[i],fx,fy,fix(clip_c,fx,fy)),fix(direct,fx,fy)));
            }
            // The side panels' forward directions are the rig's +-70 degrees from the centre's.
            const std::array<double,4> ahead_right{std::sin(70.*3.14159265358979323846/180.),0.,-std::cos(70.*3.14159265358979323846/180.),0.};
            const auto in_right=row(ahead_right,v.rotation[2]);
            CHECK(std::fabs(in_right[0])<1e-12&&std::fabs(in_right[1])<1e-12&&std::fabs(in_right[2]+1.)<1e-12);
        }
        // Gap 0: a point on the centre's right edge (NDC x = +1) is on the right panel's left edge (NDC x = -1), at
        // any height and depth: the seam is continuous.
        {   const double nz=1.,t=std::tan(0.5),r=t*16./9.;const Mat4 p_c=perspective_off_center_rh(-r,r,-t,t,nz,4000.);
            TripleViews v{};CHECK(triple_views(p_c,fixture_rig(0.),v));
            for(const double depth:{2.,50.,700.})for(const double hy:{-0.9,0.,0.7}){
                const std::array<double,4> pt{r*depth,hy*t*depth,-depth,1.};
                const auto cc=row(pt,p_c);CHECK(std::fabs(cc[0]/cc[3]-1.)<1e-9);
                const auto rc=row(cc,v.correction[2]);CHECK(std::fabs(rc[0]/rc[3]+1.)<1e-9&&std::fabs(rc[1]/rc[3]-cc[1]/cc[3])<1e-9);
                const auto lc=row(std::array<double,4>{-pt[0],pt[1],pt[2],1.},p_c);const auto lv=row(lc,v.correction[0]);
                CHECK(std::fabs(lv[0]/lv[3]-1.)<1e-9);
            }
        }
        // Refused: identity, orthographic, an off-axis centre, a degenerate rig.
        TripleViews v{};
        CHECK(!triple_views(mat_identity(),fixture_rig(),v));
        Mat4 ortho=mat_identity();ortho[0]=2./640.;ortho[5]=2./480.;ortho[10]=-1./1000.;CHECK(!triple_views(ortho,fixture_rig(),v));
        CHECK(!triple_views(perspective_off_center_rh(-0.5,0.9,-0.4,0.4,1.,4000.),fixture_rig(),v));
        TripleRig flat=fixture_rig();flat.right_angle_deg=95.;
        CHECK(!triple_views(perspective_off_center_rh(-0.7,0.7,-0.4,0.4,1.,4000.),flat,v));
        // The frustum read back from a projection is the one built.
        Frustum fr{};CHECK(perspective_frustum(perspective_off_center_rh(-0.3,0.5,-0.2,0.4,0.25,3000.),fr));
        CHECK(std::fabs(fr.l+0.3)<1e-12&&std::fabs(fr.r-0.5)<1e-12&&std::fabs(fr.b+0.2)<1e-12&&std::fabs(fr.t-0.4)<1e-12&&
              std::fabs(fr.n-0.25)<1e-12&&std::fabs(fr.f-3000.)<1e-6);
    }
    std::printf("triple screens: %u checks\n",checks);
    return 0;
}
