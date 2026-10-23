// windows/: the canonical [Controls] store (controls_store), the legacy import,
// the settings writer and the test-injection session/commands/delivery.
#include "controls_store.hpp"
#include "desktop_settings.hpp"
#include "desktop_quality.hpp"
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
#include <utility>
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
    check(std::string(unavailable_reason("reverse"))=="OutRun 2006 has no reverse gear"&&std::string(unavailable_reason("modMenu"))==
          "not mapped yet (the F6 key opens the panel)","the native reasons are the shim capability's (outrun_actions.hpp)");
    const auto none=report_controls(ctl::parseSection({"steer = axis 0 dev="+Wheel+" prod="+WheelProd+" range=0..65535 rest=0 travel=+1"}));
    check(!none.ok&&none.start_line=="controls: nothing applied via ini-controls-1: no Schema","no Schema: nothing applies");
    const auto two=report_controls(ctl::parseSection({"Schema = 2"}));
    check(!two.ok&&two.start_line.find("Schema 2 is not supported")!=std::string::npos,"Schema 2 refused by a schema-1 reader (the parser's default)");
    // STD-037: what this build's readers apply (ControlsReads): a schema-2 section names its schema and calibrated actions;
    // a schema-1 section's line is unchanged, and its cal= stays ignored.
    check(ControlsReads==2,"this build reads controls schema 2");
    const auto cal=report_controls(ctl::parseSection({"Schema = 2","Profile = Rig","Revision = r2",
        "steer = axis 0 dev="+Wheel+" prod="+WheelProd+" range=0..65535 rest=33265 travel=-1 cal=1000..33265..64000",
        "navUp = hat 0 0 dev="+Wheel+" prod="+WheelProd},ControlsReads));
    check(cal.ok&&cal.start_line=="controls: profile \"Rig\" revision r2 via ini-controls-2: applied 2 (steer,navUp); unsupported 0 (); invalid 0 (); calibrated 1 (steer)",
          "schema 2 start line: "+cal.start_line);
    const auto one=report_controls(ctl::parseSection({"Schema = 1","Profile = Rig","Revision = r1",
        "steer = axis 0 dev="+Wheel+" prod="+WheelProd+" range=0..65535 rest=33265 travel=-1 cal=1000..33265..64000"},ControlsReads));
    check(one.ok&&one.start_line=="controls: profile \"Rig\" revision r1 via ini-controls-1: applied 1 (steer); unsupported 0 (); invalid 0 ()",
          "schema 1 at reads 2: the line as before, cal ignored: "+one.start_line);
    const auto three=report_controls(ctl::parseSection({"Schema = 3"},ControlsReads));
    check(!three.ok&&three.start_line.find("this mod reads 1 or 2")!=std::string::npos,"Schema 3 refused at reads 2");
}

void test_selected_hpattern_gates(){
    std::vector<std::string> lines{"Schema = 1","Profile = Selected rig","Transmission = H-pattern",
        "steer = axis 0 dev="+Wheel+" prod="+WheelProd+" range=0..65535 rest=32768 travel=+1"};
    for(int gear=1;gear<=6;++gear)
        lines.push_back("gear"+std::to_string(gear)+" = button "+std::to_string(gear-1)+" dev="+Shifter+" prod="+ShifterProd);
    lines.push_back("reverse = button 6 dev="+Shifter+" prod="+ShifterProd);
    lines.push_back("neutral = button 7 dev="+Shifter+" prod="+ShifterProd);
    const auto section=ctl::parseSection(lines,ControlsReads);
    const auto report=report_controls(section);
    check(section.error.empty()&&section.invalid.empty()&&section.transmission=="H-pattern","inert selected profile parses as H-pattern");
    check(report.applied.size()==7&&report.unsupported.size()==2,"Native reports six direct gates, reverse and neutral unsupported");
    for(int gear=1;gear<=6;++gear){
        const auto action="gear"+std::to_string(gear);
        const auto* binding=ctl::find(section,action);
        check(binding&&binding->kind==ctl::Kind::Button&&binding->dev==Shifter&&binding->index==gear-1&&
              ctl::normalize(action,*binding,1)>=0.5,"Native reader accepts selected gate "+action);
    }
    check(unavailable_reason("neutral")&&unavailable_reason("reverse"),"no invented neutral or reverse delivery");
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
    // steer throttle brake + confirm back horn start select shiftUp shiftDown camera navUp navDown navLeft navRight + 6 gears + reverse
    check(s.bound.size()==3+12+7,"bound count "+std::to_string(s.bound.size()));
    check(notes.size()==1,"ButtonY is noted, not imported");
    const auto* horn=ctl::find(s,"horn");   // Tweaks' X switch is its horn (AudioHooks_Update)
    check(horn&&horn->dev==Wheel&&horn->index==33,"ButtonX imports as the horn");
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

void test_import_steering_calibration(){
    const LegacyDevices devices{{Wheel,WheelProd,"W"}};
    LegacyControls c;c.steer_calibration=true;c.steer_points={"1000","26000","62000"};
    std::vector<std::string> notes;
    const auto lines=import_legacy(c,devices,notes);
    const auto s=ctl::parseSection(lines,ControlsReads);
    const auto* steer=ctl::find(s,"steer");const auto* throttle=ctl::find(s,"throttle");
    check(lines[0]=="Schema = 2"&&s.error.empty()&&s.invalid.empty()&&notes.empty(),"enabled measured steering imports through schema 2");
    check(steer&&steer->cal==std::vector<long>{1000,26000,62000}&&steer->rest==26000,"measured steering endpoints and center survive the canonical round trip");
    for(const auto& point:std::vector<std::pair<long,double>>{{0,-1},{1000,-1},{13500,-0.5},{26000,0},{44000,0.5},{62000,1},{65535,1}})
        check(steer&&std::fabs(ctl::normalize("steer",*steer,point.first)-point.second)<1e-12,"measured steering at "+std::to_string(point.first));
    check(throttle&&throttle->cal.empty()&&std::fabs(ctl::normalize("throttle",*throttle,12345)-12345.0/65535.0)<1e-12,"schema promotion preserves uncalibrated pedal scaling");
    c.steer_invert=true;notes.clear();
    const auto inverted=ctl::parseSection(import_legacy(c,devices,notes),ControlsReads);
    const auto* inv=ctl::find(inverted,"steer");
    check(inv&&ctl::normalize("steer",*inv,1000)==1&&ctl::normalize("steer",*inv,62000)==-1,"calibrated steering applies inversion once");
    c.steer_invert=false;c.steer_points={"1000.25","26000.5","62000.75"};notes.clear();
    const auto rounded=ctl::parseSection(import_legacy(c,devices,notes),ControlsReads);
    const auto* fractional=ctl::find(rounded,"steer");
    check(fractional&&fractional->cal==std::vector<long>{1000,26001,62001},"fractional Tweaks endpoints round to the nearest raw count");
    for(const auto& bad:std::vector<std::array<std::string,3>>{
            {"-1","26000","62000"},{"1000","26000","65536"},{"26000","1000","62000"},{"1000","1000","62000"},
            {"1000","1200","62000"},{"1000","2000","3000"},
            {"0","nan","65535"},{"0","32768","inf"},{"0","center","65535"},{"0","32768oops","65535"},{"0","","65535"}}){
        c.steer_points=bad;notes.clear();
        const auto refused=ctl::parseSection(import_legacy(c,devices,notes),ControlsReads);
        check(refused.schema==1&&!ctl::find(refused,"steer")&&ctl::find(refused,"throttle")&&notes.size()==1&&notes[0].find("steering not imported")!=std::string::npos,
              "invalid enabled calibration refuses steering without dropping other bindings");
    }
    c.steer_calibration=false;notes.clear();
    const auto disabled=ctl::parseSection(import_legacy(c,devices,notes),ControlsReads);
    const auto* old=ctl::find(disabled,"steer");
    check(disabled.schema==1&&old&&old->cal.empty()&&notes.empty(),"disabled calibration ignores invalid endpoints and keeps schema 1");
    c.steer_calibration=true;c.steer_axis=-1;notes.clear();
    const auto absent=ctl::parseSection(import_legacy(c,devices,notes),ControlsReads);
    check(absent.schema==1&&!ctl::find(absent,"steer")&&notes.empty(),"an unbound steering axis does not promote the schema");
}

void test_import_pedal_calibration(){
    const DeviceIdentity brake_device{"{d9b1d891-6f3f-11f1-8002-444553540000}",ShifterProd,"Brake"};
    const LegacyDevices devices{{Wheel,WheelProd,"Wheel"},{Shifter,ShifterProd,"Throttle"},brake_device};
    std::vector<std::string> notes;
    LegacyControls both;both.accel_axis=2;both.brake_axis=5;
    both.accel_calibration={true,{"1000","26000","61000"},"0"};
    both.brake_calibration={true,{"10000","10000","50000"},"0"};both.brake_invert=true;
    const auto section=ctl::parseSection(import_legacy(both,devices,notes),ControlsReads);
    const auto* throttle=ctl::find(section,"throttle");const auto* brake=ctl::find(section,"brake");
    check(section.schema==2&&section.invalid.empty()&&notes.empty(),"independent pedal calibrations round trip through schema 2");
    check(throttle&&throttle->dev==Shifter&&throttle->index==2&&throttle->cal==std::vector<long>{1000,61000},"throttle keeps its own device, axis and endpoints");
    check(brake&&brake->dev==brake_device.dev&&brake->index==5&&brake->cal==std::vector<long>{10000,50000},"brake keeps its own device, axis and endpoints");
    check(throttle&&brake&&ctl::normalize("throttle",*throttle,31000)==0.5&&ctl::normalize("brake",*brake,10000)==1&&ctl::normalize("brake",*brake,50000)==0,
          "separate pedal spans normalize independently and apply inversion once");
    for(bool is_brake:{false,true}){
        LegacyControls c;
        auto& calibration=is_brake?c.brake_calibration:c.accel_calibration;
        const char* action=is_brake?"brake":"throttle";
        const char* other=is_brake?"throttle":"brake";
        calibration={true,{"1000","26000","61000"},"0"};
        const auto imported=[&]{notes.clear();return ctl::parseSection(import_legacy(c,devices,notes),ControlsReads);};
        const auto calibrated=imported();const auto* binding=ctl::find(calibrated,action);
        for(const auto& point:std::vector<std::pair<long,double>>{{0,0},{1000,0},{16000,0.25},{31000,0.5},{61000,1},{65535,1}})
            check(binding&&std::fabs(ctl::normalize(action,*binding,point.first)-point.second)<1e-12,std::string(action)+" measured scaling and saturation at "+std::to_string(point.first));
        (is_brake?c.brake_invert:c.accel_invert)=true;
        const auto inverted=imported();const auto* inv=ctl::find(inverted,action);
        check(inv&&ctl::normalize(action,*inv,1000)==1&&ctl::normalize(action,*inv,16000)==0.75&&ctl::normalize(action,*inv,61000)==0,
              std::string(action)+" calibration preserves the native inversion exactly once");
        (is_brake?c.brake_invert:c.accel_invert)=false;
        calibration.points={"1000.25","26000.5","61000.75"};
        const auto rounded=imported();const auto* fractional=ctl::find(rounded,action);
        check(fractional&&fractional->cal==std::vector<long>{1000,61001},std::string(action)+" fractional endpoints round to whole raw counts");
        for(const auto& center:{"1000","61000"}){
            calibration.points={"1000",center,"61000"};
            const auto edge_center=imported();const auto* edge=ctl::find(edge_center,action);
            check(edge&&ctl::normalize(action,*edge,31000)==0.5,std::string(action)+" validates center but never uses it as a pedal midpoint");
        }
        for(const auto& bad:std::vector<std::array<std::string,3>>{
                {"-1","26000","61000"},{"1000","26000","65536"},{"61000","26000","1000"},
                {"1000","999","61000"},{"1000","61001","61000"},{"1000","1000","1000"},
                {"1000","5000","9191.99"},{"0","nan","65535"},{"0","32768","inf"},
                {"0","center","65535"},{"0","32768oops","65535"},{"0","","65535"},{"1e-999","32768","65535"}}){
            calibration.points=bad;
            const auto refused=imported();
            check(refused.schema==1&&!ctl::find(refused,action)&&ctl::find(refused,other)&&ctl::find(refused,"steer")&&
                  notes.size()==1&&notes[0].find(std::string(action)+": enabled")==0,
                  std::string(action)+" malformed/range/order/span calibration refuses only that pedal");
        }
        calibration.points={"0","4096","8192"};
        const auto minimum_span=imported();
        check(ctl::find(minimum_span,action)&&notes.empty(),std::string(action)+" accepts the native minimum measured span");
        for(const auto& deadzone:{"0.1","-0.1","nan","inf","oops","","0.0oops","1e-999"}){
            calibration.deadzone=deadzone;
            const auto refused=imported();
            check(refused.schema==1&&!ctl::find(refused,action)&&ctl::find(refused,other)&&notes.size()==1&&
                  notes[0].find("deadzone is unsupported")!=std::string::npos,
                  std::string(action)+" unsupported/nonfinite/malformed deadzone is reported without silently changing the curve");
        }
        calibration.enabled=false;calibration.points={"bad","nan","inf"};
        const auto disabled=imported();const auto* old=ctl::find(disabled,action);
        check(disabled.schema==1&&old&&old->cal.empty()&&notes.empty()&&std::fabs(ctl::normalize(action,*old,12345)-12345.0/65535.0)<1e-12,
              std::string(action)+" disabled calibration preserves the legacy full-range route, ignoring unused calibration/deadzone");
        calibration.enabled=true;(is_brake?c.brake_axis:c.accel_axis)=-1;
        const auto absent=imported();
        check(absent.schema==1&&!ctl::find(absent,action)&&notes.empty(),std::string(action)+" unbound axis neither promotes schema nor reports unused calibration");
    }
    both.accel_calibration.points={"bad","26000","61000"};notes.clear();
    const auto mixed=ctl::parseSection(import_legacy(both,devices,notes),ControlsReads);
    check(mixed.schema==2&&!ctl::find(mixed,"throttle")&&ctl::find(mixed,"brake")&&ctl::find(mixed,"steer")&&notes.size()==1,
          "a valid brake still promotes schema 2 when throttle calibration refuses");
    LegacyControls fallback;fallback.accel_calibration.enabled=true;notes.clear();
    const auto wheel_pedals=ctl::parseSection(import_legacy(fallback,LegacyDevices{{Wheel,WheelProd,"Wheel"}},notes),ControlsReads);
    const auto* wheel_throttle=ctl::find(wheel_pedals,"throttle");
    check(wheel_throttle&&wheel_throttle->dev==Wheel&&wheel_throttle->cal==std::vector<long>{0,65535},"calibrated pedal without a separate device keeps the wheel fallback");
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
void test_graphical_presets(){
    char dir[MAX_PATH];GetTempPathA(MAX_PATH,dir);
    const std::string path=std::string(dir)+"or2_quality_"+std::to_string(GetCurrentProcessId())+".ini";
    const std::string protected_sections="[Controls]\r\nSchema = 2\r\nTransmission = Sequential\r\nsteer = axis 0 dev="+Wheel+
        " prod="+WheelProd+" range=0..65535 rest=33265 travel=-1 cal=1000..33265..64000\r\n"
        "[FFB]\r\nModel = outrun-producer@2\r\nOutput = none\r\n[Triple]\r\nScreens = Separate monitors\r\nViews = Angled\r\n";
    const std::string original=protected_sections+"[Graphics]\r\n; owner's graphics\r\nAnisotropicFiltering = 3\r\nUITextureReplacement = yes\r\nOwnerUnknown = kept\r\n";
    {std::ofstream file(path,std::ios::binary);file<<original;}
    const auto read=[&]{std::ifstream file(path,std::ios::binary);std::stringstream bytes;bytes<<file.rdbuf();return bytes.str();};
    Ini settings;check(settings.load(path),"load quality fixture");
    check(quality::detect(settings)==quality::Custom&&read()==original,"opening a custom quality profile never rewrites it");
    const auto previous=quality::snapshot(settings);std::string error;
    for(int level=0;level<quality::Custom;++level){
        const auto next=quality::preset(level);
        check(write_keys(path,"Graphics",next,error),"explicit quality preset write: "+error);
        check(settings.load(path)&&quality::detect(settings)==level,"each quality level has its own actual key values");
        check(read().find(protected_sections)==0&&settings.text("Graphics","OwnerUnknown")=="kept",
              "visual presets preserve controls/calibration/force/triples and unknown keys");
        for(int other=0;other<level;++other)check(next!=quality::preset(other),"no two quality levels are identical");
    }
    const auto applied=quality::preset(4);
    const auto backup_path=path+".graphics-undo.ini";
    check(write_section(backup_path,"GraphicsUndo",quality::undo_document(previous,applied),error),"persist restart-safe Undo: "+error);
    Ini backup;SettingsEdits undo_previous,undo_expected;
    check(backup.load(backup_path)&&quality::read_undo(backup,undo_previous,undo_expected)&&undo_previous==previous&&undo_expected==applied,
          "a fresh reader recovers exact prior values/absence and the expected applied preset");
    std::remove(backup_path.c_str());
    check(write_keys(path,"Graphics",previous,error,&applied),"undo restores original values and missing keys: "+error);
    check(settings.load(path)&&settings.text("Graphics","AnisotropicFiltering")=="3"&&settings.text("Graphics","UITextureReplacement")=="yes"&&
          !settings.has_value("Graphics","ReflectionResolution")&&quality::detect(settings)==quality::Custom,"Undo restores exact prior values and absence");
    check(read().find(protected_sections)==0&&settings.text("Graphics","OwnerUnknown")=="kept","Undo preserves other settings");
    const auto before_failure=read();const auto publish=settings_publish();
    settings_publish()=[](const std::string&,const std::string&){return false;};
    const bool failed=write_keys(path,"Graphics",applied,error,&previous);settings_publish()=publish;
    check(!failed&&read()==before_failure,"failed multi-key publish preserves every original byte");
    check(write_keys(path,"Graphics",applied,error,&previous),"apply after a failed publish: "+error);
    check(write_key(path,"Graphics","AnisotropicFiltering","7",error),"external graphics change");
    const auto changed=read();
    check(!write_keys(path,"Graphics",previous,error,&applied)&&read()==changed,"Undo refuses changed managed keys without overwriting the edit");
    check(settings.load(path)&&quality::detect(settings)==quality::Custom,"individual override shows Custom");
    check(write_key(path,"Controls","Transmission","H-pattern",error),"explicit shifter mode change: "+error);
    check(settings.load(path)&&settings.text("Controls","Transmission")=="H-pattern"&&
          settings.text("Controls","steer").find("cal=1000..33265..64000")!=std::string::npos&&settings.text("Controls","Schema")=="2",
          "transmission-only write preserves schema and measured endpoints");
    const auto unchanged=read();
    check(!write_keys(path,"Graphics",{{"OwnerUnknown",std::string("bad\nInjected = true")}},error)&&read()==unchanged,
          "multi-key values cannot introduce extra settings");
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
    try{test_report();test_selected_hpattern_gates();test_import();test_import_steering_calibration();test_import_pedal_calibration();test_write_section();test_write_key();test_graphical_presets();test_injection();test_wheel_steering();}
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    std::cout<<"desktop controls: "<<checks<<" checks passed\n";
    return 0;
}
