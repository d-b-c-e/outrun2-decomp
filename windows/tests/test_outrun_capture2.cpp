// Capture contract @2 (source/outrun_capture.hpp; romi 33134/33147/33165): an ordinary recording's rows add the input
// the game consumed after the 453BB0 arbitration (final.*) and the raw keyboard behind it (keyboard.*). An @1 row is
// unchanged, both decode strictly, and the Core frame decoder sees exactly the same frame channels either way.
#include "outrun_capture.hpp"
#include <cstdio>
#include <cstdlib>
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
