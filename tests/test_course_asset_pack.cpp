#include "platform/native_runtime.hpp"
#include "platform/race_traffic_runtime.hpp"
#include "platform/race_hud_runtime.hpp"
#include "platform/race_area_runtime.hpp"
#include "platform/race_player_car.hpp"
#include "platform/race_ghosts.hpp"
#include "driving/pc_wheel_dynamics.hpp"
#include "driving/pc_x87.hpp"
#include "driving/pc_ground_collision.hpp"
#include "platform/world_source_pack.hpp"
#include "platform/course_collision_pack.hpp"
#include "platform/course_world_runtime.hpp"
#include "platform/embedded_exe_data.hpp"
#include "system/exe_image.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>
using namespace outrun::platform;
namespace {
unsigned checks{};
void require(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
void put32(std::vector<std::uint8_t>& b,std::size_t o,std::uint32_t v){for(unsigned i=0;i<4u;++i)b[o+i]=std::uint8_t(v>>(i*8u));}
std::uint32_t crc32(const std::uint8_t* data,std::size_t size){std::uint32_t crc=0xffffffffu;for(std::size_t i=0;i<size;++i){crc^=data[i];for(unsigned bit=0;bit<8u;++bit)crc=(crc>>1u)^(0xedb88320u&std::uint32_t(0u-(crc&1u)));}return ~crc;}
std::vector<std::uint8_t> synthetic(unsigned version=CourseAssetPackVersion){
    std::vector<std::uint8_t> desc(version==1u?CourseDescriptorPackBytesLegacy:
                                   version==2u?CourseDescriptorPackBytes:
                                   CourseDescriptorPackBytesFull);
    std::memcpy(desc.data(),"ORC78TBL",8u);
    put32(desc,8u,version);put32(desc,12u,66u);put32(desc,16u,77u);
    std::vector<std::uint8_t> course(CourseCvtBlobBytes);
    put32(course,0u,1u);put32(course,4u,0u);
    put32(course,8u,outrun::driving::runtime_category_hash_4f1260("csc_data_cvt_course"));
    put32(course,12u,15u);put32(course,16u,20u);
    const auto course_off=CourseAssetPackHeaderSize+desc.size();
    std::vector<std::uint8_t> out(course_off+course.size());
    const char* magic=version==1u?"OR2CRS1\0":version==2u?"OR2CRS2\0":"OR2CRS3\0";
    std::memcpy(out.data(),magic,8u);
    put32(out,8u,version);put32(out,12u,CourseAssetPackHeaderSize);
    put32(out,16u,std::uint32_t(desc.size()));put32(out,20u,std::uint32_t(course.size()));
    put32(out,24u,crc32(desc.data(),desc.size()));put32(out,28u,crc32(course.data(),course.size()));
    put32(out,32u,CourseAssetPackHeaderSize);put32(out,36u,std::uint32_t(course_off));put32(out,40u,std::uint32_t(out.size()));
    std::memcpy(out.data()+CourseAssetPackHeaderSize,desc.data(),desc.size());std::memcpy(out.data()+course_off,course.data(),course.size());return out;
}
}
int main(int argc,char** argv){
    std::array<std::uint8_t,0x70> primary_fields{},secondary_fields{};
    for(std::size_t offset=0;offset<primary_fields.size();offset+=4u){
        for(unsigned byte=0;byte<4u;++byte){
            primary_fields[offset+byte]=std::uint8_t((0xa0000000u+offset)>>(byte*8u));
            secondary_fields[offset+byte]=std::uint8_t((0xb0000000u+offset)>>(byte*8u));
        }
    }
    constexpr std::array<std::uint32_t,18> pc_fields{{
        0xa000000cu,0xb000000cu,0xb0000018u,0xb000001cu,
        0xa0000020u,0xa0000024u,0xa0000028u,0xa000002cu,
        0xa0000030u,0xb0000030u,0xa0000034u,0xb0000034u,
        0xa0000064u,0xa0000068u,0xb0000068u,0xa000006cu,0u,0u
    }};
    for(std::uint32_t index=0;index<pc_fields.size();++index){
        std::uint32_t field=0xffffffffu;
        require(course_field_44c220(primary_fields.data(),primary_fields.size(),
                                    secondary_fields.data(),secondary_fields.size(),
                                    index,field)&&field==pc_fields[index],
                "original 44C220 VM field dispatch matches all 16 cases plus default");
    }
    std::uint32_t invalid_field=0x12345678u;
    require(!course_field_44c220(nullptr,0u,secondary_fields.data(),secondary_fields.size(),
                                 0u,invalid_field)&&invalid_field==0x12345678u,
            "44C220 refuses a missing selected primary descriptor");
    require(!course_field_44c220(primary_fields.data(),primary_fields.size(),nullptr,0u,
                                 1u,invalid_field)&&invalid_field==0x12345678u,
            "44C220 refuses a missing selected secondary descriptor");
    require(course_field_44c220(nullptr,0u,nullptr,0u,16u,invalid_field)&&invalid_field==0u,
            "44C220 out-of-range default does not dereference either descriptor");
    {   // GetRoadMu 500B30 for the player car (DrivingControl 502C90): while 55A930 ([7F94C0]) is set,
        // 46C450 -> 4FC390 on the C2C request manager's road Mu +764C, with the lookup bound (the
        // trace-run-1605 fault was "road Mu service marked available without lookup").
        auto mu_storage=std::make_unique<NativeRuntimeContext>();
        auto& mu_runtime=*mu_storage;
        require(!race_player_road_mu(mu_runtime).service_available,"road Mu service is off without the C2C request manager");
        mu_runtime.start_mode.manager_state_7f94c0=1u;
        auto& block=native_race_traffic(mu_runtime).requests_7f9460;
        const float manager_mu=0.9f;std::memcpy(block.data()+0x764cu,&manager_mu,4u);
        const auto source=race_player_road_mu(mu_runtime);
        require(source.service_available&&source.lookup!=nullptr&&source.context==block.data()+0x764cu,
                "road Mu is the request manager's +764C through 4FC390");
        std::vector<std::uint8_t> event(0x10f0u),work(0x900u);
        auto wheels=outrun::driving::embedded_wheels(outrun::driving::Bytes(work.data(),work.size()));
        wheels[0].put32(0x14,2u);wheels[1].put32(0x14,4u);wheels[2].put32(0x14,2u);wheels[3].put32(0x14,4u);
        outrun::driving::get_road_mu(outrun::driving::Bytes(event.data(),event.size()),outrun::driving::Bytes(work.data(),work.size()),source);
        // 4FC390: surface 2 -> the manager Mu (fallback 1.0); 4 -> 0.9 <= 0.95 gives 1.5 (fallback 0.7).
        require(wheels[0].f32(0xe8)==manager_mu&&wheels[2].f32(0xe8)==manager_mu&&
                wheels[1].f32(0xe8)==1.5f&&wheels[3].f32(0xe8)==1.5f,
                "player-car wheel Mu follows the request manager's 4FC390, not the fallback constants");
    }
    auto bytes=synthetic();CourseAssetPack pack{};std::string error;
    require(parse_course_asset_pack(bytes.data(),bytes.size(),pack,&error),"valid synthetic OR2CRS3");
    require(pack.descriptors.primary_count==66u&&pack.descriptors.secondary_count==77u&&pack.course_blob.size()==CourseCvtBlobBytes&&pack.descriptors.world_resource_fields_present,"course payload inventory");
    const auto old=synthetic(1u);CourseAssetPack legacy{};
    require(parse_course_asset_pack(old.data(),old.size(),legacy,&error)&&
            !legacy.descriptors.world_resource_fields_present,"OR2CRS1 remains readable without world IDs");
    const auto old2=synthetic(2u);
    require(parse_course_asset_pack(old2.data(),old2.size(),legacy,&error)&&
            legacy.descriptors.world_resource_fields_present,
            "OR2CRS2 remains readable with partial world descriptor fields");
    auto bad=bytes;bad[0]='X';require(!parse_course_asset_pack(bad.data(),bad.size(),pack,&error),"bad course magic rejected");
    bad=bytes;bad.back()^=1u;require(!parse_course_asset_pack(bad.data(),bad.size(),pack,&error),"corrupt course payload rejected");
    bad=bytes;const auto course_off=CourseAssetPackHeaderSize+CourseDescriptorPackBytesFull;
    put32(bad,course_off+20u+0x1cu,66u);
    put32(bad,28u,crc32(bad.data()+course_off,CourseCvtBlobBytes));
    require(!parse_course_asset_pack(bad.data(),bad.size(),pack,&error),"out-of-range course descriptor rejected");
    if(argc>=2){
        CourseAssetPack real{};
        RetailAssetStore retail{};
        const bool retail_root=std::filesystem::is_directory(argv[1]);
        if(retail_root){
            require(retail_asset_store_open(retail,argv[1],&error),"owned retail root opens");
            std::vector<std::uint8_t> image,csc,serialized;
            std::string exe_path;
            ExeSource found{};
            require(exe_image_find_source(argv[1],image,exe_path,&found,&error),"owned EXE image verifies");
            exe_image_install(std::move(image));
            require(retail_asset_read_relative(retail,"Scripts/bin/csc_data_cvt.bin",csc,CourseCvtBlobBytes,&error)&&
                    build_course_asset_pack(csc,serialized,&error)&&
                    parse_course_asset_pack(serialized.data(),serialized.size(),real,&error),
                    "owned EXE and course data produce OR2CRS3");
        }else require(load_course_asset_pack_file(argv[1],real,&error),"generated OR2CRS3 loads");
        require(real.descriptors.primary_count==66u&&real.descriptors.secondary_count==77u&&real.course_blob.size()==1820u,"generated course inventory");
        const auto& world=real.descriptors.storage[35u];
        require(real.descriptors.world_resource_fields_present&&
                outrun::driving::Bytes(const_cast<std::uint8_t*>(world.data()),world.size()).u32(0x00u)==0x23u&&
                outrun::driving::Bytes(const_cast<std::uint8_t*>(world.data()),world.size()).u32(0x04u)==0x162u&&
                outrun::driving::Bytes(const_cast<std::uint8_t*>(world.data()),world.size()).u32(0x38u)==0x19eu&&
                outrun::driving::Bytes(const_cast<std::uint8_t*>(world.data()),world.size()).u32(0x60u)==0x180u,
                "pinned PC descriptor 35 retains marker and world IDs");
        const auto& beach=real.descriptors.storage[15u];
        std::uint32_t source_field=0u;
        require(course_field_44c220(beach.data(),beach.size(),nullptr,0u,0u,
                                     source_field)&&source_field==0x005d7558u&&
                outrun::driving::Bytes(const_cast<std::uint8_t*>(beach.data()),
                                       beach.size()).u32(0x20u)==0x005d7538u,
                "full EXE descriptor retains BEAC collision and course-bin source tokens");
        // Keep the large runtime fixtures out of main's 1 MiB Windows stack.
        auto runtime_storage=std::make_unique<NativeRuntimeContext>();
        auto& runtime=*runtime_storage;
        runtime.mode_descriptors[16u]={0u,0x00499d90u,0x0049c840u,0x00499e50u};
        runtime.mode_state.current=32u;
        require(native_runtime_attach_course_assets(runtime,real),"attach generated course pack");
        require(native_runtime_request_mode(runtime,16u)&&native_runtime_mode_control(runtime,{}),"GAME init invokes native course chain");
        const auto& game=runtime.game_mode;
        require(game.course_load_calls==1u&&game.course_load_success==1u&&game.course_read_calls==1u&&game.course_bytes==CourseCvtBlobBytes,"real csc_data_cvt loaded exactly once");
        require(game.course_provider_loaded&&game.course_loader_phase==3u&&game.course_load.force_sync_7d2d8c==1u,"course provider reaches synchronous phase 3");
        require(game.course_runtime.active_count_7d33c4==CourseCvtRecordCount&&game.course_runtime.max_depth_7d33c0==4&&game.course_runtime.selected_index==0,"real 15-record course tree rebuilt");
        require(game.course_runtime.selected_copy_active&&game.course_matrix_ready,"selected course and matrix retained");
        if(retail_root){
            auto request_storage=std::make_unique<NativeRuntimeContext>();
            auto& request=*request_storage;
            request.game_mode.course_assets=&real;
            request.game_mode.course_retail=&retail;
            request.start_mode.frontend_prepare.event_config_7f94c4[0]=1;
            std::uint32_t request_result=0;
            require(native_start_owned_call(request,0x0046c2f0u,nullptr,0u,request_result)&&request_result==1u,
                    "first request loader call reads the owned course");
            auto& loaded=request.game_mode;
            require(loaded.course_read_calls==1u&&loaded.course_provider_loaded&&loaded.course_matrix_ready&&
                    loaded.course_runtime.active_count_7d33c4>0&&loaded.course_runtime.selected_copy_active,
                    "first request loader call selects course and matrix");
            const auto selected=loaded.course_runtime.selected_7d30a8;
            const auto matrix=loaded.course_runtime.matrix_7d3190;
            const auto work=loaded.course_work;
            const auto reads=loaded.course_read_calls;
            const auto success=loaded.course_load_success;
            std::uint32_t primary_before=0u;
            require(native_start_owned_call(request,0x0044c2d0u,nullptr,0u,primary_before)&&primary_before!=0u,
                    "scene owner reads the first selected primary descriptor");
            request_result=0;
            require(native_start_owned_call(request,0x0046c2f0u,nullptr,0u,request_result)&&request_result==1u,
                    "second phase-3 request loader call remains ready");
            std::uint32_t primary_after=0u;
            require(native_start_owned_call(request,0x0044c2d0u,nullptr,0u,primary_after)&&
                    primary_after==primary_before,
                    "scene owner still reads the selected descriptor after phase 3");
            require(loaded.course_read_calls==reads&&loaded.course_load_success==success&&
                    loaded.course_provider_loaded&&loaded.course_matrix_ready&&
                    loaded.course_runtime.selected_copy_active&&loaded.course_runtime.selected_7d30a8==selected&&
                    loaded.course_runtime.matrix_7d3190==matrix&&loaded.course_work==work,
                    "second phase-3 call keeps selected course and provider storage without rereading");
            request.game_mode.game_variant=5u;
            std::uint32_t placement_result=1u;
            require(native_start_owned_call(request,0x0046c380u,nullptr,0u,placement_result)&&
                    placement_result==0u&&request.start_mode.request_placement_handle_44e590!=0u&&
                    !native_race_traffic(request).latched,
                    "START variant-5 request manager fills its mapped placement local");
            std::uint32_t lane_zero=0u;
            require(native_start_owned_call(request,0x004f03a0u,&lane_zero,1u,placement_result)&&
                    native_race_area(request).placements[0].handle==request.start_mode.request_placement_handle_44e590,
                    "START adopts request placement into AREA lane zero");
            request.race.clock.time_842110=123.5f;
            std::array<std::uint8_t,0x400> request_stack{};
            outrun::driving::Bytes request_bytes(request_stack.data(),request_stack.size());
            for(std::size_t i=0;i<16u;++i)request_bytes.put32(i*4u,(i%5u)==0u?0x3f800000u:0u);
            outrun::driving::PcMatrixStack request_matrices{request_bytes,0,0,16};
            const bool request_control=native_race_requests_event(request,0x0046c200u,0u,request_matrices);
            auto& traffic=native_race_traffic(request);
            float translated_time=0.f;
            std::memcpy(&translated_time,traffic.requests_7f9460.data()+0x180u,sizeof translated_time);
            require(request_control&&traffic.routed.count(0x004659f0u)!=0u&&
                    traffic.routed.count(0x0045bcd0u)!=0u&&
                    !traffic.latched&&
                    std::isfinite(translated_time)&&translated_time>0.f,
                    "variant-5 request control routes the x87 clock, sprite tick, and voice pump");
            // The live control runs once per frame on the renderer's stack (event 0x190):
            // every frame must return the caller's depth and pointer unchanged.
            require(request_matrices.depth==0&&request_matrices.current_offset==0,
                    "variant-5 request control leaves the caller's matrix stack balanced");
            for(unsigned frame=0;frame<4u;++frame){
                request.race.clock.time_842110+=1.f/60.f;
                require(native_race_requests_event(request,0x0046c200u,0u,request_matrices)&&!traffic.latched&&
                        request_matrices.depth==0&&request_matrices.current_offset==0,
                        "repeated variant-5 request control keeps the matrix stack balanced");
            }
            {   // 482140 in GAME ([78026C] 0x10): +18C += [5B0320] (2pi/30), 449470 wraps it to [-pi,pi]
                // (argument slot, then fld) and 449360's translated fsin stores +190. The phases cross
                // pi, need two subtractions, cross -pi, or are NaN (both loops skipped).
                // [78026C] is the AREA view's snapshot of mode_state.current (race_area_runtime.cpp:676),
                // rebuilt at most once per frame (:918). Each mode change starts a new frame, as live.
                const auto mode=request.mode_state.current;
                request.mode_state.current=0x10u;
                ++request.completed_frames;
                (void)native_race_area_memory(request,nullptr,true);
                auto& block=traffic.requests_7f9460;
                const std::uint32_t step_bits=0x3e567750u;   // [5B0320]
                float step;std::memcpy(&step,&step_bits,4u);
                for(const float phase:{0.5f,3.0f,10.0f,-3.5f,std::nanf("")}){
                    std::memcpy(block.data()+0x18cu,&phase,4u);
                    request.race.clock.time_842110+=1.f/60.f;
                    constexpr float pi=3.14159274101257324f,two_pi=6.28318548202514648f;
                    float wrapped=phase+step;
                    while(wrapped>pi)wrapped-=two_pi;
                    while(-pi>wrapped)wrapped+=two_pi;
                    const float sine=outrun::driving::x87_float(outrun::driving::x87_sin(outrun::driving::X87(wrapped)));
                    const bool ran=native_race_requests_event(request,0x0046c200u,0u,request_matrices)&&!traffic.latched;
                    float got_phase,got_sine;
                    std::memcpy(&got_phase,block.data()+0x18cu,4u);std::memcpy(&got_sine,block.data()+0x190u,4u);
                    const bool same=std::isnan(phase)?std::isnan(got_phase)&&std::isnan(got_sine):
                        got_phase==wrapped&&got_sine==sine&&-pi<=got_phase&&got_phase<=pi;
                    if(!ran||!same)std::fprintf(stderr,"GAME request phase %g: ran=%d latched=%d fault=%X phase %g/%g sine %g/%g\n",
                        double(phase),int(ran),int(traffic.latched),unsigned(traffic.fault),double(got_phase),double(wrapped),
                        double(got_sine),double(sine));
                    require(ran&&same&&request_matrices.depth==0&&request_matrices.current_offset==0,
                            "GAME request control wraps its oscillator phase (449470) and stores its x87 sine (449360)");
                }
                request.mode_state.current=mode;
                ++request.completed_frames;
                (void)native_race_area_memory(request,nullptr,true);
            }
            {   // GamePlCar (4A8330) on the loaded course without driving data: 502C90 fails after the
                // parent's 409EF0 push and the gated children then skip its 40A010. Every failed frame
                // must still return the caller's depth and pointer (the mode-13 per-frame leak shape).
                WorldSourcePack world{};CourseCollisionPack coli{};
                require(build_beac_world_source_from_retail(retail,real,world,&error)&&world.entries[0].size!=0u,
                        "owned BEAC world source");
                const auto* coli_bytes=world_source_bytes(world,world.entries[0]);
                require(coli_bytes&&parse_pc_coli0200(coli_bytes,world.entries[0].size,coli,&error),"owned BEAC collision parses");
                auto& lane=request.start_mode.scene_owner_course_world;
                require(lane.admit_lane(0u,coli.pc_coli0200.data(),coli.pc_coli0200.size(),{},&error)&&
                        lane.set_transform(0u,course_world_identity_transform(),&error)&&lane.query_ready(),
                        "owned course world ready for the player car");
                request.game_mode.driving_data=nullptr;
                // The race manager's 451350 (before the push) reads the player car through
                // event 8, whose work is the car 7804B0 in a race, and refuses an open
                // rival/traffic event 9..31 without a native owner: none in this fixture.
                request.event_state.slots[8].work_token=0x7804b0u;
                for(unsigned id=9u;id<32u;++id)request.event_state.slots[id].flags=0u;
                auto& car_world=request.race.car_world;
                for(unsigned frame=0;frame<3u;++frame){
                    // Slot 1 is where GamePlCar's 409EF0 push copies the current matrix: a
                    // sentinel there shows whether the failure came after that push.
                    for(std::size_t k=64u;k<128u;++k)request_stack[k]=0xa5u;
                    car_world.last_error_pc=0u;car_world.last_error.clear();
                    const bool ok=race_player_car_control_4a8330(request,request_matrices,true);
                    bool pushed=false;for(std::size_t k=64u;k<128u;++k)pushed=pushed||request_stack[k]!=0xa5u;
                    std::fprintf(stderr,"GamePlCar frame %u: ok=%d fault=%06X '%s' pushed=%d depth=%d offset=%td\n",frame,int(ok),
                        unsigned(car_world.last_error_pc),car_world.last_error.c_str(),int(pushed),request_matrices.depth,request_matrices.current_offset);
                    require(!ok&&car_world.last_error_pc!=0u,"GamePlCar fails without driving data");
                    require(pushed,"the GamePlCar failure comes after its 409EF0 push");
                    require(request_matrices.depth==0&&request_matrices.current_offset==0,
                            "a failed GamePlCar frame returns the caller's matrix stack unchanged");
                }
            }
            {   // The request manager's road Mu +764C (800AAC). gated-run-2000: the wheels hit road kind 2 and
                // GetRoadMu's 4FC390 returned the manager's +764C as 0 with [7F94C0] = 1. START_Init's variant-5
                // path (49DBC2: 46C2B0(1), then 46C2D0(-1) -> 482B90 -> 481250) stores [62806C] = 1.0 there through
                // the traffic module's memory. mu-fixture-2018 (before the fix): the traffic state block mapped over
                // the manager's tail 800A00..800AF7 took that write (mapped 1, raw 0, separate storage).
                auto& traffic=native_race_traffic(request);
                auto raw_mu=[&]{float v;std::memcpy(&v,traffic.requests_7f9460.data()+0x764cu,4u);return v;};
                request.start_mode.manager_state_7f94c0=1u;
                std::uint32_t reset_eax=0u;
                const bool reset=native_race_requests_call(request,request_matrices,0x0046c2d0u,{0xffffffffu},reset_eax);
                const auto* mapped=traffic.memory.at(0x800aacu,4u);
                float mapped_mu;std::memcpy(&mapped_mu,mapped,4u);
                const bool same_storage=mapped==traffic.requests_7f9460.data()+0x764cu;
                std::fprintf(stderr,"request manager +764C after 46C2D0(-1): ran=%d latched=%d mapped=%g raw=%g same_storage=%d\n",
                    int(reset),int(traffic.latched),double(mapped_mu),double(raw_mu()),int(same_storage));
                require(reset&&!traffic.latched,"START's 46C2D0(-1) runs on the request manager");
                require(mapped_mu==1.0f,"481250 stores [62806C] = 1.0 at the manager's mapped road Mu 800AAC");
                require(same_storage&&raw_mu()==1.0f,"the player car's road Mu reader sees the manager's mapped +764C");
                // The consumers of 800AAC: GetRoadMu (46C450 -> 4FC390 on a road kind 2 wheel) and 46C470 (fld [800AAC],
                // SlipAngleCtrl's assist) both read the requests block through one accessor.
                const auto road=race_player_road_mu(request);
                require(road.service_available&&road.lookup&&road.context==race_requests_road_mu_800aac(request)&&
                        road.lookup(road.context,0u,2u)==1.0f,"GetRoadMu's manager lookup gives the manager's road Mu 1.0 on road kind 2");
                float assist;std::memcpy(&assist,race_requests_road_mu_800aac(request),4u);
                require(assist==1.0f,"46C470's slip assist reads the manager's road Mu 1.0");
                // One owner per address at the boundary: the manager's last field 800AF4 is the requests block's,
                // and the traffic module's 0x38-byte records 0 (800AF8..) and 1 (800B30.., across 800B60) are whole
                // in the traffic state block.
                require(traffic.memory.at(0x800af4u,4u)==traffic.requests_7f9460.data()+0x7694u,
                        "the manager's last field 800AF4 is the requests block's");
                require(traffic.memory.at(0x800af8u,0x38u)==traffic.state.block.data()+(0x800af8u-PcTrafficState::BlockBase)&&
                        traffic.memory.at(0x800b30u,0x38u)==traffic.state.block.data()+(0x800b30u-PcTrafficState::BlockBase),
                        "traffic records 0 and 1 (800AF8..800B67) are whole in the traffic state block");
            }
            {   // 45BCC0 (the voice queue flush: head 7F1C64 = tail 7F1994), called by the request manager (4FB520,
                // 4FB66F, 50EC38, 521420, 521824) in the traffic memory. drive-run-2044 latched there: 7F1994 was not
                // mapped. The HUD's voice state owns the queue words in every memory that runs that code.
                auto& traffic=native_race_traffic(request);
                auto& voice=native_race_hud(request).voice;
                voice.tail_7f1994=7;voice.head_7f1c64=3;
                std::uint32_t flush_eax=0u;
                const bool flushed=native_race_requests_call(request,request_matrices,0x0045bcc0u,{},flush_eax);
                require(flushed&&!traffic.latched,"the request manager's voice flush 45BCC0 runs in the traffic memory");
                require(voice.head_7f1c64==7&&voice.tail_7f1994==7,"45BCC0 sets the HUD's voice head to its tail");
                require(traffic.memory.at(0x7f1994u,4u)==reinterpret_cast<std::uint8_t*>(&voice.tail_7f1994)&&
                        traffic.memory.at(0x7f1c64u,4u)==reinterpret_cast<std::uint8_t*>(&voice.head_7f1c64),
                        "the traffic memory maps the voice tail and head to the HUD's voice state");
                // 45BD30(id, duration, priority), the queue push: the request manager (4FB390, 4FEA00, 5072F0, 507620)
                // calls it as a traffic service, routed to this HUD service (combined-run-2105 latched there). An idle
                // queue plays at once. A busy one keeps only a higher priority than its tail's. The 46C530 gate
                // ([7F94C0] ? [7F95C4] : 0) reads the request manager's +164 in its block.
                voice=NaviVoiceQueue{};
                auto& requests=traffic.requests_7f9460;
                auto put_164=[&](std::uint32_t v){std::memcpy(requests.data()+0x164u,&v,4u);};
                put_164(0u);request.start_mode.manager_state_7f94c0=1u;request.race.clock.time_842110=100.0f;
                const auto idle=native_race_hud_voice_45bd30(request,0xf3,0x3c,4);
                require(idle==1u&&voice.head_7f1c64==0&&voice.entries_7f1998[0].id==0xf3&&voice.entries_7f1998[0].duration==0x3c&&
                        voice.entries_7f1998[0].priority==4&&voice.last_7f8b40==100u,"an idle voice queue plays the pushed voice at once");
                require(native_race_hud_voice_45bd30(request,0xf4,0x3c,4)==0u&&voice.tail_7f1994==0,
                        "a busy queue refuses a voice no higher than its tail's priority");
                require(native_race_hud_voice_45bd30(request,0xf4,0x3c,5)==1u&&voice.tail_7f1994==1&&
                        voice.entries_7f1998[1].id==0xf4&&voice.entries_7f1998[1].priority==5,"a busy queue keeps a higher priority voice");
                put_164(1u);
                require(native_race_hud_voice_45bd30(request,0xf5,0x3c,9)==0u&&voice.tail_7f1994==1,
                        "46C530: the active request manager's +164 holds the voice queue");
                put_164(0u);
            }
            {   // Native branch regression from pinned 68CEB386 disassembly, 4AE0F0..4AE5D4.
                // Original-code parity is UNRUN: ghost_draw_probe needs Linux/WSL and separate pinned assets.
                auto& memory=native_race_traffic(request).memory;
                const auto mark=memory.mark();
                struct Release{PcRaceMemory& memory;std::size_t mark;~Release(){memory.release(mark);}} release{memory,mark};
                std::array<std::uint8_t,12> alpha{};
                std::uint32_t active=0,variant=5;
                memory.map(0x842038u,alpha.data(),alpha.size());
                memory.map(0x7f94c0u,reinterpret_cast<std::uint8_t*>(&active),4);
                memory.map(0x780258u,reinterpret_cast<std::uint8_t*>(&variant),4);
                constexpr std::uint32_t Car=0x7815a0u;
                auto car=memory.bytes(Car,0x10f0u);
                std::memset(car.data(),0,car.size());
                car.put8(0x11,30);car.put8(0xc35,0xff); // an authored traffic model, no optional lamps
                for(const std::uint32_t base:{0xb0u,0xf0u,0xc90u})
                    for(unsigned i=0;i<16u;++i)car.putf(base+i*4u,i%5u==0u?1.0f:0.0f);
                const auto depth=request_matrices.depth;const auto offset=request_matrices.current_offset;
                std::array<std::uint8_t,64> current{};
                std::memcpy(current.data(),request_matrices.current().data(),current.size());
                auto draw=[&](bool enabled,unsigned state,int phase,unsigned mode=5u,bool heart=false){
                    active=enabled?1u:0u;variant=mode;
                    car.put32(0xd14u,state);car.put32(0xd18u,std::uint32_t(phase));car.put32(0xc5cu,heart?0x80000u:0u);
                    std::vector<PcVehicleDrawCall> calls;
                    PcRaceContext ctx{memory,request_matrices,[](const PcRaceCall&)->std::uint32_t{
                        throw std::logic_error("unexpected service in the request draw fixture");},&calls};
                    other_car_draw_4ae0f0(ctx,Car);
                    require(request_matrices.depth==depth&&request_matrices.current_offset==offset&&
                            !std::memcmp(current.data(),request_matrices.current().data(),current.size()),
                            "request draw preserves the caller's matrix depth, pointer and current matrix");
                    return calls;
                };
                auto equal=[](const std::vector<PcVehicleDrawCall>& a,const std::vector<PcVehicleDrawCall>& b){
                    if(a.size()!=b.size())return false;
                    for(std::size_t i=0;i<a.size();++i)
                        if(a[i].pc!=b[i].pc||a[i].argc!=b[i].argc||a[i].args!=b[i].args||a[i].matrix!=b[i].matrix)return false;
                    return true;
                };
                auto item=[&](const std::vector<PcVehicleDrawCall>& calls,std::initializer_list<std::uint32_t> ids){
                    require(calls.size()==ids.size()+3u&&calls.front().pc==0x4044a0u&&
                            calls[1].pc==0x4044e0u&&calls[1].args[0]==8u&&calls.back().pc==0x4044c0u,
                            "request item has stock LOD push, threshold, ordered objects and LOD pop");
                    std::size_t i=2;
                    for(const auto id:ids){const auto& call=calls[i++];
                        require(call.pc==0x405360u&&call.argc==6u&&call.args==std::array<std::uint32_t,6>{id,0,0,0,0xffffffffu,0},
                                "request item submits the authored object ID and six arguments");}
                };
                const auto ordinary=draw(false,0,0);
                bool model=false;for(const auto& call:ordinary)model=model||call.pc==0x405360u;
                require(model,"inactive ordinary traffic model emits object draws");
                for(unsigned state=0;state<=6u;++state){
                    for(const int phase:{-30,0,30}){
                        require(equal(draw(false,state,phase),ordinary),"inactive request states keep the ordinary model path");
                        const auto calls=draw(true,state,phase);
                        if(state==2u||state==3u){
                            require(calls.empty(),"active request states 2 and 3 hide the car");
                            require(draw(true,state,phase,2u,true).empty(),"active hide gates precede Heart Attack items");
                        }else if(state==1u)item(calls,{0x57002cu});
                        else if(state==4u||state==5u){
                            if(state==4u)item(calls,{0x01400000u,0x01ee0010u,0x01ee0010u,0x01ee0010u});
                            else item(calls,{0x01400000u});
                            require(equal(draw(true,state,phase,2u,true),calls),"active request item takes precedence over the Heart flag");
                            float x,y,z;std::memcpy(&x,calls[2].matrix.data()+48,4);
                            std::memcpy(&y,calls[2].matrix.data()+52,4);std::memcpy(&z,calls[2].matrix.data()+56,4);
                            const float height=(state==4u?7.0f:1.0f)+(phase==0?0.0f:(phase>0?1.0f:-1.0f)*(state==4u?0.5f:0.25f));
                            require(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z)&&
                                    std::fabs(std::sqrt(x*x+y*y+z*z)-height)<0.00001f,
                                    "signed quarter-cycle request phases retain the authored item height");
                        }else require(equal(calls,ordinary),"active ordinary states reuse the unchanged model path");
                    }
                    std::fprintf(stderr,"request draw state %u: inactive/active phases -30/0/30 and stack checked\n",state);
                }
                item(draw(false,0,0,2u,true),{0x57002cu});
                car.put32(0x2f0u,2u);item(draw(true,1,0),{0x57002du});car.put32(0x2f0u,0u);
                const auto heart_fallback=draw(true,0,0,2u,true);
                require(heart_fallback.size()==1u&&heart_fallback[0].pc==0x4044c0u,
                        "active Heart ordinary-state fallback preserves the stock cleanup leaf");
            }
            {   // 481A40 executes its real translated children on the canonical request owner.
                auto& traffic=native_race_traffic(request);auto& block=traffic.requests_7f9460;
                const auto saved=block;const auto active=request.start_mode.manager_state_7f94c0;
                struct Restore{std::vector<std::uint8_t>& block;const std::vector<std::uint8_t>& saved;
                    NativeRuntimeContext& c;std::uint32_t active;
                    ~Restore(){std::memcpy(block.data(),saved.data(),saved.size());c.start_mode.manager_state_7f94c0=active;}
                } restore{block,saved,request,active};
                std::fill(block.begin(),block.end(),0);request.start_mode.manager_state_7f94c0=1;
                put32(block,0x16cu,0);put32(block,0x3a24u,123); // one stock row; inactive child resources
                struct Overlay{std::uint32_t mode{16};std::array<std::uint8_t,0x80> scratch{};std::size_t mark{};} overlay;
                std::vector<RaceHudDraw> draws;NativeHeartBinding binding;
                binding.user=&overlay;binding.display=true;binding.draws=&draws;
                binding.map=[](void* user,PcRaceMemory& m){auto& o=*static_cast<Overlay*>(user);o.mark=m.mark();
                    m.map(0x78026cu,reinterpret_cast<std::uint8_t*>(&o.mode),4);
                    m.map(0x0dfe2000u,o.scratch.data(),o.scratch.size());};
                std::uint32_t missing{},fault{};std::string error;
                const auto depth=request_matrices.depth;const auto offset=request_matrices.current_offset;
                const auto latched=traffic.latched;
                auto display=[&](){draws.clear();const bool ok=native_race_requests_display_481a40(
                        request,request_matrices,binding,missing,fault,error);
                    for(const auto field:{0x74u,0x78u,0x16cu,0x5800u,0x764cu})
                        require(traffic.memory.at(NativeRaceTraffic::RequestsBase+field,4)==block.data()+field,
                                "request HUD reads the canonical request owner, including the old NAVI overlap");
                    require(request_matrices.depth==depth&&request_matrices.current_offset==offset&&traffic.latched==latched,
                            "request display preserves matrix state and the independent traffic latch");
                    traffic.memory.release(overlay.mark);return ok;};
                for(const int value:{74,-74,3700}){
                    put32(block,0x5800u,std::uint32_t(value));put32(block,0x74u,value==74?0u:value<0?1u:2u);
                    const bool ok=display();if(!ok)std::fprintf(stderr,"request HUD child failure %08x/%08x: %s\n",missing,fault,error.c_str());
                    require(ok&&missing==0&&fault==0&&draws.size()==3,"request HUD executes real guarded children and captures three ordered calls");
                    require(draws[0].pc==0x4bb9e0u&&draws[0].args[0]==0&&draws[0].args[1]==123&&
                            draws[1].pc==0x429530u&&draws[1].args[0]==(value==74?0x460064u:value<0?0x2c006du:0x460063u)&&
                            std::int32_t(draws[1].args[4])==(value==3700?99:value/37)&&draws[2].pc==0x4bc990u&&
                            draws[2].args[0]==10&&draws[2].args[1]==0x206u&&draws[2].args[2]==0x1b2u,
                            "stock row, signed gauge/clamp and digits keep exact arguments and order");
                    char text[16]{};std::snprintf(text,sizeof text,"%4d",value);
                    require(!std::memcmp(&draws[2].args[3],text,sizeof text),"request total uses the pinned retail percent-4-d format");
                }
                overlay.mode=20;require(display()&&draws.empty(),"mode 20 skips main rows and gauge but executes the stock resource tail");
                overlay.mode=16;
                auto scratch_put=[&](std::size_t at,std::uint32_t v){std::memcpy(overlay.scratch.data()+at,&v,4);};
                scratch_put(0x0cu,0x0dfe2020u);scratch_put(0x20u,0x0dfe2040u);
                scratch_put(0x40u,0x0dfe2060u);scratch_put(0x74u,0x41bd40u);
                // 41BD40 (jmp 49A650, a bare RET): the stock virtual display of hud-run-2145's request objects. The same
                // request state with and without that object in the list ends in the same block and the same draws.
                {   const auto start=block;
                    put32(block,0x78u,0u);const bool plain=display();const auto plain_block=block;const auto plain_draws=draws;
                    std::memcpy(block.data(),start.data(),start.size());put32(block,0x78u,0x0dfe2000u);
                    const bool listed=display();auto listed_block=block;put32(listed_block,0x78u,0u);
                    bool same=plain_draws.size()==draws.size();
                    for(std::size_t i=0;same&&i<draws.size();++i)same=draws[i].pc==plain_draws[i].pc&&draws[i].args==plain_draws[i].args;
                    require(plain&&listed&&missing==0&&fault==0&&same&&draws.size()==3&&listed_block==plain_block,
                            "a request object's 41BD40 virtual display leaves the request block and the draws unchanged");}
                // 50ECC0: the virtual display of route-run-2212's request objects (vtable 5E5D64). Its children 50E460 /
                // 50E4E0 / 50E9A0 loop over the object's +70 / +72 entries; with none, each kind runs its stock child and
                // the block and draws stay as without the object; a kind above 6 or a key mismatch runs no child.
                {   constexpr std::uint32_t B=NativeRaceTraffic::RequestsBase,Req=0x6e40u,Edi=0x6e80u,Obj=0x6ec0u;
                    const auto start=block;
                    put32(block,0x80u,B+Req);put32(block,0x7660u,B+Edi);put32(block,Req+0x58u,B+Obj);
                    scratch_put(0x44u,B);scratch_put(0x48u,0x1234u);scratch_put(0x74u,0x50ecc0u);
                    const auto set=block;
                    auto run=[&](std::uint32_t kind,std::uint32_t key,bool listed){
                        block=set;put32(block,Obj,key);put32(block,Obj+4u,kind);put32(block,0x78u,listed?0x0dfe2000u:0u);
                        const bool ok=display();put32(block,0x78u,0u);return ok;};
                    struct Case{std::uint32_t kind,key,child;};
                    for(const Case& entry:{Case{0u,0x1234u,0x50e460u},Case{4u,0x1234u,0x50e4e0u},Case{6u,0x1234u,0x50e9a0u},
                                       Case{7u,0x1234u,0u},Case{0u,0x4321u,0u}}){
                        const bool plain=run(entry.kind,entry.key,false);const auto plain_block=block;const auto plain_draws=draws;
                        const auto shown=traffic.routed[0x50ecc0u];
                        const std::array<std::uint32_t,3> pcs{0x50e460u,0x50e4e0u,0x50e9a0u};
                        std::array<std::uint32_t,3> before{};for(std::size_t i=0;i<3;++i)before[i]=traffic.routed[pcs[i]];
                        const bool listed=run(entry.kind,entry.key,true);
                        bool same=plain&&listed&&missing==0&&fault==0&&block==plain_block&&draws.size()==3&&draws.size()==plain_draws.size();
                        for(std::size_t i=0;same&&i<draws.size();++i)same=draws[i].pc==plain_draws[i].pc&&draws[i].args==plain_draws[i].args;
                        bool children=traffic.routed[0x50ecc0u]==shown+1u;
                        for(std::size_t i=0;i<3;++i)children=children&&traffic.routed[pcs[i]]==before[i]+(pcs[i]==entry.child?1u:0u);
                        require(same&&children,"50ECC0 runs the stock child of the object's kind (none above 6 or on a key mismatch) "
                                               "and leaves the request block and the draws as without the object");
                    }
                    block=start;scratch_put(0x44u,0u);scratch_put(0x48u,0u);
                }
                scratch_put(0x74u,0xdead0042u);
                require(!display()&&missing==0xdead0042u&&fault==0&&!error.empty()&&draws.empty(),
                        "unsupported virtual request display retains its exact child PC without guessed draws");
                scratch_put(0x74u,0x0dfe3000u); // an unknown PC is distinct from an unmapped object/vtable read
                scratch_put(0x40u,0x0dfe3000u);
                require(!display()&&missing==0x481a40u&&fault==0x0dfe3014u&&draws.empty(),
                        "unmapped request vtable input retains its exact fault address");
                std::fprintf(stderr,"request HUD bridge: canonical owner, signed gauge, ordered capture, mode20 and unsupported child checked\n");
            }
            {   // 4EFD20(source, lane): hud-run-2145 latched the traffic module there. The request manager's 521930 runs it
                // (this +4 = manager, manager +80 = request with +8 == 1, request +58 = an object keyed like this +8, manager
                // +7660 non-zero): 4EFDB0(0) saves START's table 84BD08 to 84C8E8, the first row's word becomes [+7660]+64,
                // then 4EFD20(object +58, 0) copies the rows to 84BD08 up to the FFFF mark. Scratch structures inside the
                // request block (owner of 7F9460..800AF7) and START's table are restored afterwards.
                auto& traffic=native_race_traffic(request);auto& block=traffic.requests_7f9460;
                const auto saved=block;const auto table=request.start_mode.scene_owner_table68;
                struct Restore{std::vector<std::uint8_t>& block;const std::vector<std::uint8_t>& saved;
                    ~Restore(){std::memcpy(block.data(),saved.data(),saved.size());}} restore{block,saved};
                constexpr std::uint32_t B=NativeRaceTraffic::RequestsBase,This=0x6e00u,Req=0x6e40u,Edi=0x6e80u,Obj=0x6ec0u,Rows=0x7000u,Src=0x7040u;
                put32(block,This+4u,B);put32(block,This+8u,0x1234u);
                put32(block,0x80u,B+Req);put32(block,0x7660u,B+Edi);
                put32(block,Req+8u,1u);put32(block,Req+0x58u,B+Obj);
                put32(block,Obj,0x1234u);put32(block,Obj+0x58u,B+Rows);
                const std::uint16_t word=0x0042u,end=0xffffu;std::memcpy(block.data()+Edi+0x64u,&word,2);
                for(std::uint32_t k=0;k<24u;k+=4)put32(block,Rows+k,0x11110000u+k);           // rows 0 and 1
                std::memcpy(block.data()+Rows+24u,&end,2);                                     // row 2: the end mark
                const auto before=traffic.routed[0x4efd20u];
                std::uint32_t eax=0u;
                const bool ran=native_race_requests_call(request,request_matrices,0x00521930u,{},eax,B+This);
                std::array<std::uint8_t,26> expect{};std::memcpy(expect.data(),block.data()+Rows,24u);
                std::memcpy(expect.data(),&word,2);std::memcpy(expect.data()+24,&end,2);
                require(ran&&!traffic.latched&&traffic.routed[0x4efd20u]==before+1u,"the request manager's 521930 runs 4EFD20 as a traffic service");
                require(!std::memcmp(request.start_mode.scene_owner_table68.data(),expect.data(),expect.size()),
                        "4EFD20 lane 0 copies the request rows to START's table 84BD08 and ends them with FFFF");
                // The targets are their owners' storage, and the shared copy returns the stock EAX: lane 0 the source
                // advanced past the rows copied, lane != 0 the source + C (the light words, restored afterwards).
                auto& area=native_race_area(request);
                const auto lights=area.light_words_6a5dc4;const auto lights_written=area.light_words_written;
                require(traffic.memory.at(0x84bd08u,4u)==request.start_mode.scene_owner_table68.data()&&
                        traffic.memory.at(0x6a5dc4u,4u)==reinterpret_cast<std::uint8_t*>(area.light_words_6a5dc4.data()),
                        "the traffic memory maps 84BD08 to START's table68 and 6A5DC4 to the AREA's light words");
                require(race_objects_copy_4efd20(traffic.memory,B+Rows,0u)==B+Rows+24u,"4EFD20 lane 0 returns the source past two rows");
                for(std::uint32_t k=0;k<24u;k+=4)put32(block,Src+k,0x22220000u+k);
                require(race_objects_copy_4efd20(traffic.memory,B+Src,1u)==B+Src+0xcu&&
                        !std::memcmp(area.light_words_6a5dc4.data(),block.data()+Src,24u),
                        "4EFD20 lane != 0 copies two vectors to the light words and returns the source + C");
                area.light_words_6a5dc4=lights;area.light_words_written=lights_written;
                request.start_mode.scene_owner_table68=table;
                // 4EF870(v): route-run-2212 latched the traffic module there. The same request object's control 50E9F0
                // (vtable 5E5D64 +C) at request state 7 runs 521930 (nothing outside states 1 / 5), clears the manager's
                // +19C and runs 4EF870(0): the appear override 84CC18 in the traffic state's appear block.
                {   auto& appear=traffic.state.appear;constexpr std::uint32_t Override=0x84cc18u-PcTrafficState::AppearBase;
                    const auto kept=appear;
                    auto get32=[](const std::vector<std::uint8_t>& b,std::size_t o){std::uint32_t v;std::memcpy(&v,b.data()+o,4);return v;};
                    put32(block,Req+8u,7u);put32(block,0x19cu,0x55u);put32(appear,Override,0x7f23e0u);
                    const auto routed=traffic.routed[0x4ef870u];
                    std::uint32_t control_eax=0u;
                    const bool control=native_race_requests_call(request,request_matrices,0x0050e9f0u,{},control_eax,B+This);
                    require(control&&!traffic.latched&&traffic.routed[0x4ef870u]==routed+1u&&
                            get32(appear,Override)==0u&&get32(block,0x19cu)==0u,
                            "request state 7: 50E9F0 clears the manager's +19C and runs 4EF870(0) on the appear override 84CC18");
                    require(traffic.memory.at(0x84cc18u,4u)==appear.data()+Override,"84CC18 is the traffic state's appear block");
                    appear=kept;
                }
                std::fprintf(stderr,"request 4EFD20 / 4EF870: 521930 rows to 84BD08, 50E9F0 state 7 override clear checked\n");
            }
            {   // 5209A0(out, normal) with ECX = a course place {t, cs, .., part}: the course point under the request
                // objects' display (50DBE0 -> 520BD0 -> 5209A0). Its first course read is the polygon root [780110 + part * 4];
                // on a loaded lane it runs 43D920 (hint -1 for t up to 0.5) on the selected course world and blends the run's
                // polygon and normal records (40F180, then 40EF00). At t 0 the weight is 0 whatever the 44F0F0 gate gives:
                // the point is the first polygon's +24 vector and the normal that of its normal record, normalized. Lane 1
                // is not loaded: 5209A0 returns the zero point and the up normal without 43D920.
                auto& traffic=native_race_traffic(request);auto& block=traffic.requests_7f9460;
                const auto saved=block;
                struct Restore{std::vector<std::uint8_t>& block;const std::vector<std::uint8_t>& saved;
                    ~Restore(){std::memcpy(block.data(),saved.data(),saved.size());}} restore{block,saved};
                constexpr std::uint32_t B=NativeRaceTraffic::RequestsBase,Place=0x6e00u,Out=0x6e40u,Normal=0x6e80u;
                constexpr std::int16_t Cs=10;
                auto& world=request.start_mode.scene_owner_course_world;
                require(world.query_ready()&&world.lane_loaded(0u)&&!world.lane_loaded(1u),"lane 0 holds the owned course, lane 1 is empty");
                auto vec=[&](std::uint32_t o){std::array<float,3> v{};std::memcpy(v.data(),block.data()+o,12);return v;};
                auto point=[&](std::uint32_t part){
                    std::fill(block.begin()+Place,block.begin()+Normal+12u,std::uint8_t(0));
                    const float t=0.0f;std::memcpy(block.data()+Place,&t,4);std::memcpy(block.data()+Place+4u,&Cs,2);
                    put32(block,Place+0xcu,part);
                    std::uint32_t eax=0u;
                    const bool ran=native_race_requests_call(request,request_matrices,0x005209a0u,{B+Out,B+Normal},eax,B+Place);
                    if(!ran)std::fprintf(stderr,"5209A0 part %u: latched=%d fault=%X %s\n",part,int(traffic.latched),
                                         unsigned(traffic.fault),traffic.error.c_str());
                    return ran;};
                const auto lookups=traffic.routed[0x43d920u];
                const bool loaded=point(0u);
                const auto tables=world.tables();
                std::array<std::uint8_t,10> place{};std::memcpy(place.data()+8,&Cs,2);   // part 0, side 0, cs
                std::int32_t first=-1,last=-1;
                const auto run=outrun::driving::find_selected_course_run(tables,outrun::driving::Bytes(place.data(),place.size()),-1,first,last);
                require(loaded&&!traffic.latched&&traffic.routed[0x43d920u]==lookups+1u&&run>0&&first>=0,
                        "lane 0: 5209A0 runs 43D920 on the owned course through the mapped polygon root");
                const auto& course=tables.courses[0];
                const auto at=std::uint32_t(first);
                const auto p=vec(Out),n=vec(Normal);
                require(p[0]==course.polygons.f32(at*64u+0x24u)&&p[1]==course.polygons.f32(at*64u+0x28u)&&
                        p[2]==course.polygons.f32(at*64u+0x2cu),
                        "lane 0 at t 0: the point is the first polygon's +24 vector (43D920's first, 780110's records)");
                const float rx=course.normals.f32(at*48u+0x24u),ry=course.normals.f32(at*48u+0x28u),rz=course.normals.f32(at*48u+0x2cu);
                const float len=std::sqrt(rx*rx+ry*ry+rz*rz);
                require(len>0.f&&std::fabs(n[0]-rx/len)<1e-5f&&std::fabs(n[1]-ry/len)<1e-5f&&std::fabs(n[2]-rz/len)<1e-5f,
                        "lane 0 at t 0: the normal is the first normal record's +24 vector, normalized (40F180, 40EF00)");
                const bool empty=point(1u);
                require(empty&&!traffic.latched&&traffic.routed[0x43d920u]==lookups+1u&&
                        vec(Out)==std::array<float,3>{0.f,0.f,0.f}&&vec(Normal)==std::array<float,3>{0.f,1.f,0.f},
                        "lane 1 (not loaded): its polygon root is 0, so 5209A0 returns the zero point and the up normal without 43D920");
                std::fprintf(stderr,"request course point 5209A0: lane 0 run %d first %d, lane 1 empty checked\n",int(run),int(first));
            }
            {   // 4FC0B0: the C2C request manager's car records (+7630, 20 bytes each from index +7638) call 4FBBB0(record,
                // previous) for the first record whose cs ([+7634] + record +8) is 30..100 ahead of the player's ([+7660] +64).
                // route-run-2303 latched the traffic module at 4FBBB0. Without the AREA's streamed road tables only a failed
                // pose is reachable here: on course type 0 a lane of 6 is refused before any table read (470120). Covered:
                // no traffic event (count 0) and a live one (flags & 3): 4FBBB0 returns 0, the record stays open and 4FC0B0
                // stops; a free event and a refused pose: [record] = 1, +7638 counted in 4FBBB0 and again by 4FC0B0, no event
                // opened. The placed car (model, colour, work fields) has no offline witness.
                auto& traffic=native_race_traffic(request);auto& block=traffic.requests_7f9460;
                auto& racers=request.mission.racers;constexpr std::uint32_t Event=0x108u;
                struct Restore{std::vector<std::uint8_t>& block;const std::vector<std::uint8_t> saved;
                    outrun::platform::RacerSetupState& racers;const std::uint32_t base,count;
                    outrun::driving::PcEventSlot& slot;const outrun::driving::PcEventSlot kept;
                    ~Restore(){std::memcpy(block.data(),saved.data(),saved.size());racers.v680ad4=base;racers.v680ad8=count;slot=kept;}
                } restore{block,block,racers,racers.v680ad4,racers.v680ad8,request.event_state.slots[Event],request.event_state.slots[Event]};
                constexpr std::uint32_t B=NativeRaceTraffic::RequestsBase,Table=0x6e00u,Car=0x6f00u;
                auto get32=[&](std::size_t o){std::uint32_t v;std::memcpy(&v,block.data()+o,4);return v;};
                auto put16=[&](std::size_t o,std::uint16_t v){std::memcpy(block.data()+o,&v,2);};
                auto records=[&](std::uint32_t count,std::uint8_t flags){
                    std::fill(block.begin()+Table,block.begin()+Car+0x260u,std::uint8_t(0));
                    put32(block,0x7630u,B+Table);put32(block,0x7634u,0);put32(block,0x7638u,0);put32(block,0x763cu,0);
                    put32(block,0x7654u,0);put32(block,0x7658u,0);put32(block,0x7660u,B+Car);
                    put16(Car+0x64u,100);                                   // the player's place: course type 0, cs 100
                    put16(Table+8u,132);block[Table+0xau]=5;               // record 0: cs 132 (30..100 ahead), lane 5 + 1
                    put16(Table+0x14u+8u,0xffffu);                          // record 1: a negative cs ends 4FC0B0
                    racers.v680ad4=Event-8u;racers.v680ad8=count;request.event_state.slots[Event].flags=flags;};
                auto manager=[&](){std::uint32_t eax=0u;
                    const bool ran=native_race_requests_call(request,request_matrices,0x004fc0b0u,{},eax,B);
                    if(!ran)std::fprintf(stderr,"4FC0B0: latched=%d fault=%X %s\n",int(traffic.latched),unsigned(traffic.fault),traffic.error.c_str());
                    return ran;};
                for(const auto& [count,flags]:{std::pair<std::uint32_t,std::uint8_t>{0u,0u},std::pair<std::uint32_t,std::uint8_t>{1u,1u}}){
                    records(count,flags);
                    const auto cars=traffic.routed[0x4fbbb0u],checks=traffic.routed[0x440370u];
                    require(manager()&&!traffic.latched&&traffic.routed[0x4fbbb0u]==cars+1u&&traffic.routed[0x440370u]==checks&&
                            get32(Table)==0u&&get32(0x7638u)==0u&&get32(0x763cu)==0u,
                            "4FC0B0: without a free traffic event 4FBBB0 returns 0, the record stays open and the manager stops");
                }
                records(1u,0u);
                const auto cars=traffic.routed[0x4fbbb0u],checks=traffic.routed[0x440370u],opens=traffic.routed[0x440110u];
                require(manager()&&!traffic.latched&&traffic.routed[0x4fbbb0u]==cars+1u&&traffic.routed[0x440370u]==checks+1u&&
                        traffic.routed[0x440110u]==opens&&request.event_state.slots[Event].flags==0u&&
                        get32(Table)==1u&&get32(0x7638u)==2u&&get32(0x763cu)==0u&&request_matrices.depth==0,
                        "4FC0B0 / 4FBBB0: a refused pose closes the record, counts +7638 in 4FBBB0 and in 4FC0B0, opens no event");
                std::fprintf(stderr,"request car 4FBBB0: no event, live event and refused pose through 4FC0B0 checked\n");
            }
        }
        {   // 4BEC9B / 4BECD2: variant-5 dispatch boundaries, before any sprite renderer.
            NaviPubGlobals globals;PcRaceMemory memory;globals.map(memory);
            std::uint32_t mode=13,variant=5,car=0;
            memory.map(0x78026cu,reinterpret_cast<std::uint8_t*>(&mode),4);
            memory.map(0x780258u,reinterpret_cast<std::uint8_t*>(&variant),4);
            memory.map(0x799d18u,reinterpret_cast<std::uint8_t*>(&car),4);
            memory.put32(0x8447f8u,1);memory.put32(0x844700u,0xffffffffu);
            struct Calls{std::vector<std::uint32_t> pcs;std::uint32_t refuse{},goal{};} calls;
            std::vector<RaceHudDraw> draws;NaviPubServices services;
            services.m=&memory;services.draws=&draws;services.request_user=&calls;services.heart_user=&calls;
            services.request=[](void* p,std::uint32_t pc,std::uint32_t& value){
                auto& c=*static_cast<Calls*>(p);c.pcs.push_back(pc);value=pc==0x46c500u?c.goal:0;return pc!=c.refuse;};
            services.heart=[](void* p,std::uint32_t pc){static_cast<Calls*>(p)->pcs.push_back(pc);return true;};
            auto reset=[&](std::uint32_t next,std::uint32_t refuse=0){mode=next;calls.pcs.clear();calls.refuse=refuse;
                draws.clear();services.missing=0;services.fault=0;calls.goal=0;};
            require(navi_pub_display_4beb00(services)&&draws.empty()&&memory.u32(0x842c10u)==0&&
                    calls.pcs==std::vector<std::uint32_t>{0x46ed20u},"mode 13 skips request HUD and preserves its stock tail flag");
            reset(20);
            require(navi_pub_display_4beb00(services)&&draws.empty()&&memory.u32(0x842c10u)==1&&
                    calls.pcs==std::vector<std::uint32_t>{0x46c220u,0x46ed20u},"mode 20 runs request display before the protected tail");
            reset(17);
            require(navi_pub_display_4beb00(services)&&draws.empty()&&
                    calls.pcs==std::vector<std::uint32_t>{0x46ed20u},"other non-GAME modes keep the protected tail");
            reset(16,0x46c220u);
            require(!navi_pub_display_4beb00(services)&&services.missing==0x46c220u&&draws.size()==1&&
                    draws[0].args==std::array<std::uint32_t,7>{0x2c0030u,0,0,1,0,0,0}&&
                    calls.pcs==std::vector<std::uint32_t>{0x46c220u},"GAME submits the stock panel before a retained request-child failure");
            reset(16,0x46c500u);
            require(!navi_pub_display_4beb00(services)&&services.missing==0x46c500u&&draws.size()==1&&
                    calls.pcs==std::vector<std::uint32_t>{0x46c220u,0x46c500u},"GAME tests the goal gate only after request display");
            reset(16);
            require(!navi_pub_display_4beb00(services)&&services.fault==0x7d3994u&&services.missing==0xfa000000u&&
                    draws.size()==1&&calls.pcs==std::vector<std::uint32_t>{0x46c220u,0x46c500u},
                    "GAME reaches the existing clock next and retains its first unmapped input");
            reset(16);calls.goal=1;
            require(!navi_pub_display_4beb00(services)&&services.fault==0x5cu&&draws.size()==1&&
                    calls.pcs==std::vector<std::uint32_t>{0x46c220u,0x46c500u},
                    "a true goal gate reaches the stock goal-ahead car input before the clock");
            reset(16);memory.put32(0x8447f8u,0);
            require(navi_pub_display_4beb00(services)&&draws.empty()&&calls.pcs.empty(),"HUD toggle suppresses the whole variant-5 display");
            std::fprintf(stderr,"request HUD: modes 13/16/17/20, ordered children and retained faults checked\n");
        }
        auto unready_storage=std::make_unique<NativeRuntimeContext>();
        auto& unready=*unready_storage;
        unready.game_mode.course_assets=&real;
        unready.start_mode.frontend_prepare.event_config_7f94c4[0]=1;
        put32(native_race_traffic(unready).requests_7f9460,0x98u,3u);
        std::uint32_t unready_result=1u;
        require(native_start_owned_call(unready,0x0046c2f0u,nullptr,0u,unready_result)&&unready_result==0u&&
                unready.game_mode.course_read_calls==0u,
                "phase-3 state without an earlier ready course fails closed");
        auto start_storage=std::make_unique<NativeRuntimeContext>();
        auto& start=*start_storage;
        start.mode_descriptors[13u]={0u,0x0049db20u,0x0049dd40u,0x00499d80u};
        LoaderAssetPack start_resources{};
        start_resources.records.push_back({LoaderAssetStartLoadingId,2u,{0x78u,0xdau,1u}});
        start.event_function36.loader_assets=&start_resources;
        require(native_runtime_attach_course_assets(start,real),"START attaches owned course data");
        require(native_runtime_request_mode(start,13u)&&native_runtime_mode_control(start,{}),"START enters original course-loading mode");
        require(start.mode_state.current==13u&&start.start_mode.stage==2u&&start.start_mode.course_load_attempts==1u&&start.start_mode.course_load_success==1u&&start.game_mode.course_load_success==1u,"START stage 0 loads real course before requesting 2F/2");
        require(start.start_mode.resource_id==LoaderAssetStartLoadingId&&start.start_mode.resource_ready==1u&&start.start_mode.event_open_count==1u,"START stage 2 retains PC readiness gate and does not open event 185 early");
        // 49BA80 world ids come from 44DBB0 on the live 7D30BC descriptor
        // (scene_owner_49ba80 + native_scene_owner_service).
    }
    std::printf("course_asset_pack: %u checks passed\n",checks);return 0;
}
