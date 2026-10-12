#pragma once
// Triple-screen rig geometry for the projected views (windows/docs/TRIPLE-SCREENS.md step 4): a self-contained
// transcription of the triple-screen toolkit's Dbce.TripleScreen.Core at e63bd4b (TripleRigDefinition checks,
// TripleRigBuilder.Build, the bounds of ProjectionCalculator.Calculate, EyeDistanceForVerticalFov), shaped like the
// planned native ABI's neutral outputs (toolkit docs/native-bridge.md): per-panel corners and near-plane bounds from the
// rig and the eye, with error codes instead of exceptions. Doubles throughout, as the toolkit. Tested against values
// the toolkit itself produced (windows/tests/test_triple_screens.cpp).
//
// Space (the toolkit's): metres or millimetres alike, eye-relative, x right, y up, z toward the viewer; the centre panel
// faces the eye at z = -eye_distance. Panels are left, centre, right.
#include <array>
#include <cmath>
namespace outrun::desktop {
struct RigVec { double x{},y{},z{}; };
inline RigVec operator+(RigVec a,RigVec b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline RigVec operator-(RigVec a,RigVec b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline RigVec operator*(RigVec a,double s){return {a.x*s,a.y*s,a.z*s};}
inline double dot(RigVec a,RigVec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline RigVec cross(RigVec a,RigVec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline double length(RigVec a){return std::sqrt(dot(a,a));}
inline RigVec normalized(RigVec a){const double l=length(a);return {a.x/l,a.y/l,a.z/l};}

// Three identical panels hinged at the centre panel's edges (TripleRigDefinition).
struct TripleRig {
    double panel_width_mm{},panel_height_mm{};   // visible active area of one panel
    double eye_distance_mm{};                     // eye to the centre panel, along its normal
    double left_angle_deg{},right_angle_deg{};    // how far each side turns toward the viewer (0 = flat), [0, 90)
    double eye_height_mm{};                       // eye above the centre panel's middle
    double bezel_gap_mm{};                        // gap between active areas at each seam, along the side panel
};
enum class RigError { none, panel_size, eye_distance, angle, gap, eye_behind, degenerate };
inline const char* rig_error_name(RigError e){
    switch(e){
    case RigError::panel_size:return "panel width and height must be > 0";
    case RigError::eye_distance:return "eye distance must be > 0";
    case RigError::angle:return "side angles must be in [0, 90) degrees";
    case RigError::gap:return "bezel gap must be finite and >= 0";
    case RigError::eye_behind:return "the eye is not in front of a panel";
    case RigError::degenerate:return "a panel gives a degenerate frustum";
    default:return "none";
    }
}
inline RigError rig_check(const TripleRig& r){
    if(!(r.panel_width_mm>0.)||!(r.panel_height_mm>0.))return RigError::panel_size;
    if(!(r.eye_distance_mm>0.))return RigError::eye_distance;
    if(!(r.left_angle_deg>=0.&&r.left_angle_deg<90.)||!(r.right_angle_deg>=0.&&r.right_angle_deg<90.))return RigError::angle;
    if(!(r.bezel_gap_mm>=0.)||std::isinf(r.bezel_gap_mm))return RigError::gap;
    return RigError::none;
}
// A rectangular panel: corners lower-left, lower-right, upper-left as the user sees them (DisplaySurface).
struct RigPanel { RigVec lower_left,lower_right,upper_left; };
inline RigVec panel_right(const RigPanel& p){return normalized(p.lower_right-p.lower_left);}
inline RigVec panel_up(const RigPanel& p){return normalized(p.upper_left-p.lower_left);}
inline RigVec panel_normal(const RigPanel& p){return normalized(cross(panel_right(p),panel_up(p)));}   // toward the viewer
// TripleRigBuilder.Build: left, centre, right. Call rig_check first.
inline std::array<RigPanel,3> rig_panels(const TripleRig& r){
    const double w=r.panel_width_mm,h=r.panel_height_mm,cy=-r.eye_height_mm,cz=-r.eye_distance_mm;
    const RigVec up{0.,1.,0.};
    const auto surface=[&](RigVec centre,RigVec right){
        const RigVec hr=right*(w/2.),hu=up*(h/2.);
        return RigPanel{centre-hr-hu,centre+hr-hu,centre-hr+hu};
    };
    const double pi=3.14159265358979323846;
    const double la=r.left_angle_deg*pi/180.,ra=r.right_angle_deg*pi/180.;
    const RigVec left_right{std::cos(la),0.,-std::sin(la)};
    const RigVec left_hinge{-w/2.,cy,cz};
    const RigVec left_centre=left_hinge-(left_right*(r.bezel_gap_mm+w/2.));
    const RigVec right_right{std::cos(ra),0.,std::sin(ra)};
    const RigVec right_hinge{w/2.,cy,cz};
    const RigVec right_centre=right_hinge+(right_right*(r.bezel_gap_mm+w/2.));
    return {surface(left_centre,left_right),surface(RigVec{0.,cy,cz},RigVec{1.,0.,0.}),surface(right_centre,right_right)};
}
// ProjectionCalculator.Calculate's near-plane extents, divided by near (so: at near = 1). Its matrix is not needed here:
// the game builds its own (404310).
struct PanelBounds { double left{},right{},bottom{},top{}; };
inline RigError panel_bounds(const RigPanel& p,RigVec eye,PanelBounds& out){
    const RigVec r=panel_right(p),u=panel_up(p),n=panel_normal(p);
    const RigVec ll=p.lower_left-eye,lr=p.lower_right-eye,ul=p.upper_left-eye;
    const double d=-dot(ll,n);
    if(d<=1e-9)return RigError::eye_behind;
    const PanelBounds b{dot(r,ll)/d,dot(r,lr)/d,dot(u,ll)/d,dot(u,ul)/d};
    if(b.right-b.left<=1e-12||b.top-b.bottom<=1e-12)return RigError::degenerate;
    out=b;return RigError::none;
}
// The same view in the game camera's own terms (404310's arguments): the vertical FOV, the aspect, the off-axis shifts
// (404310: top = t (1 + shift_y), bottom = t (shift_y - 1), right = u (1 + shift_x), left = u (shift_x - 1), with
// t = tan(fov / 2) near and u = aspect t), and the yaw of the panel's forward (-normal) from the centre's, in radians,
// positive to the right.
struct PanelView { double yaw{},fov_y{},aspect{},shift_x{},shift_y{}; };
inline PanelView panel_view(const RigPanel& p,const PanelBounds& b){
    const RigVec f=panel_normal(p)*-1.;
    const double t=(b.top-b.bottom)/2.,u=(b.right-b.left)/2.;
    return {std::atan2(f.x,-f.z),2.*std::atan(t),u/t,(b.right+b.left)/(b.right-b.left),(b.top+b.bottom)/(b.top-b.bottom)};
}
// TripleRigDefinition.EyeDistanceForVerticalFov: the eye distance at which the centre panel spans the game's vertical
// FOV (the "match the game's FOV" rig; the sides then still meet the centre at the seams).
inline double eye_distance_for_vertical_fov(double panel_height_mm,double fov_y_deg){
    return panel_height_mm/2./std::tan(fov_y_deg*3.14159265358979323846/360.);
}
// The toolkit's fixture rig: a 32-inch 16:9 panel (PanelDimensions.FromDiagonal), eye 660 mm, both sides 70 degrees.
inline TripleRig fixture_rig(double bezel_gap_mm=0.){
    const double diag=32.*25.4,aspect_diag=std::sqrt(16.*16.+9.*9.);
    return {diag*16./aspect_diag,diag*9./aspect_diag,660.,70.,70.,0.,bezel_gap_mm};
}
}
