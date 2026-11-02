// Actual desktop source producer/mesh/shader bridge, CPU only. No window/backend.
#include "curve_scene_profile.hpp"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <limits>
using namespace outrun::desktop;namespace pf=outrun::platform;static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"curve profile:%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static std::array<double,4> transform_clip(const std::array<double,4>& p,const float* m){std::array<double,4> q{};for(unsigned j=0;j<4;++j)for(unsigned i=0;i<4;++i)q[j]+=p[i]*m[i*4+j];return q;}
int main(){
    CHECK(!pf::pc_curve_active()&&pf::g_pc_curve_mesh_provider==nullptr);
    auto profile=std::make_unique<CurveSceneProfile>();auto rig=fixture_rig(8);rig.panel_height_mm=rig.panel_width_mm*9./16.;
    for(double radius:{0.,1500.}){
        CHECK(curve_scene_profile(rig,radius,*profile));
        for(const auto size:{std::array<unsigned,2>{6,2},std::array<unsigned,2>{7680,1440},std::array<unsigned,2>{16383,16384}}){
            pf::PcCurveMeshView v;CHECK(curve_scene_mesh_view(*profile,size[0],size[1],v)&&pf::pc_curve_mesh_valid(v));
            CHECK(v.vertices==profile->mesh.panels.data()&&v.indices==profile->mesh.indices.data()&&v.shader_source==CurveCompositorHlsl);
            CHECK(v.vertex_bytes==115440&&v.index_count==13824&&v.vertices_per_panel==2405&&v.stride==16);
            for(unsigned i=0;i<3;++i){CurveSampleRect r;CHECK(curve_sample_rect(size[0],size[1],i,r));
                for(unsigned k=0;k<4;++k)CHECK(v.sample_rects[i][k]==r.map[k]&&v.sample_rects[i][4+k]==r.limits[k]);}
            auto bad=v;bad.stride=12;CHECK(!pf::pc_curve_mesh_valid(bad));bad=v;bad.vertex_bytes++;CHECK(!pf::pc_curve_mesh_valid(bad));
            bad=v;bad.inner[1][2]=0;CHECK(!pf::pc_curve_mesh_valid(bad));bad=v;bad.shader_source=nullptr;CHECK(!pf::pc_curve_mesh_valid(bad));
            bad=v;bad.sample_rects[0][0]=std::numeric_limits<float>::quiet_NaN();CHECK(!pf::pc_curve_mesh_valid(bad));
        }
        for(double degrees:{20.,40.,70.,110.,140.}){
            const double near=.1,top=near*std::tan(degrees*3.141592653589793/360),right=top*16./9.;
            auto p=perspective_off_center_rh(-right,right,-top,top,near,10000);std::array<float,16> original;
            for(unsigned i=0;i<16;++i)p[i]=original[i]=float(p[i]);
            std::array<float,48> actual;CHECK(curve_scene_corrections(*profile,original.data(),actual.data()));
            TripleViews direct;CHECK(curve_scene_views(*profile,original.data(),direct));
            Mat4 inverse;CHECK(mat_inverse(p,inverse));
            for(unsigned panel=0;panel<3;++panel){const auto composed=mat_mul(mat_mul(inverse,direct.rotation[panel]),direct.projection[panel]);
                for(unsigned k=0;k<16;++k)CHECK(float(composed[k])==actual[panel*16+k]);}
            TripleViews flat;CHECK(triple_views(p,rig,flat));
            if(radius==0.)for(unsigned panel=0;panel<3;++panel)for(unsigned k=0;k<16;++k)CHECK(std::abs(actual[panel*16+k]-flat.correction[panel][k])<1e-5);
            for(unsigned panel=0;panel<3;++panel){
                float fc[16];for(unsigned k=0;k<16;++k)fc[k]=float(flat.correction[panel][k]);
                const auto& r=profile->inner[panel];
                for(double x:{-.5,0.,.6})for(double y:{-.4,0.,.7})for(double w:{.1,1.,100.}){
                    const std::array<double,4> q{x*w,y*w,.5*w,w};const auto base=transform_clip(q,fc),source=transform_clip(q,actual.data()+16*panel);
                    CHECK(std::abs(source[3]-base[3])<1e-4&&std::abs(source[2]-base[2])<1e-4);
                    CHECK(std::abs(source[0]-(r[2]*base[0]+(2*r[0]+r[2]-1)*base[3]))<1e-4);
                    CHECK(std::abs(source[1]-(r[3]*base[1]+(1-2*r[1]-r[3])*base[3]))<1e-4);
                    // Undo native D3D9 half-pixel fix, apply C, then reapply scaled fix.
                    const double fixed_x=source[0]+source[3]/1280*r[2],fixed_y=source[1]-source[3]/720*r[3];
                    CHECK(std::abs(fixed_x-(r[2]*(base[0]+base[3]/1280)+(2*r[0]+r[2]-1)*base[3]))<1e-4);
                    CHECK(std::abs(fixed_y-(r[3]*(base[1]-base[3]/720)+(1-2*r[1]-r[3])*base[3]))<1e-4);
                    // Actual GS's widened-source point extent must be scaled by inner width/height.
                    for(double sign:{-1.,1.}){
                        const double native_x=base[0]+sign*8./1280*base[3],native_y=base[1]+sign*8./720*base[3];
                        const double gpu_x=source[0]+sign*8./1280*source[3]*r[2],gpu_y=source[1]+sign*8./720*source[3]*r[3];
                        CHECK(std::abs(gpu_x-(r[2]*native_x+(2*r[0]+r[2]-1)*base[3]))<1e-4);
                        CHECK(std::abs(gpu_y-(r[3]*native_y+(1-2*r[1]-r[3])*base[3]))<1e-4);
                    }
                }
            }
        }
    }
    pf::PcCurveMeshView view;CHECK(curve_scene_mesh_view(*profile,7680,1440,view));const auto saved=view.vertices;
    for(unsigned width:{0u,5u,7u,16384u})CHECK(!curve_scene_mesh_view(*profile,width,1440,view)&&view.vertices==saved);
    std::array<float,16> invalid{};std::array<float,48> out;out.fill(17);CHECK(!curve_scene_corrections(*profile,invalid.data(),out.data()));for(float v:out)CHECK(v==17);
    const auto saved_mesh=profile->mesh.panels[1][100];CHECK(!curve_scene_profile(rig,10,*profile)&&profile->mesh.panels[1][100].u==saved_mesh.u);
    CHECK(pf::g_pc_curve_refusal.load()==pf::PcCurveRefusal::none);
    CHECK(!pf::pc_curve_active()&&pf::g_pc_curve_mesh_provider==nullptr); // preparation does not publish GPU readiness
    std::printf("curve scene profile: %u producer/mesh/atlas/point/refusal checks; no renderer enabled\n",checks);
}
