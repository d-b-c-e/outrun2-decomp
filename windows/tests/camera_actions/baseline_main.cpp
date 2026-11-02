// Fixture generator uses only the pinned pre-extension headers and actual tick serializer.
#include "frame_channels.hpp"
#include "observer_domain.hpp"
#include "dbce_shim.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
namespace sh=dbce::shim;namespace rec=sh::recorder;namespace ob=sh::observe;
int main(int argc,char** argv){
    static_assert(sh::ActionCount==31&&DBCE_SHIM_ACTIONS==31&&sizeof(dbce_shim_frame)==296);
    if(argc!=2)return 1;const std::filesystem::path dir=argv[1];std::filesystem::create_directories(dir);
    for(unsigned row=0;row<3;++row){
        sh::Frame f;f.seq=0x123456789ull+row;f.tick_us=0x234567890ll+100*row;f.boundary_us=f.tick_us+50;
        f.down=1ull<<17;f.up=1ull<<30;f.run=sh::Run::Running;f.latched=true;f.suppress_native=true;
        for(int k=0;k<sh::ActionCount;++k){f.action[k].value=k==0?-.25f*(row+1):float(k%4)*.25f;f.action[k].status=sh::Status::TestNeutral;}
        const auto base=dir/("frame"+std::to_string(row));
        std::ofstream channels(base.string()+".channels");channels<<std::setprecision(17);for(const auto& entry:rec::encode(f))channels<<entry.first<<'\t'<<entry.second<<'\n';
        dbce_shim_frame output{};output.struct_size=sizeof output;dbce_shim_frame* frame=&output;
#include "baseline_serializer.inc"
        std::ofstream binary(base.string()+".abi2",std::ios::binary);binary.write(reinterpret_cast<const char*>(&output),sizeof output);
        ob::Document d;d.identity.process=7;d.identity.process_start=9;d.identity.nonce[0]=1;d.identity.executable[0]=2;d.identity.build[0]=3;d.identity.profile[0]=4;
        d.frame=f.seq;d.tick_us=f.tick_us;d.boundary_us=f.boundary_us;d.down=f.down;d.up=f.up;d.run=f.run;d.latched=true;d.suppress_native=true;d.actions=f.action;
        ob::Payload payload{};if(!ob::encode(d,payload))return 2;
        std::ofstream observer(base.string()+".observer2",std::ios::binary);observer.write(reinterpret_cast<const char*>(payload.data()),payload[2]*4);
    }
}
