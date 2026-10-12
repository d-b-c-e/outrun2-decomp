// Capture contract @2 (source/outrun_capture.hpp; romi 33134/33147/33165): an ordinary recording's rows add the input
// the game consumed after the 453BB0 arbitration (final.*) and the raw keyboard behind it (keyboard.*). An @1 row is
// unchanged, both decode strictly, and the Core frame decoder sees exactly the same frame channels either way.
#include "outrun_capture.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
using namespace outrun::desktop;
namespace {
int checks=0;
void check(bool v,const std::string& what){++checks;if(!v){std::fprintf(stderr,"capture @2: FAIL: %s\n",what.c_str());std::exit(1);}}
bool prefixed(const rec::Channels& c,const char* p){for(const auto& v:c)if(v.first.rfind(p,0)==0)return true;return false;}
Snapshot base(){
    Snapshot s;s.frame.seq=41;s.frame.tick_us=123456;s.frame.action[0].value=0.25f;s.frame.action[0].status=dbce::shim::Status::Live;
    s.signals.frame_seq=41;s.signals.mode=16;s.signals.in_gameplay=true;s.signals.record_buttons=0x2;
    s.view.mode=16;s.view.car=true;s.view.gear=3;s.view.speed=88.5f;
    return s;
}
}
int main(){
    const auto one=base();
    const auto c1=channels(one);
    check(!prefixed(c1,"final.")&&!prefixed(c1,"keyboard."),"an @1 row has no final.* or keyboard.* channel");
    check(std::string(CaptureContract)=="dbce.shim.outrun-capture@1\n"&&std::string(CaptureContract2)=="dbce.shim.outrun-capture@2\n","the two contracts");

    auto two=base();two.has_consumed=true;
    auto& f=two.consumed;
    f.steering=-77;f.accelerator=200;f.brake=13;f.shift_up=true;f.menu_left=true;f.menu_confirm=true;f.start=true;
    f.frontend.feature_mask=0x2004u;f.frontend.device_held=0x80u;f.frontend.axes={-5,6,-7,8};f.connected=true;
    f.pc_input_layer=true;f.pc_record_used=true;f.pc_overlay=true;f.pc_overlay_yield=true;
    f.pc_device.buttons_04=0x182u;f.pc_device.axes_94[0x0e]=-12000;f.pc_device.axes_94[0x0d]=255;f.pc_device.axes_94[0x0c]=17;
    two.keyboard={true,false,true,false,false,true,true,true};
    two.pose.valid=true;two.pose.position={1.5f,-2.0f,300.25f};two.pose.vec20={0.125f,0.0f,-1.0f};
    two.pose.rotation={1,0,0, 0,0.5f,-0.75f, 0,0.75f,0.5f};two.pose.model=4;two.pose.stage_key=17;two.pose.course_gate=9;
    const auto c2=channels(two);
    check(prefixed(c2,"final.")&&prefixed(c2,"keyboard."),"an @2 row has both");
    // The pose observer (sana 35853): every channel on an @2 row, none on @1, raw values (no default fill).
    check(!c1.count("gameInput.poseValid")&&!c1.count("gameInput.posX")&&!c1.count("gameInput.rot22"),"an @1 row has no pose channel");
    check(c2.at("gameInput.poseValid")==1&&c2.at("gameInput.posX")==1.5&&c2.at("gameInput.posY")==-2.0&&c2.at("gameInput.posZ")==300.25,"the position");
    check(c2.at("gameInput.vec20X")==0.125&&c2.at("gameInput.vec20Y")==0.0&&c2.at("gameInput.vec20Z")==-1.0,"the raw +20 vector");
    check(c2.at("gameInput.rot00")==1&&c2.at("gameInput.rot11")==0.5&&c2.at("gameInput.rot12")==-0.75&&c2.at("gameInput.rot21")==0.75&&
          c2.at("gameInput.rot22")==0.5&&c2.at("gameInput.rot10")==0,"the full 3x3, row-major");
    check(c2.at("gameInput.carModel")==4&&c2.at("gameInput.stageKey")==17&&c2.at("gameInput.courseGate")==9,"the identity");
    {auto off=two;off.pose={};const auto c=channels(off);
     check(c.at("gameInput.poseValid")==0&&c.count("gameInput.posX")&&c.count("gameInput.stageKey"),"outside a race: present, not valid");}
    // car_pose (sana 35974): read only for the active player car; a stale record with values is never valid.
    {std::uint8_t car[CarPoseBytes]{};
     const auto put=[&](std::size_t at,float v){std::memcpy(car+at,&v,4);};
     const auto put32=[&](std::size_t at,std::uint32_t v){std::memcpy(car+at,&v,4);};
     put(0x14,12.5f);put(0x18,-3.0f);put(0x1c,-640.25f);put(0x20,0.5f);put(0x24,0.0f);put(0x28,-0.875f);
     for(std::size_t r=0;r<3;++r)for(std::size_t k=0;k<4;++k)put(0x70+(r*4+k)*4,float(10*r+k)+0.5f);   // the 4th column is not read
     car[0x11]=7;put32(0x5c,0x13u);put32(0x68,0x2a0u);
     const auto p=car_pose(16,0x7804b0u,0x02,car);
     check(p.valid&&p.position[0]==12.5f&&p.position[1]==-3.0f&&p.position[2]==-640.25f&&p.vec20[0]==0.5f&&p.vec20[2]==-0.875f,"the active car: position and the raw +20 vector");
     check(p.rotation[0]==0.5f&&p.rotation[2]==2.5f&&p.rotation[3]==10.5f&&p.rotation[5]==12.5f&&p.rotation[8]==22.5f,"the 3x3 from the +70 4x4, row-major");
     check(p.model==7&&p.course_gate==0x13u&&p.stage_key==0x2a0u,"the identity");
     check(car_pose(18,0x7804b0u,0x0a,car).valid,"mode 18, and a paused (flags 0x08) active car, is valid");
     check(!car_pose(13,0x7804b0u,0x02,car).valid&&!car_pose(32,0x7804b0u,0x02,car).valid,"loading and the menus: not valid");
     check(!car_pose(16,0x7804b0u,0x01,car).valid,"event 8 opened, its init not run yet: not valid");
     check(!car_pose(16,0x7804b0u,0x00,car).valid&&!car_pose(16,0x7804b0u,0x04,car).valid,"event 8 closed or closing: not valid");
     check(!car_pose(16,0x7804b0u,0x03,car).valid,"open and init pending together: not valid");
     check(!car_pose(16,0x7d0000u,0x02,car).valid&&!car_pose(16,0,0x02,car).valid,"event 8 holding another work, or none: not valid");
     const auto z=car_pose(16,0,0x02,car);
     check(z.position[0]==0&&z.stage_key==0&&z.rotation[8]==0,"an invalid pose carries zeros, not the stale record");}
    check(c2.size()==c1.size()+consumed_channels(two).size(),"the @2 channels are only added");
    for(const auto& v:c1)check(c2.count(v.first)&&c2.at(v.first)==v.second,"every @1 channel unchanged in @2: "+v.first);
    check(frame_channels(c1)==frame_channels(c2),"the Core frame decoder sees the same channels (final.*/keyboard.* excluded)");
    const auto d=decoded_consumed(c2);
    const auto& g=d.input;
    check(g.steering==-77&&g.accelerator==200&&g.brake==13&&g.shift_up&&!g.shift_down&&g.menu_left&&!g.menu_right&&g.menu_confirm&&!g.menu_cancel&&g.start,
          "the consumed driving and menu input round-trips");
    check(g.frontend.feature_mask==0x2004u&&g.frontend.device_held==0x80u&&g.frontend.axes[0]==-5&&g.frontend.axes[3]==8&&g.connected,"the frontend input round-trips");
    check(g.pc_input_layer&&g.pc_record_used&&g.pc_overlay&&g.pc_overlay_yield&&g.pc_device.buttons_04==0x182u&&
          g.pc_device.axes_94[0x0e]==-12000&&g.pc_device.axes_94[0x0d]==255&&g.pc_device.axes_94[0x0c]==17,"the PC layer's record round-trips");
    check(d.keyboard.enter&&!d.keyboard.esc&&d.keyboard.up&&d.keyboard.right&&d.keyboard.any&&d.keyboard.drives,"the keyboard provenance round-trips");
    // Strict decoding: an out-of-range or missing value throws.
    auto bad=c2;bad["final.steering"]=200;
    bool threw=false;try{(void)decoded_consumed(bad);}catch(const std::exception&){threw=true;}
    check(threw,"an out-of-range steering value is refused");
    bad=c2;bad.erase("keyboard.drives");threw=false;
    try{(void)decoded_consumed(bad);}catch(const std::exception&){threw=true;}
    check(threw,"a missing channel is refused");
    std::printf("capture @2: %d checks passed\n",checks);
    return 0;
}
