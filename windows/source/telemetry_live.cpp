// The process side of the desktop telemetry (telemetry_forza.hpp): the UDP socket, the shared-memory block and the frame
// after the input filter. The packet and block contents are telemetry_forza.cpp's pure functions.
#include <winsock2.h>   // before anything that could bring in windows.h (and its winsock.h)
#include <windows.h>
#include "telemetry_forza.hpp"
#include "native_window.hpp"
#include "platform/native_runtime.hpp"
#include "platform/runtime_input.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace outrun::desktop {
namespace {
std::uint8_t byte(std::int64_t v){return std::uint8_t(std::clamp<std::int64_t>(v,0,255));}
}
namespace {
struct Live {
    bool on{},wsa{};
    TelemetrySettings s;
    SOCKET sock{INVALID_SOCKET};
    sockaddr_in to{};
    HANDLE map{};
    OutRun2006TelemetryData* shm{};
    xbox_runtime::InputFilter inner{};
    std::uint32_t packets{},ticks{},sent{},failed{},skipped{};
    bool hud_bad_logged{};
};
Live t;
void send(const std::uint8_t* p,int n){
    if(t.sock==INVALID_SOCKET||n<=0)return;
    if(sendto(t.sock,reinterpret_cast<const char*>(p),n,0,reinterpret_cast<const sockaddr*>(&t.to),sizeof t.to)==n)++t.sent;else ++t.failed;
}
void frame(const platform::NativeRuntimeContext* rt){
    const std::uint32_t mode=rt?rt->mode_state.current:0u;
    const bool gameplay=rt&&telemetry_in_gameplay(mode);
    TelemetryCar car;
    if(rt){const auto& b=rt->event_function36.car_select.car_799d18;car=telemetry_car(b.data(),b.size());}
    if(t.shm)telemetry_shared(car,mode,gameplay,++t.packets,*t.shm);
    std::uint8_t buf[dbce::forza::FORZA_HORIZON_324];
    const auto ms=std::uint32_t(GetTickCount());
    if(gameplay){
        // GAME's 7D6810 values are what the game consumed, whichever device drove. START and the pause menu clear
        // car input (operation_input_49fad0); goal/time-up have no qualified consumed input, so report neutral.
        const bool race=mode==16u;
        const auto& a=rt->race.car_world.analog_7d6810.current;
        const std::int32_t steer=race?a[0]:0;
        const std::uint8_t accel=race?byte(a[1]):0,brake=race?byte(a[2]):0;
        const int n=telemetry_packet(car,true,steer,accel,brake,ms,t.s.layout,buf,int(sizeof buf));
        if(n)send(buf,n);
        else{++t.skipped;if(!t.hud_bad_logged){t.hud_bad_logged=true;std::printf("telemetry: invalid HUD speed; packet skipped, not reported as zero\n");std::fflush(stdout);}}
    }else if((t.ticks++%6u)==0u)send(buf,telemetry_parked_packet(ms,t.s.layout,buf,int(sizeof buf)));   // about 10 Hz
}
void filter(platform::NativeInputState& out,const platform::NativeRuntimeContext* rt){
    if(t.inner)t.inner(out,rt);
    if(t.on)frame(rt);
}
}

void telemetry_configure(const Ini& settings,bool latched){
    t=Live{};
    t.s=telemetry_settings(settings,std::getenv("OR2_TELEMETRY"),std::getenv("OR2_TELEMETRY_PORT"));
    if(!t.s.notes.empty())std::printf("telemetry: %s\n",t.s.notes.c_str());
    if(latched){std::printf("telemetry: off (latched test process)\n");std::fflush(stdout);return;}
    if(!t.s.enabled){std::printf("telemetry: off ([Telemetry] Enabled = false or OR2_TELEMETRY=0)\n");std::fflush(stdout);return;}
    WSADATA w;
    if(WSAStartup(MAKEWORD(2,2),&w)==0){
        t.wsa=true;   // cleaned up at shutdown even when the socket fails (romi 27774)
        t.sock=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        t.to.sin_family=AF_INET;t.to.sin_port=htons(t.s.port);t.to.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if(t.sock==INVALID_SOCKET)std::printf("telemetry: no UDP socket (WSA %d)\n",WSAGetLastError());
    }else std::printf("telemetry: WSAStartup failed\n");
    if(!t.s.shared_name.empty()){
        t.map=CreateFileMappingA(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,DWORD(sizeof(OutRun2006TelemetryData)),t.s.shared_name.c_str());
        if(t.map)t.shm=static_cast<OutRun2006TelemetryData*>(MapViewOfFile(t.map,FILE_MAP_ALL_ACCESS,0,0,sizeof(OutRun2006TelemetryData)));
        if(t.shm){std::memset(t.shm,0,sizeof *t.shm);t.shm->version=1;}
        else{
            std::printf("telemetry: shared memory %s not mapped (error %lu)\n",t.s.shared_name.c_str(),GetLastError());
            if(t.map){CloseHandle(t.map);t.map=nullptr;}   // a mapping without a view is closed now (romi 27774)
        }
    }
    t.on=t.sock!=INVALID_SOCKET||t.shm;
    std::printf("telemetry: Forza %s to 127.0.0.1:%u%s%s%s\n",t.s.layout==dbce::forza::FORZA_FM7_DASH_311?"FM7 Dash (311 bytes)":"Horizon (324 bytes)",
        unsigned(t.s.port),t.shm?"; shared memory ":"",t.shm?t.s.shared_name.c_str():"",t.shm?" (76 bytes, version 1)":"");
    std::fflush(stdout);
}

void telemetry_wrap_input_filter(){
    if(!t.on)return;
    auto& f=xbox_runtime::input_filter();
    if(f==filter)return;
    t.inner=f;f=filter;
}

void telemetry_shutdown(){
    if(t.on){
        std::uint8_t buf[dbce::forza::FORZA_HORIZON_324];
        for(int i=0;i<3;++i)send(buf,telemetry_parked_packet(std::uint32_t(GetTickCount()),t.s.layout,buf,int(sizeof buf)));
        std::printf("telemetry: %u packets sent, %u failed, %u race frames skipped (invalid HUD speed)\n",t.sent,t.failed,t.skipped);std::fflush(stdout);
    }
    // Everything opened is closed, whether or not the stream ran.
    if(t.sock!=INVALID_SOCKET){closesocket(t.sock);t.sock=INVALID_SOCKET;}
    if(t.wsa){WSACleanup();t.wsa=false;}
    if(t.shm){t.shm->isInGameplay=0;UnmapViewOfFile(t.shm);t.shm=nullptr;}
    if(t.map){CloseHandle(t.map);t.map=nullptr;}
    t.on=false;
}
}
