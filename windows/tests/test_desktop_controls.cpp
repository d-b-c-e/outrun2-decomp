// windows/: the canonical [Controls] store (controls_store), the legacy import,
// the settings writer and the test-injection session/commands/delivery.
#include "controls_store.hpp"
#include "desktop_settings.hpp"
#include "test_injection.hpp"
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace outrun::desktop;
namespace ctl=dbce::controls;

namespace {
int checks=0;
void check(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
const std::string Wheel="{d71b8350-61b7-11f1-8001-444553540000}",WheelProd="{0006346e-0000-0000-0000-504944564944}";
const std::string Shifter="{d9b1d890-6f3f-11f1-8002-444553540000}",ShifterProd="{0002f3a1-0000-0000-0000-504944564944}";

void test_report(){
    const auto s=ctl::parseSection({"Schema = 1","Profile = Rig","Revision = abc","Transmission = H-pattern",
        "steer = axis 0 dev="+Wheel+" prod="+WheelProd+" range=0..65535 rest=33265 travel=-1 name=\"MOZA R12 Base\"",
        "clutch = axis 4 dev="+Wheel+" prod="+WheelProd+" range=0..65535 rest=0 travel=+1",
        "gear3 = button 2 dev="+Shifter+" prod="+ShifterProd,
        "navUp = hat 0 0 dev="+Wheel+" prod="+WheelProd,
        "brake = button 1",
        "horn ="});
    const auto r=report_controls(s);
    check(r.ok,"Schema 1 applies");
    check(r.start_line=="controls: profile \"Rig\" revision abc via ini-controls-1: applied 3 (steer,gear3,navUp); unsupported 1 (clutch); invalid 1 (brake)",
          "start line: "+r.start_line);
    check(r.notes.size()==2,"one note per unsupported / invalid action");
    check(unavailable_reason("gear6")==nullptr&&unavailable_reason("gear7")!=nullptr&&unavailable_reason("reverse")!=nullptr,"gear coverage");
    const auto none=report_controls(ctl::parseSection({"steer = axis 0 dev="+Wheel+" prod="+WheelProd+" range=0..65535 rest=0 travel=+1"}));
    check(!none.ok&&none.start_line=="controls: nothing applied via ini-controls-1: no Schema","no Schema: nothing applies");
    const auto two=report_controls(ctl::parseSection({"Schema = 2"}));
    check(!two.ok&&two.start_line.find("Schema 2 is not supported")!=std::string::npos,"Schema 2 refused");
}

// The owner's (Redux) Tweaks settings: steering 0, throttle 2, brake 5, Moza buttons, DS-8X H-pattern.
void test_import(){
    LegacyControls c;
    c.steer_axis=0;c.accel_axis=2;c.brake_axis=5;
    c.buttons={31,18,33,32,34,21,13,12,32,4,6,7,5};
    c.hpattern=true;c.gears={0,1,2,3,4,5,7};
    LegacyDevices d;d.wheel={Wheel,WheelProd,"MOZA R12 Base"};d.shifter={Shifter,ShifterProd,"DS-8X Shifter"};
    std::vector<std::string> notes;
    const auto lines=import_legacy(c,d,notes);
    check(lines[0]=="Schema = 1"&&lines[3]=="Transmission = H-pattern","header lines");
    const auto s=ctl::parseSection(lines);
    check(s.error.empty()&&s.invalid.empty(),"the import parses back with nothing invalid");
    // steer throttle brake + confirm back start select shiftUp shiftDown camera navUp navDown navLeft navRight + 6 gears + reverse
    check(s.bound.size()==3+11+7,"bound count "+std::to_string(s.bound.size()));
    check(notes.size()==2,"ButtonX and ButtonY are noted, not imported");
    const auto* steer=ctl::find(s,"steer");
    check(steer&&steer->dev==Wheel&&steer->prod==WheelProd&&steer->index==0&&!steer->inverted,"steer binding");
    const auto* throttle=ctl::find(s,"throttle");
    check(throttle&&throttle->index==2&&throttle->rest==0&&throttle->travel==1,"throttle binding");
    // Tweaks' scaling: pedal = raw / 65535 (1 - that when inverted); steering (raw - 32767.5) / 32767.5.
    for(long raw:{0L,1000L,32767L,65535L})check(std::fabs(ctl::normalize("throttle",*throttle,raw)-double(raw)/65535.0)<1e-12,"pedal scaling at "+std::to_string(raw));
    check(std::fabs(ctl::normalize("steer",*steer,65535)-1.0)<1e-12&&std::fabs(ctl::normalize("steer",*steer,0)+1.0)<1e-12,"steering scaling");
    const auto* gear1=ctl::find(s,"gear1");const auto* reverse=ctl::find(s,"reverse");
    check(gear1&&gear1->dev==Shifter&&gear1->index==0&&reverse&&reverse->index==7,"H-pattern gates on the shifter");
    const auto* up=ctl::find(s,"navUp");
    check(up&&up->kind==ctl::Kind::Button&&up->index==4,"Moza D-pad buttons stay buttons");
    // A POV number becomes a hat direction; an inverted pedal stays the Tweaks inversion.
    LegacyControls p;p.buttons={128+3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};p.accel_invert=true;
    std::vector<std::string> n2;
    const auto s2=ctl::parseSection(import_legacy(p,LegacyDevices{{Wheel,WheelProd,"W"}},n2));
    const auto* hat=ctl::find(s2,"confirm");const auto* inv=ctl::find(s2,"throttle");
    check(hat&&hat->kind==ctl::Kind::Hat&&hat->index==0&&hat->angle==27000,"POV 131 = hat 0 left");
    check(inv&&std::fabs(ctl::normalize("throttle",*inv,65535))<1e-12&&std::fabs(ctl::normalize("throttle",*inv,0)-1.0)<1e-12,"inverted pedal");
    std::vector<std::string> n3;
    check(import_legacy(c,LegacyDevices{},n3).size()==4&&!n3.empty(),"no wheel: nothing imported");
}

void test_write_section(){
    char dir[MAX_PATH];GetTempPathA(MAX_PATH,dir);
    const std::string path=std::string(dir)+"or2_desktop_settings_test.ini";
    {std::ofstream f(path,std::ios::binary);f<<"; head\r\n[Wheel]\r\nSteeringSensitivity = 1.0\r\n\r\n[Controls]\r\nSteeringDeadZone = 0.0\r\nold = x\r\n\r\n[Window]\r\nWindowedBorderless = true\r\n";}
    std::string error;
    check(write_section(path,"Controls",{"Schema = 1","steer ="},error),"replace: "+error);
    check(write_section(path,"ControlsImport",{"From = test"},error),"append: "+error);
    std::ifstream f(path,std::ios::binary);std::stringstream s;s<<f.rdbuf();
    check(s.str()=="; head\r\n[Wheel]\r\nSteeringSensitivity = 1.0\r\n\r\n[Controls]\r\nSchema = 1\r\nsteer =\r\n\r\n[Window]\r\nWindowedBorderless = true\r\n\r\n[ControlsImport]\r\nFrom = test\r\n",
          "file after the writes:\n"+s.str());
    Ini ini;ini.load(path);
    check(ini.section_lines("Controls").size()==3&&ini.section_lines("controls")[0]=="Schema = 1","raw section lines, case-insensitive section name");
    std::remove(path.c_str());
}

// write_key (the F6 panel's saves): one key, every other byte kept; repeated writes do not grow the file.
void test_write_key(){
    char dir[MAX_PATH];GetTempPathA(MAX_PATH,dir);
    const std::string path=std::string(dir)+"or2_desktop_write_key_"+std::to_string(GetCurrentProcessId())+".ini";
    const std::string controls="[Controls]\r\nSchema = 1\r\nsteer = axis 0 dev={d71b8350-61b7-11f1-8001-444553540000}\r\n";
    {std::ofstream f(path,std::ios::binary);f<<"; head\r\n"<<controls<<"\r\n[FFB]\r\n; tuning\r\nffbspringstrength = 0.45\r\nFFBDamperStrength = 0.10\r\n\r\n[Window]\r\nFrameLatency = 2\r\n";}
    std::string error;
    check(write_key(path,"FFB","FFBSpringStrength","0.30",error),"replace (key in another case): "+error);
    check(write_key(path,"FFB","FFBGlobalStrength","0.80",error),"append to the section: "+error);
    check(write_key(path,"Triple","Screens","Separate monitors",error),"a new section: "+error);
    const auto read=[&]{std::ifstream f(path,std::ios::binary);std::stringstream s;s<<f.rdbuf();return s.str();};
    const std::string want="; head\r\n"+controls+"\r\n[FFB]\r\n; tuning\r\nFFBSpringStrength = 0.30\r\nFFBDamperStrength = 0.10\r\n"
        "FFBGlobalStrength = 0.80\r\n\r\n[Window]\r\nFrameLatency = 2\r\n\r\n[Triple]\r\nScreens = Separate monitors\r\n";
    check(read()==want,"file after the writes:\n"+read());
    for(int i=0;i<3;++i)check(write_key(path,"FFB","FFBSpringStrength","0.30",error),"rewrite: "+error);
    check(read()==want,"repeated writes keep the file as it was (no blank lines added)");
    Ini ini;ini.load(path);
    check(ini.text("FFB","FFBSpringStrength")=="0.30"&&ini.text("Controls","steer").rfind("axis 0",0)==0&&ini.text("Triple","Screens")=="Separate monitors",
          "the game's reader sees the new values and the controls untouched");
    // Failures leave the file exactly as it was (romi 31154): a file that cannot be read is never taken as empty, and a
    // replacement that cannot be published keeps the original (no delete before the move).
    {const HANDLE lock=CreateFileA(path.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);   // no sharing: unreadable
     check(lock!=INVALID_HANDLE_VALUE,"lock the file");
     check(!write_key(path,"FFB","FFBSpringStrength","0.99",error)&&error.find("cannot read")!=std::string::npos,"an unreadable file is refused: "+error);
     check(!write_section(path,"Controls",{"Schema = 1"},error),"write_section refuses it too");
     CloseHandle(lock);}
    check(read()==want,"after the refused reads the file is unchanged, [Controls] included");
    {const HANDLE hold=CreateFileA(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);   // readable, not replaceable
     check(hold!=INVALID_HANDLE_VALUE,"hold the file without delete sharing");
     check(!write_key(path,"FFB","FFBSpringStrength","0.99",error)&&error.find("unchanged")!=std::string::npos,"a refused replacement fails: "+error);
     CloseHandle(hold);}
    check(read()==want,"after the refused replacement the original is intact");
    // A publish that fails after the temp file was written (the seam): the original must still be there, so nothing
    // may delete or truncate it before the move.
    {const auto real=settings_publish();
     settings_publish()=[](const std::string&,const std::string&){return false;};
     check(!write_key(path,"FFB","FFBSpringStrength","0.99",error)&&error.find("unchanged")!=std::string::npos,"a failed publish is reported: "+error);
     settings_publish()=real;}
    check(read()==want,"after the failed publish the original is intact");
    check(GetFileAttributesA((path+".tmp").c_str())==INVALID_FILE_ATTRIBUTES,"the temp file is removed");
    std::remove(path.c_str());
    check(write_key(path,"Triple","Screens","Off",error)&&read()=="[Triple]\nScreens = Off\n","a missing file is created with the one key: "+error);
    // A section that appears twice: Ini takes the last value, so writing one copy is refused instead (romi 31196).
    const std::string twice=controls+"[FFB]\r\nFFBSpringStrength = 0.45\r\n[Window]\r\nFrameLatency = 2\r\n[ffb]\r\nFFBSpringStrength = 0.60\r\n";
    {std::ofstream f(path,std::ios::binary|std::ios::trunc);f<<twice;}
    check(!write_key(path,"FFB","FFBSpringStrength","0.30",error)&&error.find("appears 2 times")!=std::string::npos,"duplicate [FFB] refused: "+error);
    check(read()==twice,"the file with the duplicate is unchanged");
    check(write_key(path,"Window","FrameLatency","1",error),"another section of that file still saves: "+error);
    {Ini dup;dup.load(path);check(dup.text("Window","FrameLatency")=="1"&&dup.text("FFB","FFBSpringStrength")=="0.60","the reader's view after it");}
    std::remove(path.c_str());
}
void test_injection(){
    std::string nonce,why;std::uint64_t expires=0;
    check(inject::parse_session("nonce=abcdefgh\nexpires=1000",900,nonce,expires,why)&&nonce=="abcdefgh"&&expires==1000,"session");
    check(!inject::parse_session("nonce=abc\nexpires=1000",900,nonce,expires,why),"short nonce refused");
    check(!inject::parse_session("nonce=abcdefgh\nexpires=5000",900,nonce,expires,why),"more than an hour ahead refused");
    check(!inject::parse_session("nonce=abcdefgh\nexpires=800",900,nonce,expires,why),"expired refused");
    const auto s=ctl::parseSection({"Schema = 1","steer = axis 0 dev="+Wheel+" prod="+WheelProd+" range=0..65535 rest=32768 travel=+1"});
    check(inject::test_arm(s),"armed");
    inject::test_clock(1000);
    std::vector<std::string> log;
    check(inject::test_file("nonce=other\ninject action steer 1 ms=500",log)==0,"another session's file is not read");
    check(inject::test_file("nonce=test\ninject action steer 1 ms=500\ninject raw button 3 dev="+Wheel+" value=1 ms=200\ninject action brake 1 ms=500",log)==2,
          "two commands accepted, the unbound action refused");
    DIJOYSTATE2 st{};st.lX=32768;
    log.clear();
    check(inject::deliver(Wheel,st,128,4,log)==2&&st.lX==65535&&st.rgbButtons[3]==0x80,"delivered: steer full right, button 3 down");
    DIJOYSTATE2 other{};
    check(inject::deliver(Shifter,other,128,4,log)==0,"another instance gets nothing");
    inject::test_clock(1300);
    DIJOYSTATE2 later{};later.lX=100;
    check(inject::deliver(Wheel,later,128,4,log)==1&&later.lX==65535&&later.rgbButtons[3]==0,"the 200 ms button ended, steering still runs");
    inject::test_clock(1600);
    DIJOYSTATE2 end{};end.lX=100;
    check(inject::deliver(Wheel,end,128,4,log)==0&&end.lX==100,"everything ended: the real sample passes");
}
}

// [Wheel] steering as both input paths read it (romi 25900): strtof accepts "nan" / "inf"; those become the defaults,
// finite values pass unchanged (the dead zone clamped to 0..0.9 as before).
void test_wheel_steering(){
    auto same=[](WheelSteering w,float s,float z){return w.sensitivity==s&&w.dead_zone==z;};
    check(same(wheel_steering(NAN,NAN),1.f,0.f),"NaN: the defaults");
    check(same(wheel_steering(INFINITY,-INFINITY),1.f,0.f),"infinities: the defaults");
    check(same(wheel_steering(1.5f,0.95f),1.5f,0.9f),"finite: unchanged, dead zone clamped to 0.9");
    check(same(wheel_steering(0.8f,-0.2f),0.8f,0.f),"a negative dead zone clamps to 0");
    check(same(wheel_steering(-1.f,0.1f),-1.f,0.1f),"a finite negative sensitivity passes, as before");
    char dir[MAX_PATH];GetTempPathA(MAX_PATH,dir);
    const std::string path=std::string(dir)+"or2_desktop_steering_test.ini";
    {std::ofstream f(path,std::ios::binary);f<<"[Wheel]\r\nSteeringSensitivity = nan\r\nSteeringDeadZone = inf\r\n";}
    Ini bad;bad.load(path);
    check(std::isnan(bad.number("Wheel","SteeringSensitivity",1.f))&&std::isinf(bad.number("Wheel","SteeringDeadZone",0.f)),
          "the reader does return nan / inf for these strings (so the case is real)");
    check(same(wheel_steering(bad),1.f,0.f),"[Wheel] nan / inf: the defaults");
    {std::ofstream f(path,std::ios::binary);f<<"[Wheel]\r\nSteeringSensitivity = 1.25\r\nSteeringDeadZone = 0.05\r\n";}
    Ini good;good.load(path);
    check(same(wheel_steering(good),1.25f,0.05f),"[Wheel] finite values as written");
    Ini none;none.load(path+".missing");
    check(same(wheel_steering(none),1.f,0.f),"no file: the defaults");
    std::remove(path.c_str());
}

int main(){
    try{test_report();test_import();test_write_section();test_write_key();test_injection();test_wheel_steering();}
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    std::cout<<"desktop controls: "<<checks<<" checks passed\n";
    return 0;
}
