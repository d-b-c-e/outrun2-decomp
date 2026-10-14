#include "platform/native_runtime.hpp"
#include "platform/race_area_runtime.hpp"
#include "platform/race_area.hpp"
#include "platform/pc_scene_renderer.hpp"
#include "platform/pc_soft_d3d9.hpp"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
using namespace outrun;
namespace {
void require(bool ok,const char* what){if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
struct ThrowingDevice:platform::PcSoftD3D9Device {
    using PcSoftD3D9Device::PcSoftD3D9Device;
    bool throw_on_set{};
    void set_render_state(std::uint32_t state,std::uint32_t value)override{
        if(throw_on_set)throw std::runtime_error("injected device failure");
        PcSoftD3D9Device::set_render_state(state,value);
    }
};
}
int main(){
    auto device=std::make_unique<ThrowingDevice>(640,480);
    auto scene=std::make_unique<platform::PcSceneRenderer>(*device);
    auto runtime=std::make_unique<platform::NativeRuntimeContext>();
    for(unsigned i=0;i<3;++i)driving::pc_matrix_push(scene->matrices());
    scene->matrices().current().put32(0,0x12345678u);
    const auto lod=scene->queue_context().lod_threshold_8999b8;
    platform::PcVehicleDrawCall push_lod{};push_lod.pc=0x4044a0u;
    platform::PcVehicleDrawCall set_lod{};set_lod.pc=0x4044e0u;set_lod.argc=1;set_lod.args[0]=6;
    platform::PcVehicleDrawCall state{};state.pc=platform::PcRaceSetRenderState;
    state.argc=2;state.args[0]=7;state.args[1]=1;
    device->throw_on_set=true;
    for(unsigned i=0;i<80;++i){
        bool threw=false;
        try{(void)platform::native_race_draw_list_execute(*runtime,*scene,{push_lod,set_lod,state});}
        catch(const std::runtime_error&){threw=true;}
        require(threw&&scene->matrices().depth==3&&scene->matrices().current_offset==192&&
                scene->matrices().current().u32(0)==0x12345678u&&
                platform::native_race_area(*runtime).lod_stack_899560.empty()&&
                scene->queue_context().lod_threshold_8999b8==lod,
                "failed list restores matrix and LOD entry state on every face");
    }
    device->throw_on_set=false;
    require(platform::native_race_draw_list_execute(*runtime,*scene,{state})==0&&
            scene->matrices().depth==3&&scene->matrices().current_offset==192,
            "list succeeds after repeated failed faces");
    platform::PcVehicleDrawCall unknown{};unknown.pc=0xdeadbeefu;
    bool threw=false;
    try{(void)platform::native_race_draw_list_execute(*runtime,*scene,{unknown});}
    catch(const std::runtime_error&){threw=true;}
    require(threw&&scene->matrices().depth==3&&scene->matrices().current_offset==192,
            "explicit list error restores nonzero entry stack");
    std::puts("race_area_draw_list: repeated failure accounting passed");
}
