// Production camera model and native projection; no window, game, devices or force.
#include "camera_adjustment.hpp"
#include "desktop_keyboard.hpp"
#include "driving/pc_race_camera.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <limits>
#include <cmath>
#include <stdexcept>
namespace c=outrun::desktop::camera;
namespace d=outrun::driving;
namespace fs=std::filesystem;
using outrun::desktop::Ini;
unsigned checks{};
void check(bool v,const char* why){++checks;if(!v)throw std::runtime_error(why);}
bool close_to(float a,float b){return std::abs(a-b)<0.00001f;}
std::string read(const fs::path& p){std::ifstream f(p,std::ios::binary);std::stringstream s;s<<f.rdbuf();return s.str();}
void write(const fs::path& p,const std::string& s){std::ofstream f(p,std::ios::binary);f<<s;check(bool(f),"write fixture");}
bool refuse_publish(const std::string&,const std::string&){return false;}
struct Driver {
    c::Model m;c::KeyStates keys{};double time{};unsigned clears{};int view{};
    explicit Driver(const Ini& ini){m.load(ini,true);tick();}
    d::MountedCameraPose tick(bool eligible=true,bool blocked=false,double dt=.016){time+=dt;bool any=false;for(bool k:keys)any|=k;return m.tick(view,eligible,keys,any,time,clears,blocked);}
    void release(){keys.fill(false);tick();}
    void press(unsigned action){release();keys[m.effective.bindings[action]]=true;tick();}
};
void model_checks(const Ini& ini){
    for(unsigned action=0;action<c::Count;++action){
        Driver a(ini);a.press(action);const auto p=a.m.effective.views[0];
        check(close_to(p.forward,action==0?.02f:action==1?-.02f:0),"forward/back metres");
        check(close_to(p.up,action==2?.02f:action==3?-.02f:0),"up/down metres");
        check(close_to(p.right,action==4?-.02f:action==5?.02f:0),"left/right metres");
        check(close_to(p.tilt,action==6?-1.f:action==7?1.f:0),"look direction degrees");
        check(close_to(p.fov,action==8?2.f:action==9?-2.f:0),"FOV direction degrees");
    }
    Driver a(ini);a.press(0);a.release();a.view=1;a.tick();a.press(2);
    check(close_to(a.m.effective.views[0].forward,.02f)&&close_to(a.m.effective.views[1].up,.02f),"independent views");
    a.press(10);check(close_to(a.m.effective.views[1].up,0)&&close_to(a.m.effective.views[0].forward,.02f),"reset only active view");
    a.m.effective.views[1].up=.7f;for(int i=0;i<60;++i)a.tick();
    check(close_to(a.m.effective.views[1].up,.7f),"held reset never repeats");
    Driver repeat(ini);repeat.press(0);for(int i=0;i<10;++i)repeat.tick();
    check(close_to(repeat.m.effective.views[0].forward,.02f),"repeat delay");
    for(int i=0;i<12;++i)repeat.tick();check(close_to(repeat.m.effective.views[0].forward,.04f),"time-based repeat");
    repeat.tick(true,false,10);check(close_to(repeat.m.effective.views[0].forward,.04f),"stall no repeat catch-up");
    repeat.release();for(int i=0;i<150;++i)repeat.press(0);check(repeat.m.effective.views[0].forward==2.f,"pose clamp");
    for(int i=0;i<100;++i)repeat.press(8);check(repeat.m.effective.views[0].fov==60.f,"FOV clamp");
    for(int gate=0;gate<4;++gate){
        Driver g(ini);g.press(0);const auto before=g.m.effective.views[0].forward;
        if(gate==0)g.tick(false);if(gate==1)g.tick(true,true);if(gate==2){++g.clears;g.tick();}if(gate==3)g.tick(true,false,.5);
        for(int i=0;i<40;++i)g.tick();check(close_to(g.m.effective.views[0].forward,before),"lifecycle requires release");
        g.press(0);check(g.m.effective.views[0].forward>before,"fresh press after lifecycle gate");
    }
    Driver disabled(ini);disabled.m.enabled=false;disabled.press(0);check(disabled.m.effective.views[0].forward==0,"record/playback/test policy");
    Driver capture(ini);capture.press(0);capture.m.begin_capture(1,capture.time);for(int i=0;i<40;++i)capture.tick();
    check(close_to(capture.m.effective.views[0].forward,.02f),"binding capture suppresses held adjustments");
    capture.m.cancel_capture();for(int i=0;i<40;++i)capture.tick();
    check(close_to(capture.m.effective.views[0].forward,.02f),"capture exit requires release");
    capture.press(0);check(close_to(capture.m.effective.views[0].forward,.04f),"new press after capture exit");
    capture.view=1;for(int i=0;i<40;++i)capture.tick();
    check(capture.m.effective.views[1].forward==0,"view change while held requires release");
    std::string why;auto invalid=repeat.m.effective;
    invalid.views[0].up=std::numeric_limits<float>::quiet_NaN();check(!repeat.m.valid(invalid,why),"NaN rejected");
    invalid=repeat.m.effective;invalid.move_step=std::numeric_limits<float>::infinity();check(!repeat.m.valid(invalid,why),"infinite step rejected");
    invalid=repeat.m.effective;invalid.fov_step=0;check(!repeat.m.valid(invalid,why),"zero step rejected");
    // A physical key held across a clear cannot become a press from typematic events.
    Driver physical(ini);c::KeyStates none{};
    physical.m.tick(0,true,none,true,physical.time+=.016,++physical.clears);
    physical.keys[0x48]=true;physical.tick();check(physical.m.effective.views[0].forward==0,"physical release barrier");
}
void persistence(const fs::path& path,const Ini& ini){
    c::Model m;m.load(ini,true);auto candidate=m.effective;candidate.bindings[0]=0x3b;candidate.views[0].up=.4f;
    check(m.propose(candidate),"custom binding and pose save");
    const auto contents=read(path);
    check(contents.find("; keep calibration\r\n")!=std::string::npos&&contents.find("steer = custom bytes\r\n")!=std::string::npos,"unrelated calibration/bindings bytes retained");
    check(contents.find("CustomCameraKey = keep")!=std::string::npos,"unowned camera key retained");
    Ini reloaded;check(reloaded.load(path.string()),"reload saved INI");c::Model fresh;fresh.load(reloaded,true);
    check(fresh.effective.bindings[0]==0x3b&&close_to(fresh.effective.views[0].up,.4f),"restart values retained");
    auto next=m.effective;next.bindings[1]=0x3b;check(!m.propose(next)&&!m.proposal,"duplicate assignment refused");
    next=m.effective;next.bindings[1]=0x1c;check(!m.propose(next),"game assignment protected");
    for(unsigned scan:{0x40u,0x42u,0x57u,0x1du,0xc8u}){next=m.effective;next.bindings[1]=scan;check(!m.propose(next),"reserved/navigation/modifier rejected");}
    m.reserved.push_back(0x48);next=m.effective;next.bindings=c::Config{}.bindings;
    const auto before=read(path);check(!m.propose(next)&&read(path)==before&&m.effective.bindings[0]==0x3b,"default batch conflict rolls back atomically");
    m.reserved.clear();next=m.effective;next.bindings=c::Config{}.bindings;
    auto publish=outrun::desktop::settings_publish();outrun::desktop::settings_publish()=refuse_publish;
    check(!m.propose(next)&&m.proposal.has_value(),"save failure keeps recoverable proposal");
    check(m.effective.bindings[0]==0x3b&&read(path)==before,"save failure retains effective bindings and original file");
    outrun::desktop::settings_publish()=publish;check(m.retry()&&!m.proposal&&m.effective.bindings[0]==0x48,"retry publishes complete default batch");
    next=m.effective;next.bindings[0]=0;outrun::desktop::settings_publish()=refuse_publish;
    check(!m.propose(next),"clear save failure");m.cancel_proposal();check(m.effective.bindings[0]==0x48&&!m.proposal,"cancel failed clear retains prior binding");
    outrun::desktop::settings_publish()=publish;
    m.edit_pose(1,{0,.3f,0,0,0},10);const auto deferred=read(path);m.flush(10.1);check(read(path)==deferred,"pose save deferred");m.flush(10.8);
    check(read(path)!=deferred,"idle pose saved");
    m.begin_capture(0,20);m.capture({},false,false,20.1);m.capture({0x3b},true,false,20.2);m.capture({},false,false,20.3);
    check(!m.capturing()&&m.effective.bindings[0]==0x3b,"single key capture saves");
    m.begin_capture(0,21);m.capture({},false,false,21.1);m.capture({0x3c},true,true,21.2);
    check(m.effective.bindings[0]==0x3b&&!m.capturing(),"chord capture keeps previous");
    m.begin_capture(0,21.3);m.capture({},false,false,21.4);m.capture({0x3c},true,false,21.5);m.capture({0x3d},true,false,21.6);
    check(m.effective.bindings[0]==0x3b&&!m.capturing(),"two regular keys rejected as chord");
    m.begin_capture(0,22);m.cancel_capture();check(m.effective.bindings[0]==0x3b,"capture cancel keeps prior");
    m.begin_capture(0,23);m.capture({},false,false,34);check(!m.capturing()&&m.effective.bindings[0]==0x3b,"capture timeout keeps prior");
    m.begin_capture(0,35);m.capture({},false,false,35.1);m.capture({0x50},true,false,35.2);
    check(m.effective.bindings[0]==0x3b,"capture conflict keeps prior");
    Ini hud;hud.load_text("[Misc]\nHudToggleKey=NUMPAD8\n",path.string());c::Model collision;collision.load(hud,true);
    check(collision.effective.bindings[0]==0,"HUD collision does not steal assignment");
    next=collision.effective;next.bindings=c::Config{}.bindings;check(!collision.propose(next),"HUD reserved default batch refused");
    Ini damaged;damaged.load_text("[CameraAdjustment]\nBumperUp=99\nInCarForward=0.3\nMoveStep=-1\nKey0=59\nKey1=60\nKey2=59\n",path.string());
    c::Model recovered;recovered.load(damaged,true);
    check(recovered.effective.views[0].up==0&&close_to(recovered.effective.views[1].forward,.3f),"invalid pose recovery preserves other view");
    check(close_to(recovered.effective.move_step,.02f)&&recovered.effective.bindings[0]==59&&recovered.effective.bindings[1]==60&&
        recovered.effective.bindings[2]==0,"startup preserves valid custom bindings and unbinds only conflict");
    recovered.reserved.push_back(0x3d);next=recovered.effective;next.bindings[2]=0x3d;
    check(!recovered.propose(next),"next-start HUD reservation protects later camera edits");
}
void native_race_checks(){
    auto run=[](unsigned view,bool armed,d::MountedCameraPose adjustment,unsigned transition=0){
        std::array<std::uint8_t,0x3b4> camera{};std::array<std::uint8_t,0xea2> car{};
        std::array<std::uint8_t,0x64c> entries{};std::array<std::uint8_t,0x438> models{};
        std::array<std::uint8_t,0x5460> chase{};std::array<std::uint8_t,4096> arena{};
        d::Bytes cam(camera.data(),camera.size()),table(entries.data(),entries.size());
        for(unsigned v=0;v<3;++v){auto entry=table.sub(v*0x34,0x34);
            entry.putf(4,2);entry.putf(8,10);entry.putf(0x10,2);entry.putf(0x14,9);entry.putf(0x24,1.f);}
        d::Bytes model(models.data(),models.size());model.putf(4,2);model.putf(8,10);
        d::Bytes following(chase.data(),chase.size());following.putf(4,2);following.putf(8,10);following.putf(0x10,2);following.putf(0x14,9);
        cam.put8(0x34a,std::uint8_t(view));cam.putf(0xb8,16.f/9);cam.putf(0xbc,.1f);cam.putf(0xc0,5000);cam.put16(0x33c,std::uint16_t(transition));
        d::PcRaceCameraInputs in;in.car=d::Bytes(car.data(),car.size());in.live_818fe8=table;
        in.live_818bb0=model;in.live_813750=following;in.ground_43eb60=[](d::CourseProbe& p){p.y=-100;};
        d::PcMatrixStack stack{d::Bytes(arena.data(),arena.size()),0,0,64};d::pc_matrix_identity(stack);
        auto& context=d::mounted_camera_context();context.armed=armed;context.sample=[=](d::Bytes){return adjustment;};
        const auto frame=[&]{d::PcCameraDevice device;d::race_camera_race_4858d0(cam,stack,device,{1920,1080},{},in);
            check(stack.depth==0&&device.transforms.size()==2,"race path preserves native projection and stack");};
        frame();const auto first=camera;
        if(!transition){for(unsigned n=0;n<100;++n)frame();
            d::Bytes before(const_cast<std::uint8_t*>(first.data()),first.size());
            for(unsigned field:{0xf8u,0xfcu,0x100u,0x104u,0x108u,0x10cu,0xa0u})
                check(close_to(cam.f32(field),before.f32(field)),"actual race path recomputes adjustment without drift");}
        context={};return camera;
    };
    for(unsigned view=0;view<3;++view){
        const auto stock=run(view,false,{});check(run(view,true,{})==stock,"zero adjustment preserves native race result");
        auto adjusted=run(view,true,{.02f,.03f,.04f,1,2});
        if(view==2)check(adjusted==stock,"chase camera remains unchanged");
        else{d::Bytes a(adjusted.data(),adjusted.size());auto original=stock;d::Bytes b(original.data(),original.size());
            check(close_to(a.f32(0xf8)-b.f32(0xf8),.02f)&&close_to(a.f32(0xfc)-b.f32(0xfc),.03f)&&
                close_to(a.f32(0x100)-b.f32(0x100),-.04f),"mounted race offsets use native metre axes");
            check(close_to(a.f32(0xa0)-b.f32(0xa0),2*.01745329252f),"race path scopes FOV offset");}
    }
    check(run(0,true,{.02f,.03f,.04f,1,2},2)==run(0,false,{},2),"native view interpolation is untouched");
    std::array<std::uint8_t,0x3b4> bytes{};d::Bytes cam(bytes.data(),bytes.size());cam.put8(0x34a,2);
    for(unsigned expected:{1u,0u,2u}){d::race_camera_view_switch_484a40(cam,0x40000,0);check(cam.u8(0x34a)==expected,"stock Change view cycle retained");}
}
void native_checks(){
    std::array<std::uint8_t,0x3b4> bytes{};d::Bytes b(bytes.data(),bytes.size());
    b.putf(0x100,10);b.putf(0x10c,9);auto stock=bytes;
    check(d::mounted_camera_eligible(b),"bumper native eligibility");b.put8(0x34a,1);check(d::mounted_camera_eligible(b),"in-car native eligibility");
    for(unsigned view:{2u,3u,0x1au,0x1bu,0x1du}){b.put8(0x34a,std::uint8_t(view));check(!d::mounted_camera_eligible(b),"chase/scripted views excluded");}
    bytes=stock;for(unsigned field:{0x33cu,0x340u,0x344u,0x364u,0x94u,0x98u,0x9cu}){b.put32(field,1);check(!d::mounted_camera_eligible(b),"native transitions/debug excluded");bytes=stock;}
    d::mounted_camera_pose(b,{});check(bytes==stock,"zero pose preserves stock bytes");
    d::mounted_camera_pose(b,{.02f,.03f,.04f,0,0});
    check(close_to(b.f32(0xf8),.02f)&&close_to(b.f32(0xfc),.03f)&&close_to(b.f32(0x100),9.96f),"native local axes in metres");
    for(int i=0;i<1000;++i){bytes=stock;d::mounted_camera_pose(b,{.02f,.03f,.04f,0,0});}
    check(close_to(b.f32(0xf8),.02f)&&close_to(b.f32(0x100),9.96f),"recomputed native pose has no frame drift");
    bytes=stock;d::mounted_camera_pose(b,{0,0,0,1,0});check(b.f32(0x108)>0,"positive tilt looks up");
    bytes=stock;d::mounted_camera_pose(b,{0,0,0,-1,0});check(b.f32(0x108)<0,"negative tilt looks down");
    bytes=stock;d::mounted_camera_pose(b,{100,0,0,0,0});check(bytes==stock,"native bound validation");
    auto project=[&](float offset,float rig){
        bytes=stock;b.putf(0xac,1);b.putf(0xb8,16.f/9);b.putf(0xbc,.1f);b.putf(0xc0,5000);
        std::array<std::uint8_t,4096> arena{};d::PcMatrixStack stack{d::Bytes(arena.data(),arena.size()),0,0,64};d::pc_matrix_identity(stack);d::PcCameraDevice device;
        d::camera_fov_context().fov_y=rig;d::mounted_camera_context().fov_delta=offset;
        d::camera_project_484bd0(b,stack,device,{1920,1080},{});
        check(device.transforms.size()==2&&stack.depth==0,"native projection/view stores and balanced stack");
        d::mounted_camera_context().fov_delta=0;d::camera_fov_context().fov_y=0;
        return b.f32(0xa0);
    };
    const auto normal=project(0,0);check(project(2,0)>normal&&project(-2,0)<normal,"FOV units and direction");
    check(close_to(project(2,1.f),1.f+2*.01745329252f),"offset preserves physical triple FOV baseline");
    check(close_to(project(1000,0),2.44346095f)&&close_to(project(-1000,0),.34906585f),"effective FOV bounds");
    // Camera-only keys must not switch wheel input custody to the native keyboard.
    using namespace outrun::desktop;desktop_keys_clear();desktop_keys()[0x48]=0x80;
    desktop_presentation_key()=[](unsigned scan){return scan==0x48;};check(!desktop_any_key(),"camera key does not seize simulation keyboard custody");
    desktop_keys()[0x1e]=0x80;check(desktop_any_key(),"game keys still hand off normally");desktop_presentation_key()=nullptr;desktop_keys_clear();
}
int main(int argc,char** argv){try{
    check(argc==2,"fixture directory argument required");const auto root=fs::path(argv[1])/std::to_string(GetCurrentProcessId());
    check(fs::create_directories(root),"fresh private fixture directory");const auto path=root/"settings.ini";
    write(path,"; keep calibration\r\n[Controls]\r\nsteer = custom bytes\r\n[Wheel]\r\nCalibration=preserve\r\n[CameraAdjustment]\r\nCustomCameraKey = keep\r\n");
    Ini ini;check(ini.load(path.string()),"fixture load");model_checks(ini);persistence(path,ini);native_checks();native_race_checks();
    fs::remove_all(root);std::cout<<"camera_adjustment: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"camera_adjustment FAIL: "<<e.what()<<" after "<<checks<<" checks\n";return 1;}}
