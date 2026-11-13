// Actual 40CBC0 caller rejection, CPU software state only. This fixture records a
// remaining three-view limitation; it does not claim to fix or render the sun.
#include "platform/native_runtime.hpp"
#include "platform/pc_scene_renderer.hpp"
#include "platform/pc_soft_d3d9.hpp"
#include "platform/pc_screen.hpp"
#include "platform/retail_asset_store.hpp"
#include "platform/race_course_passes.hpp"
#include "system/exe_image.hpp"
#include "triple_rig.hpp"
#include "platform/pc_render_flush.hpp"
#include "platform/pc_world_coverage.hpp"
#include "driving/pc_d3dx.hpp"
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <map>
#include <cmath>
using namespace outrun;using namespace platform;namespace math=outrun::desktop;
static unsigned checks,correction_calls;
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("sun gate line "+std::to_string(__LINE__)+": " #x);}while(0)
static bool corrections(const float*,float*){++correction_calls;return false;}
struct Device:PcSoftD3D9Device {
    Device():PcSoftD3D9Device(32,32){}
    unsigned draws{},queries{},query_reads{},query_begin{},query_end{};bool query_ready{};
    std::vector<unsigned> primitive_counts;std::vector<PcDrawPhase> phases;
    std::vector<std::uint8_t> packets;std::vector<unsigned> query_begin_ids;
    struct Routed {int panel;std::array<float,16> view,projection;};
    std::vector<Routed> routed;
    std::map<unsigned,unsigned> query_samples;std::map<unsigned,bool> query_ready_by_id;
    template<class T>void capture(const T& value){const auto* b=reinterpret_cast<const std::uint8_t*>(&value);packets.insert(packets.end(),b,b+sizeof value);}
    unsigned create_occlusion_query()override{++queries;return 41+queries;} // synthetic query identity only
    unsigned query_get_data(unsigned q,unsigned& samples)override{CHECK(q>=42&&q<42+queries);++query_reads;const auto sample=query_samples.find(q);samples=sample==query_samples.end()?11500:sample->second;const auto ready=query_ready_by_id.find(q);return (ready==query_ready_by_id.end()?query_ready:ready->second)?0:1;}
    void query_issue(unsigned q,unsigned flags)override{CHECK(q>=42&&q<42+queries);if(flags&2){++query_begin;query_begin_ids.push_back(q);}if(flags&1)++query_end;}
    unsigned draw_indexed_primitive(unsigned type,std::int32_t base,unsigned minimum,unsigned vertices,unsigned start,unsigned count)override{
        if(draw_phase()==PcDrawPhase::SunView)routed.push_back({sun_panel(),transform_view,transform_proj});
        capture(type);capture(base);capture(minimum);capture(vertices);capture(start);capture(count);
        capture(transform_world);capture(transform_view);capture(transform_proj);capture(render);capture(textures);
        capture(indices);capture(declaration);capture(vertex_shader);capture(pixel_shader);capture(vs_constants);capture(ps_constants);
        for(const auto& stream:streams){capture(stream.buffer);capture(stream.offset);capture(stream.stride);}
        ++draws;primitive_counts.push_back(count);phases.push_back(draw_phase());return 0;
    }
    void draw_primitive_up(unsigned,unsigned,const void*,unsigned)override{++draws;}
    void draw_indexed_primitive_up(unsigned,unsigned,unsigned,unsigned,const void*,unsigned,const void*,unsigned)override{++draws;}
};
static std::array<double,4> transform(const math::Mat4& m,const math::RigVec& v){
    return {v.x*m[0]+v.y*m[4]+v.z*m[8]+m[12],v.x*m[1]+v.y*m[5]+v.z*m[9]+m[13],v.x*m[2]+v.y*m[6]+v.z*m[10]+m[14],v.x*m[3]+v.y*m[7]+v.z*m[11]+m[15]};
}
static bool inside(const std::array<double,4>& p){return p[3]>0&&p[0]>=-p[3]&&p[0]<=p[3]&&p[1]>=-p[3]&&p[1]<=p[3]&&p[2]>=0&&p[2]<=p[3];}
// The sun's source views are the three flat views: on curved screens the views are drawn as on flat ones and bent
// afterwards (windows/source/curve_runtime.hpp), so there is no widened curved source to route to.
static bool source_views(const float* projection,PcSunViews& out){
    math::Mat4 p{};for(unsigned k=0;k<16;++k)p[k]=projection[k];math::TripleViews views;
    if(!math::triple_views(p,math::fixture_rig(),views))return false;
    for(unsigned i=0;i<3;++i)for(unsigned k=0;k<16;++k){out.rotation[i][k]=float(views.rotation[i][k]);out.projection[i][k]=float(views.projection[i][k]);}
    return true;
}
int main(int argc,char** argv){try{
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
    if(argc>=2){
        RetailAssetStore store;std::string error;CHECK(retail_asset_store_open(store,argv[1],&error));
        std::vector<std::uint8_t> bytes;CHECK(retail_asset_read_relative_inflated(store,"Common/obj_course_obj_common_pmt.sz",bytes,128u<<20,&error));
        CHECK(r.load_bank(0x57,std::move(bytes),error));CHECK(d.errors.empty());
        for(unsigned i=0;i<16;++i)cam.putf(0x1c0+i*4,i%5==0?1.f:0.f);
        cam.putf(0xf8,0);cam.putf(0xfc,0);cam.putf(0x100,0);cam.putf(0x104,0);cam.putf(0x108,0);cam.putf(0x10c,-1);
        for(unsigned k=0;k<3;++k){const float f=k==2?-10.f:0.f;cam.putf(0xd4+k*4,f);cam.putf(0x3a8+k*4,f);}
        d.query_ready=true;
        for(const bool paused:{false,true}){
            work.putf(0x90+0xc,1);work.put16(0x90+0x10,0);work.put16(0x90+0x12,3);r.pause_780248=paused;
            const auto camera_before=camera;const auto car_before=car;
            const auto depth=r.matrices().depth;const auto offset=r.matrices().current_offset;
            std::array<unsigned char,64> matrix{};std::memcpy(matrix.data(),r.matrices().current().data(),64);
            d.set_render_state(7,1);const auto old_draws=d.draws,old_begin=d.query_begin,old_end=d.query_end;
            CHECK(native_course_pass_leaf(c,r,0x40cbc0,Object));++runs;
            CHECK(native_course_pass_stats().runs==runs&&native_course_pass_stats().failures==0);
            CHECK(d.draws>old_draws&&d.query_begin==old_begin+1&&d.query_end==old_end+1);
            CHECK(work.i16(0x90+0x12)==(paused?3:2)&&work.i16(0x90+0x10)==0);
            CHECK(camera==camera_before&&car==car_before&&d.get_render_state(7)==1);
            CHECK(r.matrices().depth==depth&&r.matrices().current_offset==offset&&std::memcmp(matrix.data(),r.matrices().current().data(),64)==0);
        }
        CHECK(d.errors.empty()&&!d.primitive_counts.empty());
        for(unsigned i=0;i<d.phases.size();++i)CHECK(d.phases[i]==PcDrawPhase::World&&d.primitive_counts[i]>0);
        std::printf("sun center: %u indexed packets, query reads%u begin%u/end%u; real bank, synthetic visibility, no rasterization\n",d.draws,d.query_reads,d.query_begin,d.query_end);
        if(argc==4){
            const std::filesystem::path file=argv[3];const std::string mode=argv[2];
            if(mode=="--write-packets"){
                CHECK(!std::filesystem::exists(file));std::ofstream out(file,std::ios::binary);CHECK(bool(out));out.write(reinterpret_cast<const char*>(d.packets.data()),std::streamsize(d.packets.size()));CHECK(bool(out));
            }else{
                CHECK(mode=="--compare-packets");std::ifstream in(file,std::ios::binary);CHECK(bool(in));const std::vector<std::uint8_t> expected{std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};CHECK(expected==d.packets);
            }
            std::printf("sun legacy packet bytes: %zu, %s\n",d.packets.size(),mode.c_str());
        }

        // Prepare once, then reuse the same legacy view in three independent
        // contexts. This proves state separation, not panel viewport routing.
        work.put16(0x90+0x10,7);work.put16(0x90+0x12,3);r.pause_780248=false;
        const auto frame=native_sun_prepare_frame(c,r,Object);CHECK(work.i16(0x90+0x12)==2&&work.i16(0x90+0x10)==7&&!frame.paused);
        const auto prepared_work=c.race_effects.scene.work;const auto prepared_lights=r.environment().lights_899b98;
        const auto prepared_camera=camera;const auto prepared_car=car;const auto prepared_point=frame.sun_point;
        std::array<NativeSunViewState,3> contexts{};std::array<std::array<std::uint8_t,2>,3> flickers{};
        r.pause_780248=true; // render must use the immutable prepared pause state
        for(unsigned i=0;i<3;++i){
            flickers[i][0]=std::uint8_t(3+2*i);const unsigned expected_query=42+d.queries;d.query_samples[expected_query]=(i+1)*1000;
            const auto begin=d.query_begin,end=d.query_end;const auto depth=r.matrices().depth;const auto offset=r.matrices().current_offset;
            std::array<std::uint8_t,64> matrix{};std::memcpy(matrix.data(),r.matrices().current().data(),64);
            native_sun_render_view(c,r,frame,frame.legacy_view,contexts[i],driving::Bytes(flickers[i].data(),2));
            CHECK(contexts[i].query==expected_query&&contexts[i].samples_89f684==(i+1)*1000&&contexts[i].count_89f66c==0);
            CHECK(d.query_begin==begin+1&&d.query_end==end+1&&d.query_begin_ids.back()==contexts[i].query);
            CHECK(flickers[i][0]==2+2*i&&flickers[i][1]==0);
            CHECK(c.race_effects.scene.work==prepared_work&&r.environment().lights_899b98==prepared_lights&&camera==prepared_camera&&car==prepared_car&&frame.sun_point==prepared_point);
            CHECK(r.matrices().depth==depth&&r.matrices().current_offset==offset&&std::memcmp(matrix.data(),r.matrices().current().data(),64)==0&&d.get_render_state(7)==1);
        }
        CHECK(contexts[0].query!=contexts[1].query&&contexts[1].query!=contexts[2].query&&contexts[0].query!=contexts[2].query);
        const auto queries=d.queries,begin=d.query_begin;d.query_ready_by_id[contexts[1].query]=false;d.query_samples[contexts[1].query]=9999;
        native_sun_render_view(c,r,frame,frame.legacy_view,contexts[1],driving::Bytes(flickers[1].data(),2));
        CHECK(d.queries==queries&&d.query_begin==begin&&contexts[1].samples_89f684==2000&&contexts[0].samples_89f684==1000&&contexts[2].samples_89f684==3000);
        CHECK(std::abs(r.globals().f(0x95aef8)-2000.f/11500.f)<1e-6f&&c.race_effects.scene.work==prepared_work);
        CHECK(d.errors.empty());
        std::printf("sun split: one preparation, independent3 query/flicker contexts; no panel routing or rendering\n");

        // Exercise the actual 40CBC0 route with retail sprites. The formerly
        // rejected side-visible points must reach the matching source eye.
        std::array<float,16> native_projection{},identity{};
        for(unsigned k=0;k<16;++k){native_projection[k]=float(projection[k]);identity[k]=k%5==0?1.f:0.f;}
        native_projection[5]=native_projection[0]*16.f/9.f; // admitted equal16:9 developer source
        render_queue_flush_4052c0(r.flush_context(),r.queue_context());
        render_set_matrix_410f90(r.flush_context(),native_projection,1);render_set_matrix_410f90(r.flush_context(),identity,0);
        d.set_transform(3,native_projection.data());d.set_transform(2,identity.data());
        const auto main_view=r.render_view();const auto car_before=car;
        const auto old_provider=g_pc_sun_views_provider;g_pc_sun_views_provider=source_views;g_pc_triple_corrections=corrections;
        for(const bool left:{false,true})for(const auto point:{behind,edge}){
            const auto eye_point=math::RigVec{left?-point.x:point.x,point.y,point.z};
            for(unsigned k=0;k<3;++k){const float f=float(k==0?eye_point.x:k==1?eye_point.y:eye_point.z);cam.putf(0xd4+k*4,f);cam.putf(0x3a8+k*4,f);}
            const auto unchanged_camera=camera;
            PcSunViews expected;CHECK(source_views(native_projection.data(),expected));
            work.put16(0x90+0x10,0);work.put16(0x90+0x12,3);r.pause_780248=false;
            r.globals().putf(0x95aef8,.123f);const auto before=d.routed.size();const auto queries_before=d.query_begin;
            CHECK(native_course_pass_leaf(c,r,0x40cbc0,Object));
            CHECK(native_course_pass_stats().failures==0&&d.routed.size()>before&&d.query_begin>queries_before);
            bool matching=false;
            for(std::size_t n=before;n<d.routed.size();++n){
                const auto& record=d.routed[n];CHECK(record.panel>=0&&record.panel<3);
                const unsigned i=unsigned(record.panel);matching=matching||i==(left?0u:2u);
                CHECK(record.projection==expected.projection[i]);
                CHECK(record.view==driving::pc_d3dx_matrix_multiply(identity,expected.rotation[i]));
            }
            CHECK(matching&&d.sun_panel()==-1&&d.draw_phase()==PcDrawPhase::World);
            CHECK(pc_world_source_phase(PcDrawPhase::SunView)&&!pc_world_three_views(PcDrawPhase::SunView,false,true));
            CHECK(r.queue_context().opaque.count==0&&r.queue_context().opaque.undo.empty());
            CHECK(d.transform_view==identity&&d.transform_proj==native_projection);
            CHECK(r.render_view().view_95d860==main_view.view_95d860&&r.render_view().panels.valid==main_view.panels.valid);
            CHECK(r.globals().f(0x95aef8)==.123f&&work.i16(0x90+0x12)==2&&camera==unchanged_camera&&car==car_before);
        }
        g_pc_sun_views_provider=old_provider;g_pc_triple_corrections=nullptr;
        // The renderer-owned camera scope restores even when its draw fails.
        bool failed=false;try{r.with_sun_camera(identity,native_projection,[&]{throw std::runtime_error("injected sun camera failure");});}catch(const std::runtime_error&){failed=true;}
        CHECK(failed&&d.transform_view==identity&&d.transform_proj==native_projection&&r.render_view().view_95d860==main_view.view_95d860);
        CHECK(d.errors.empty());
        std::printf("sun routing:4 actual side-point calls, matching source camera packets and drained queues; no rasterization\n");

    }
    std::printf("sun gate: %u assertions;8 actual caller rejections, side-visible loss remains; no rendering\n",checks);return 0;
}catch(const std::exception& e){g_pc_triple_corrections=nullptr;std::fprintf(stderr,"%s\n",e.what());std::fprintf(stderr,"course: %s\n",native_course_pass_stats().last_error.c_str());return 1;}}
