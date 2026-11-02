// Real adapter boundaries for the service audit's optional callbacks. No
// sockets, device, renderer or physical force output is created here.
#include "platform/native_runtime.hpp"
#include "platform/race_traffic_runtime.hpp"
#include "platform/pc_network_sockets.hpp"
#include "platform/car_producer_observer.hpp"
#include "system/exe_image.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>
using namespace outrun;using namespace platform;
namespace {unsigned checks;}
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("optional callback line "+std::to_string(__LINE__)+": " #x);}while(0)
int main(){try{
    NativeCarProducerObservation observation{};NativeCarProducerObserver observer{};
    notify_car_producer(observer,observation);CHECK(!observer.observe);
    unsigned notified=0;observer.user=&notified;observer.observe=[](void* user,const auto&){++*static_cast<unsigned*>(user);};
    notify_car_producer(observer,observation);CHECK(notified==1&&observer.observe);
    observer.observe=[](void*,const auto&){throw std::runtime_error("optional diagnostic failed");};
    notify_car_producer(observer,observation);CHECK(!observer.observe&&!observer.user);
    notify_car_producer(observer,observation);CHECK(notified==1);
    for(unsigned error_case=0;error_case<3;++error_case){
        PcRaceMemory memory;std::array<std::uint8_t,16> output{};memory.map(0x20000000,output.data(),output.size());
        PcNetworkSockets sockets;PcNetworkPlatform supplier;unsigned resolves=0;
        supplier.resolve=[&](const std::string&){++resolves;return std::vector<std::uint32_t>{};};
        if(error_case)supplier.last_error=[&](){return error_case==1?0u:11002u;};
        sockets.set_platform(std::move(supplier));
        CHECK(!sockets.resolve_server(memory,"missing.example",0x20000000,1)&&resolves==1);
        NativeNetworkStats stats;unsigned pop=99;
        CHECK(sockets.call(memory,WsaGetLastError,[](unsigned){return 0u;},pop,stats)==(error_case==2?11002u:11001u)&&pop==0);
        CHECK(!sockets.resolve_server(memory,"missing.example",0x20000000,0)&&resolves==1);
        CHECK((output==std::array<std::uint8_t,16>{}));
    }
    if(!exe_image_autoload()){std::puts("SKIP: request display requires verified OR2_EXE constants");return 77;}
    auto owner=std::make_unique<NativeRuntimeContext>();auto& c=*owner;c.mode_state.current=16;
    auto& traffic=native_race_traffic(c);auto& block=traffic.requests_7f9460;
    std::fill(block.begin(),block.end(),0);driving::Bytes request(block.data(),block.size());request.put32(0x16c,~0u);
    std::array<std::uint8_t,128> arena{};driving::PcMatrixStack matrices{driving::Bytes(arena.data(),arena.size()),0,0,2};driving::pc_matrix_identity(matrices);
    std::vector<RaceHudDraw> draws;NativeHeartBinding binding;binding.display=true;binding.draws=&draws;
    unsigned missing=99,fault=99;std::string error="old failure";
    const auto latched=traffic.latched;const auto before=request.u32(0x60);
    CHECK(!native_race_requests_display_481a40(c,matrices,binding,missing,fault,error));
    CHECK(missing==0x481a40&&fault==0x78026c&&!error.empty()&&draws.empty());
    CHECK(!binding.map&&traffic.latched==latched&&matrices.depth==0&&matrices.current_offset==0);
    unsigned mode=20;binding.user=&mode;binding.map=[](void* user,PcRaceMemory& m){m.map(0x78026c,static_cast<std::uint8_t*>(user),4);};
    const bool ok=native_race_requests_display_481a40(c,matrices,binding,missing,fault,error);
    if(!ok)std::fprintf(stderr,"mode20 display: %x/%x %s\n",missing,fault,error.c_str());
    CHECK(ok&&missing==0&&fault==0&&error.empty());
    CHECK(request.u32(0x60)==before&&traffic.latched==latched);
    CHECK(draws.empty()); // authored mode20 skips the ordinary request HUD
    mode=16;constexpr unsigned Bad=0x0dfe3000;request.put32(0x78,Bad);draws.clear();
    CHECK(!native_race_requests_display_481a40(c,matrices,binding,missing,fault,error));
    CHECK(missing==0x481a40&&fault==Bad+12&&!error.empty()&&draws.empty());
    CHECK(traffic.latched==latched&&matrices.depth==0&&matrices.current_offset==0);
    request.put32(0x78,0);binding.map=[](void*,PcRaceMemory&){throw std::runtime_error("mapper failure");};
    CHECK(!native_race_requests_display_481a40(c,matrices,binding,missing,fault,error));
    CHECK(missing==0x481a40&&fault==0&&error=="mapper failure"&&traffic.latched==latched);
    CHECK(matrices.depth==0&&matrices.current_offset==0);
    std::printf("optional callback contracts: %u checks passed, no transport/device\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
