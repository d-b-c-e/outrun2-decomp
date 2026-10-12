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
#include <cstddef>
#include <utility>
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
// ---- The three-view clip corrections (TRIPLE-SCREENS.md "Step 4 mechanism") -------------------------------------
// D3D row-vector matrices, row-major: clip = [x y z 1] M. The game renders once with the centre projection P_c (a
// D3DXMatrixPerspectiveOffCenterRH, as 404310 builds it); panel i's clip position is clip_c C[i] with
// C[i] = P_c^-1 R_i P_i: R_i takes the centre's view space to the panel's (its right / up / normal as columns), P_i is
// the panel's off-axis projection at the same near and far. Linear in homogeneous coordinates, so exact for any vertex
// (w included); composed in double (nene 30684).
using Mat4=std::array<double,16>;
inline Mat4 mat_mul(const Mat4& a,const Mat4& b){Mat4 r{};
    for(int i=0;i<4;++i)for(int j=0;j<4;++j){double s=0.;for(int k=0;k<4;++k)s+=a[i*4+k]*b[k*4+j];r[i*4+j]=s;}return r;}
inline Mat4 mat_identity(){Mat4 m{};m[0]=m[5]=m[10]=m[15]=1.;return m;}
// Gauss-Jordan with partial pivoting; false for a singular matrix.
inline bool mat_inverse(const Mat4& m,Mat4& out){
    double a[4][8];
    for(int i=0;i<4;++i)for(int j=0;j<8;++j)a[i][j]=j<4?m[i*4+j]:(j-4==i?1.:0.);
    for(int c=0;c<4;++c){int p=c;for(int r=c+1;r<4;++r)if(std::fabs(a[r][c])>std::fabs(a[p][c]))p=r;
        if(std::fabs(a[p][c])<1e-300)return false;
        if(p!=c)for(int j=0;j<8;++j)std::swap(a[p][j],a[c][j]);
        const double d=a[c][c];for(int j=0;j<8;++j)a[c][j]/=d;
        for(int r=0;r<4;++r)if(r!=c){const double f=a[r][c];if(f!=0.)for(int j=0;j<8;++j)a[r][j]-=f*a[c][j];}}
    for(int i=0;i<4;++i)for(int j=0;j<4;++j)out[i*4+j]=a[i][j+4];
    return true;
}
// D3DXMatrixPerspectiveOffCenterRH (z in [0, 1]).
inline Mat4 perspective_off_center_rh(double l,double r,double b,double t,double n,double f){
    Mat4 m{};m[0]=2.*n/(r-l);m[5]=2.*n/(t-b);m[8]=(l+r)/(r-l);m[9]=(t+b)/(t-b);m[10]=f/(n-f);m[11]=-1.;m[14]=n*f/(n-f);return m;
}
// The near-plane frustum of a D3DXMatrixPerspectiveOffCenterRH; false for anything else (an orthographic or custom
// projection: such a draw is not corrected and stays single).
struct Frustum { double l,r,b,t,n,f; };
inline bool perspective_frustum(const Mat4& p,Frustum& out){
    const auto zero=[](double v){return std::fabs(v)<1e-12;};
    if(!zero(p[1])||!zero(p[2])||!zero(p[3])||!zero(p[4])||!zero(p[6])||!zero(p[7])||!zero(p[12])||!zero(p[13])||!zero(p[15]))return false;
    if(std::fabs(p[11]+1.)>1e-9||!(p[0]>0.)||!(p[5]>0.)||zero(p[10])||zero(p[10]+1.))return false;
    const double n=p[14]/p[10],f=p[10]*n/(1.+p[10]);
    if(!(n>0.)||!(f>n)||!std::isfinite(f))return false;
    const double w=2.*n/p[0],h=2.*n/p[5],sx=p[8]*w,sy=p[9]*h;
    out={(sx-w)/2.,(sx+w)/2.,(sy-h)/2.,(sy+h)/2.,n,f};
    return true;
}
// The panel's view space from the centre's: v_i = v_c R_i (rows: the centre axes in the panel's basis).
inline Mat4 panel_rotation(const RigPanel& p){
    const RigVec r=panel_right(p),u=panel_up(p),n=panel_normal(p);
    return {r.x,u.x,n.x,0., r.y,u.y,n.y,0., r.z,u.z,n.z,0., 0.,0.,0.,1.};
}
// The matched-FOV rig for the projection the game used: the centre panel exactly fills P_c's frustum (its height
// follows the game's aspect, the eye distance its vertical FOV; toolkit EyeDistanceForVerticalFov), the sides at the
// rig's angles and gap, scaled with it. Then per panel the direct view (R_i, P_i) and the correction C[i]; the centre's
// C is the identity. False (nothing written) for a projection that is not a centred off-centre RH perspective, or a
// rig the toolkit would refuse.
struct TripleViews { Mat4 rotation[3],projection[3],correction[3]; };
inline bool triple_views(const Mat4& p_c,const TripleRig& rig,TripleViews& out){
    Frustum fr{};if(!perspective_frustum(p_c,fr))return false;
    const double half_w=(fr.r-fr.l)/2./fr.n,half_h=(fr.t-fr.b)/2./fr.n;
    if(std::fabs(fr.r+fr.l)>1e-9*(fr.r-fr.l)||std::fabs(fr.t+fr.b)>1e-9*(fr.t-fr.b))return false;   // an off-axis centre: not handled
    TripleRig m=rig;m.panel_height_mm=rig.panel_width_mm*half_h/half_w;m.eye_distance_mm=m.panel_height_mm/2./half_h;m.eye_height_mm=0.;
    if(rig_check(m)!=RigError::none)return false;
    const auto panels=rig_panels(m);
    Mat4 inv{};if(!mat_inverse(p_c,inv))return false;
    TripleViews v{};
    for(int i=0;i<3;++i){
        PanelBounds b{};if(panel_bounds(panels[std::size_t(i)],{},b)!=RigError::none)return false;
        v.rotation[i]=i==1?mat_identity():panel_rotation(panels[std::size_t(i)]);
        v.projection[i]=i==1?p_c:perspective_off_center_rh(b.left*fr.n,b.right*fr.n,b.bottom*fr.n,b.top*fr.n,fr.n,fr.f);
        v.correction[i]=i==1?mat_identity():mat_mul(mat_mul(inv,v.rotation[i]),v.projection[i]);
    }
    out=v;return true;
}
// What the geometry shader does to one clip position (mirrors TRIPLE_GS in d3d11_d3d9.cpp): undo the D3D9 pixel-centre
// fix (x + fx w, y - fy w; d3d11_d3d9.cpp:285 and :334), apply C, re-apply the fix (every viewport is the same size).
inline std::array<double,4> triple_apply(const Mat4& c,double fx,double fy,const std::array<double,4>& p){
    const double x=p[0]-fx*p[3],y=p[1]+fy*p[3];
    std::array<double,4> q{};for(int j=0;j<4;++j)q[std::size_t(j)]=x*c[j]+y*c[4+j]+p[2]*c[8+j]+p[3]*c[12+j];
    q[0]+=fx*q[3];q[1]-=fy*q[3];return q;
}
// The toolkit's fixture rig: a 32-inch 16:9 panel (PanelDimensions.FromDiagonal), eye 660 mm, both sides 70 degrees.
inline TripleRig fixture_rig(double bezel_gap_mm=0.){
    const double diag=32.*25.4,aspect_diag=std::sqrt(16.*16.+9.*9.);
    return {diag*16./aspect_diag,diag*9./aspect_diag,660.,70.,70.,0.,bezel_gap_mm};
}
}
