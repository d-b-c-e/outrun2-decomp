#include "platform/native_runtime.hpp"
#include "platform/race_traffic_runtime.hpp"
#include "platform/race_hud_runtime.hpp"
#include "platform/race_area_runtime.hpp"
#include "platform/race_player_car.hpp"
#include "driving/pc_wheel_dynamics.hpp"
#include "driving/pc_x87.hpp"
#include "platform/world_source_pack.hpp"
#include "platform/course_collision_pack.hpp"
#include "platform/course_world_runtime.hpp"
#include "platform/embedded_exe_data.hpp"
#include "system/exe_image.hpp"
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
            }
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
