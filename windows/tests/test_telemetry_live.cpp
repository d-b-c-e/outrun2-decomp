// Production telemetry_live.cpp over inert runtime records, an ephemeral
// loopback listener and a unique private mapping. No game/device/owner receiver.
#include <winsock2.h>
#include <windows.h>
#include "telemetry_forza.hpp"
#include "native_window.hpp"
#include "platform/native_runtime.hpp"
#include <array>
#include <vector>
#include <string>
#include <memory>
#include <cstring>
#include <cmath>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
namespace {unsigned checks,inner_calls;}
namespace outrun::xbox_runtime {InputFilter& input_filter(){static InputFilter value=nullptr;return value;}}
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error("telemetry live line "+std::to_string(__LINE__)+": " #x);}while(0)
template<class T> T read(const std::vector<std::uint8_t>& p,std::size_t at){CHECK(at+sizeof(T)<=p.size());T v;std::memcpy(&v,p.data()+at,sizeof v);return v;}
template<class T,class B> void put(B& bytes,std::size_t at,T value){CHECK(at+sizeof(T)<=bytes.size());std::memcpy(bytes.data()+at,&value,sizeof value);}
struct Env {
    const char* name;bool present{};std::string value;
    explicit Env(const char* key):name(key){if(const auto* old=std::getenv(key)){present=true;value=old;}_putenv_s(key,"");}
    ~Env(){_putenv_s(name,present?value.c_str():"");}
};
struct Harness {
    Env enabled{"OR2_TELEMETRY"},port_override{"OR2_TELEMETRY_PORT"};
    SOCKET receiver{INVALID_SOCKET};HANDLE mapping{};const outrun::desktop::OutRun2006TelemetryData* shared{};
    outrun::xbox_runtime::InputFilter saved{};bool wsa{};unsigned short port{};
    std::string name="Local\\OutRunTelemetryFixture-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64());
    Harness(){
        WSADATA data{};CHECK(WSAStartup(MAKEWORD(2,2),&data)==0);wsa=true;
        receiver=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);CHECK(receiver!=INVALID_SOCKET);
        sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        CHECK(bind(receiver,reinterpret_cast<const sockaddr*>(&addr),sizeof addr)==0);int bytes=sizeof addr;
        CHECK(getsockname(receiver,reinterpret_cast<sockaddr*>(&addr),&bytes)==0);port=ntohs(addr.sin_port);CHECK(port!=0);
        CHECK(!OpenFileMappingA(FILE_MAP_READ,FALSE,name.c_str()));
        saved=outrun::xbox_runtime::input_filter();outrun::xbox_runtime::input_filter()=[](outrun::platform::NativeInputState& out,const outrun::platform::NativeRuntimeContext*){++inner_calls;out.brake=17;};
    }
    ~Harness(){
        outrun::desktop::telemetry_shutdown();outrun::xbox_runtime::input_filter()=saved;close_map();
        if(receiver!=INVALID_SOCKET)closesocket(receiver);if(wsa)WSACleanup();
    }
    void close_map(){if(shared){UnmapViewOfFile(shared);shared=nullptr;}if(mapping){CloseHandle(mapping);mapping=nullptr;}}
    outrun::desktop::Ini settings(const char* layout,bool on=true){
        outrun::desktop::Ini ini;ini.load_text(std::string("[Telemetry]\nEnabled=")+(on?"true":"false")+"\nPort="+std::to_string(port)+"\nLayout="+layout+"\nEngineRpm=Game\nSharedMemName="+name+"\n","");return ini;
    }
    void open_map(){mapping=OpenFileMappingA(FILE_MAP_READ,FALSE,name.c_str());CHECK(mapping);shared=static_cast<const outrun::desktop::OutRun2006TelemetryData*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof *shared));CHECK(shared);}
    std::vector<std::uint8_t> receive(long milliseconds=50){
        fd_set fds;FD_ZERO(&fds);FD_SET(receiver,&fds);timeval wait{milliseconds/1000,(milliseconds%1000)*1000};const int ready=select(0,&fds,nullptr,nullptr,&wait);CHECK(ready>=0);
        if(!ready)return {};std::vector<std::uint8_t> packet(400);sockaddr_in from{};int bytes=sizeof from;
        const int n=recvfrom(receiver,reinterpret_cast<char*>(packet.data()),int(packet.size()),0,reinterpret_cast<sockaddr*>(&from),&bytes);
        CHECK(n>0&&ntohl(from.sin_addr.s_addr)==INADDR_LOOPBACK);packet.resize(std::size_t(n));return packet;
    }
};
int main(){try{
    using namespace outrun;using namespace desktop;
    auto rt=std::make_unique<platform::NativeRuntimeContext>();rt->mode_state.current=16;
    auto& cs=rt->event_function36.car_select;
    put(cs.car_799d18,0x1c4,.4f);put(cs.car_799d18,0x1f8,180.f);put(cs.car_799d18,0x208,std::uint32_t(4));
    put(cs.car_799d18,0x1d8,std::uint32_t(3));put(cs.car_799d18,0x20c,std::int32_t(4321));
    put(cs.parameters,0x15f8,1500.f/9.54929638f);put(cs.parameters,0x1644,8500.f/9.54929638f);
    rt->race.car_world.analog_7d6810.current[0]=-64;rt->race.car_world.analog_7d6810.current[1]=200;rt->race.car_world.analog_7d6810.current[2]=30;
    for(const char* layout:{"Horizon","FM7"}){
        Harness h;const auto inner=xbox_runtime::input_filter();platform::NativeInputState out;
        const auto tick=[&]{xbox_runtime::input_filter()(out,rt.get());};
        for(unsigned gate=0;gate<3;++gate){
            auto ini=h.settings(layout,gate!=0);if(gate==2)_putenv_s("OR2_TELEMETRY","0");
            telemetry_configure(ini,gate==1);telemetry_wrap_input_filter();CHECK(xbox_runtime::input_filter()==inner);tick();
            CHECK(h.receive(5).empty()&&!OpenFileMappingA(FILE_MAP_READ,FALSE,h.name.c_str()));telemetry_shutdown();_putenv_s("OR2_TELEMETRY","");
        }
        telemetry_configure(h.settings(layout),false);telemetry_wrap_input_filter();CHECK(xbox_runtime::input_filter()!=inner);h.open_map();CHECK(h.shared->version==1&&h.shared->packetId==0);
        const auto start=GetTickCount();const auto calls=inner_calls;rt->mode_state.current=16;tick();const auto packet=h.receive();const auto end=GetTickCount();
        const bool horizon=std::strcmp(layout,"Horizon")==0;const unsigned speed=horizon?256:244,accel=horizon?315:303,gear=horizon?319:307,steer=horizon?320:308;
        CHECK(packet.size()==(horizon?324u:311u)&&read<std::int32_t>(packet,0)==1&&(!horizon||packet.back()=='R')); // only Horizon has the extra sentinel byte
        CHECK(read<std::uint32_t>(packet,4)-start<=end-start&&std::fabs(read<float>(packet,speed)-50.f)<.0001f);
        CHECK(packet[accel]==200&&packet[accel+1]==30&&packet[gear]==4&&std::int8_t(packet[steer])==-64);
        CHECK(std::fabs(read<float>(packet,8)-8500.f)<.01f&&std::fabs(read<float>(packet,12)-1500.f)<.01f&&read<float>(packet,16)==4321.f);
        CHECK(read<float>(packet,20)==0.f&&inner_calls==calls+1&&out.brake==17); // inner input preserved; telemetry uses consumed channels
        CHECK(h.shared->packetId==1&&h.shared->isInGameplay==1&&h.shared->gameMode==16&&h.shared->gear==4&&h.shared->prevGear==3&&h.shared->speed==.4f);
        rt->mode_state.current=18;tick();const auto pause=h.receive();CHECK(read<std::int32_t>(pause,0)==1&&pause[accel]==0&&pause[accel+1]==0&&std::int8_t(pause[steer])==0);
        rt->mode_state.current=3;tick();const auto parked=h.receive();CHECK(read<std::int32_t>(parked,0)==0&&read<float>(parked,speed)==0.f&&h.shared->isInGameplay==0);
        for(unsigned i=0;i<5;++i)tick();CHECK(h.receive(5).empty());tick();CHECK(read<std::int32_t>(h.receive(),0)==0); // parked 1 in 6 ticks
        rt->mode_state.current=16;put(cs.car_799d18,0x1f8,std::numeric_limits<float>::quiet_NaN());tick();CHECK(h.receive(5).empty());put(cs.car_799d18,0x1f8,180.f);
        tick();CHECK(read<std::int32_t>(h.receive(),0)==1);
        telemetry_shutdown();CHECK(h.shared->isInGameplay==0);
        for(unsigned i=0;i<3;++i){const auto stopped=h.receive();CHECK(read<std::int32_t>(stopped,0)==0&&read<float>(stopped,speed)==0.f);}
        telemetry_shutdown();const auto stopped_calls=inner_calls;tick();CHECK(inner_calls==stopped_calls+1&&h.receive(5).empty());
        h.close_map();CHECK(!OpenFileMappingA(FILE_MAP_READ,FALSE,h.name.c_str())); // producer mapping really closed
    }
    std::printf("telemetry live: %u checks\n",checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
