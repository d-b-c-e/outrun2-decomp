// Actual 40CBC0 caller rejection, CPU software state only. This fixture records a
// remaining three-view limitation; it does not claim to fix or render the sun.
#include "platform/native_runtime.hpp"
#include "platform/pc_scene_renderer.hpp"
#include "platform/pc_soft_d3d9.hpp"
#include "platform/pc_screen.hpp"
#include "platform/race_course_passes.hpp"
#include "system/exe_image.hpp"
#include "triple_rig.hpp"
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
using namespace outrun;using namespace platform;namespace math=outrun::desktop;
static unsigned checks,correction_calls;
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("sun gate line "+std::to_string(__LINE__)+": " #x);}while(0)
static bool corrections(const float*,float*){++correction_calls;return false;}
struct Device:PcSoftD3D9Device {
    Device():PcSoftD3D9Device(32,32){}
    unsigned draws{},queries{};
    unsigned create_occlusion_query()override{++queries;return 0;}
    void draw_primitive_up(unsigned,unsigned,const void*,unsigned)override{++draws;}
    void draw_indexed_primitive_up(unsigned,unsigned,unsigned,unsigned,const void*,unsigned,const void*,unsigned)override{++draws;}
};
static std::array<double,4> transform(const math::Mat4& m,const math::RigVec& v){
    return {v.x*m[0]+v.y*m[4]+v.z*m[8]+m[12],v.x*m[1]+v.y*m[5]+v.z*m[9]+m[13],v.x*m[2]+v.y*m[6]+v.z*m[10]+m[14],v.x*m[3]+v.y*m[7]+v.z*m[11]+m[15]};
}
static bool inside(const std::array<double,4>& p){return p[3]>0&&p[0]>=-p[3]&&p[0]<=p[3]&&p[1]>=-p[3]&&p[1]<=p[3]&&p[2]>=0&&p[2]<=p[3];}
int main(){try{
    if(!exe_image_autoload()){std::puts("SKIP: OR2_EXE verified cache required for actual course/manager constants");return 77;}
    auto context=std::make_unique<NativeRuntimeContext>();auto& c=*context;
    c.event_state.slots[8].work_token=0x7804b0;
    auto& selected=c.game_mode.course_runtime.selected_7d2de0;
    driving::Bytes selection(selected.data(),selected.size());selection.put32(0,0);selection.put32(0x14,0x7d2de0); // explicit stage0 fallback pointee

    Device d;PcSceneRenderer r(d);d.set_draw_phase(PcDrawPhase::World);
    std::array<unsigned char,0x1000> car{};std::array<unsigned char,0x400> camera{};
    driving::Bytes cam(camera.data(),camera.size());
    r.vehicle_camera=[&](driving::Bytes& v,driving::Bytes& k){v=driving::Bytes(car.data(),car.size());k=cam;return true;};
    for(unsigned i=0;i<16;++i)cam.putf(0x140+i*4,i%5==0?1.f:0.f);
    cam.putf(0xb0,320);cam.putf(0xb4,240);cam.putf(0xa0,1.1f);
    r.globals().w(0x740c98)=0x3f800000;
    driving::pc_matrix_identity(r.matrices());
    const auto projection=math::perspective_off_center_rh(-.1,.1,-.075,.075,.1,1000);
    math::TripleViews views{};CHECK(math::triple_views(projection,math::fixture_rig(),views));
    const auto panel=math::rig_panels(math::fixture_rig())[2];
    const auto behind=(panel.lower_left+(panel.lower_right-panel.lower_left)*.996+(panel.upper_left-panel.lower_left)*.5)*.02;
    const auto edge=(panel.lower_left+(panel.lower_right-panel.lower_left)*.7+(panel.upper_left-panel.lower_left)*.5)*.02;
    CHECK(behind.z>0&&edge.z<0&&edge.x/(-edge.z)*320>470);
    for(const auto& point:{behind,edge})CHECK(inside(transform(math::mat_mul(views.rotation[2],views.projection[2]),point))&&!inside(transform(projection,point)));
    constexpr unsigned Object=PcSceneEffectsState::WorkBase+0x90;
    driving::Bytes work(c.race_effects.scene.work.data(),c.race_effects.scene.work.size());
    native_course_pass_stats()={};g_pc_triple_corrections=corrections;
    unsigned runs=0;
    for(const bool paused:{false,true})for(const bool left:{false,true})for(const bool offscreen:{false,true}){
        const auto point=offscreen?edge:behind;const float x=float(left?-point.x:point.x),y=float(point.y),z=float(point.z);
        CHECK(inside(transform(math::mat_mul(views.rotation[left?0:2],views.projection[left?0:2]),{x,y,z})));
        for(unsigned k=0;k<3;++k){const float f=k==0?x:k==1?y:z;cam.putf(0xd4+k*4,f);cam.putf(0x3a8+k*4,f);}
        work.putf(0x90+0xc,1);work.put16(0x90+0x10,7);work.put16(0x90+0x12,3);
        r.pause_780248=paused;r.set_events(&c.event_state);
        const auto before_camera=camera;const auto before_car=car;const auto before_stack=r.matrices().current_offset;const auto before_depth=r.matrices().depth;
        std::array<unsigned char,64> matrix{};std::memcpy(matrix.data(),r.matrices().current().data(),64);
        CHECK(native_course_pass_leaf(c,r,0x40cbc0,Object));++runs;
        CHECK(native_course_pass_stats().runs==runs&&native_course_pass_stats().failures==0);
        CHECK(d.draws==0&&correction_calls==0&&d.draw_phase()==PcDrawPhase::World);
        CHECK(work.i16(0x90+0x12)==(paused?3:2));CHECK(work.i16(0x90+0x10)==(offscreen?0:7));
        CHECK(camera==before_camera&&car==before_car);
        CHECK(r.matrices().depth==before_depth&&r.matrices().current_offset==before_stack&&std::memcmp(matrix.data(),r.matrices().current().data(),64)==0);
    }
    CHECK(d.queries==1&&d.errors.empty());g_pc_triple_corrections=nullptr;
    std::printf("sun gate: %u assertions;8 actual caller rejections, side-visible loss remains; no rendering\n",checks);return 0;
}catch(const std::exception& e){g_pc_triple_corrections=nullptr;std::fprintf(stderr,"%s\n",e.what());std::fprintf(stderr,"course: %s\n",native_course_pass_stats().last_error.c_str());return 1;}}
