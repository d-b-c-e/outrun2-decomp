// Production editor + accepted Core envelope, synthetic samples and fresh disposable files only.
// No provider, reader, enumeration, input injection, game, window or physical force.
#include "desktop_bindings.hpp"
#include "controls_store.hpp"
#include "desktop_settings.hpp"
#include <windows.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace b=outrun::desktop::bindings;
namespace ctl=dbce::controls;
namespace sh=dbce::shim;
namespace fs=std::filesystem;
namespace {
int checks{};
void check(bool value,const char* what){++checks;if(!value)throw std::runtime_error(what);}
const std::string Wheel="{d71b8350-61b7-11f1-8001-444553540000}";
const std::string Product="{0006346e-0000-0000-0000-504944564944}";
struct Sample {
    sh::Frame frame;
    std::vector<sh::DeviceReport> reports;
    Sample(){
        frame.seq=1;frame.boundary_us=100'500;
        sh::DeviceReport device;
        device.identity={Wheel,Product,"Fixture wheel","fixture-interface"};
        device.opened=true;device.epoch=3;
        device.caps.axis[0]=device.caps.range_ok[0]=true;
        device.caps.min[0]=0;device.caps.max[0]=65535;device.caps.button.set(1);device.caps.pov[0]=true;
        reports.push_back(device);
        sh::DeviceSample sample;
        sample.device=0;sample.epoch=3;sample.seq=1;sample.valid=true;
        sample.poll=sample.state=sh::Call::Ok;sample.read_end_us=100'000;
        sample.physical.axes[0]=32767;frame.samples.push_back(sample);
    }
    std::vector<b::edit::AcceptedObservation> rows(double now){return b::accepted_observations(frame,reports,now);}
    void next(){++frame.seq;++frame.samples[0].seq;}
};
std::string read(const fs::path& path){std::ifstream file(path,std::ios::binary);std::stringstream bytes;bytes<<file.rdbuf();return bytes.str();}
void write(const fs::path& path,const std::string& bytes){std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<bytes;check(bool(file),"write fixture");}
std::string profile(){
    ctl::Binding steer;steer.kind=ctl::Kind::Axis;steer.index=0;steer.dev=Wheel;steer.prod=Product;
    steer.min=0;steer.max=65535;steer.rest=32767;steer.travel=1;steer.hasRange=steer.hasRest=true;
    steer.cal={1000,32767,64000};steer.name="Fixture wheel";
    auto throttle=steer;throttle.index=1;throttle.rest=0;throttle.cal={1000,64000};
    return "[Input]\r\nBackend = Shim\r\n[Controls]\r\nSchema = 2\r\nProfile = Fixture\r\nRevision = original\r\nTransmission = Sequential\r\n"
        "steer = "+ctl::format(steer)+"\r\nthrottle = "+ctl::format(throttle)+"\r\nOwnerControl = kept\r\n"
        "[FFB]\r\nModel = outrun-producer@2\r\nOutput = none\r\n[OwnerSection]\r\nUntouched = yes\r\n";
}
void envelope(){
    Sample good;const auto rows=good.rows(1);
    check(rows.size()==1&&rows[0].instance==Wheel&&rows[0].product==Product&&rows[0].sequence==1&&
          rows[0].sample.axes[0]==32767&&rows[0].axis_mask==1&&rows[0].range_mask==1,
          "only accepted Core sample + adopted same-epoch identity/caps becomes a capture observation");
    auto test=[](const Sample& sample){auto copy=sample;return copy.rows(1).empty();};
    {auto s=good;s.frame.latched=true;check(test(s),"latched frames never feed capture");}
    {auto s=good;s.frame.samples[0].injected.buttons.set(1);check(test(s),"injected samples never feed capture");}
    {auto s=good;s.frame.samples[0].epoch=4;check(test(s),"report/sample epoch mismatch refuses");}
    {auto s=good;s.reports[0].opened=false;check(test(s),"closed ownership refuses");}
    {auto s=good;s.reports[0].identity.interface_path.clear();check(test(s),"unknown handle interface identity refuses");}
    {auto s=good;s.reports[0].identity.prod="bad";check(test(s),"unproven product identity refuses");}
    {auto s=good;s.frame.boundary_us=300'000;check(test(s),"old physical read cannot become a fresh UI observation");}
    {auto s=good;s.frame.samples[0].state=sh::Call::Pending;check(test(s),"pending read refuses");}
    {auto s=good;s.frame.samples[0].stale=true;check(test(s),"Core stale flag refuses");}
    {auto s=good;s.frame.samples[0].seq=0;check(test(s),"missing physical read sequence refuses");}
    {auto s=good;s.reports[0].caps.max[0]=0;check(test(s),"unqualified axis range refuses");}
    check(!b::action_kind("clutch",ctl::Kind::Axis)&&!b::action_kind("reverse",ctl::Kind::Button)&&
          !b::action_kind("gear7",ctl::Kind::Button)&&b::action_kind("gear6",ctl::Kind::Button),
          "only OutRun-delivered actions and gears 1-6 are editable");
}
void draft_save(const fs::path& root){
    const auto path=root/"save.ini";const auto original=profile();write(path,original);
    b::Editor unavailable(path.u8string(),false);
    check(!unavailable.open()&&read(path)==original,"tests/playback cannot open an editable draft");
    b::Editor editor(path.u8string(),true);check(editor.open(),"open schema-2 binding draft");
    check(editor.transmission("H-pattern",true)&&editor.view(1,true).dirty,"transmission is staged");
    editor.cancel();check(read(path)==original&&editor.open()&&editor.view(1,true).transmission=="Sequential","Cancel discards transmission without writing");
    check(editor.invert(true,true)&&editor.transmission("H-pattern",true)&&read(path)==original,"input direction and mode stay local before Save");
    check(editor.save(true),"explicit guarded binding Save");
    const auto state=editor.view(1,true);check(state.pending_restart&&!state.opened,"Save requests restart and closes editor; no active input reload");
    outrun::desktop::Ini saved;check(saved.load(path.u8string()),"read saved fixture");
    const auto section=ctl::parseSection(saved.section_lines("Controls"),2);
    check(section.error.empty()&&section.schema==2&&section.transmission=="H-pattern","mode saved without schema downgrade");
    bool steering=false,throttle=false;
    for(const auto& action:section.bound){
        if(action.action=="steer")steering=action.binding.inverted&&action.binding.cal==std::vector<long>{1000,32767,64000};
        if(action.action=="throttle")throttle=action.binding.cal==std::vector<long>{1000,64000};
    }
    check(steering&&throttle,"direction edit and transmission preserve steering/pedal calibration");
    check(saved.text("Input","Backend")=="Shim"&&saved.text("FFB","Model")=="outrun-producer@2"&&saved.text("FFB","Output")=="none"&&
          saved.text("Controls","OwnerControl")=="kept"&&saved.text("OwnerSection","Untouched")=="yes",
          "Save preserves backend/force/unknown controls and sections");
    bool backup=false;
    for(const auto& entry:fs::directory_iterator(root))if(entry.path().filename().u8string().find("save.ini.bindings-backup-")==0)backup=read(entry.path())==original;
    check(backup,"exact pre-Save backup is retained");
}
void capture_and_conflict(const fs::path& root){
    const auto path=root/"capture.ini";const auto original=profile();write(path,original);
    b::Editor editor(path.u8string(),true);check(editor.open(),"open capture draft");
    Sample sample;editor.observe(sample.rows(1),1,true);
    check(editor.begin_capture(1,true),"explicit capture on saved device");
    sample.next();editor.observe(sample.rows(2.01),2.01,true); // one-second rest, then baseline
    sample.next();sample.frame.samples[0].physical.axes[0]=50000;editor.observe(sample.rows(2.02),2.02,true);
    auto state=editor.view(2.02,true);
    check(state.dirty&&!state.listening&&state.binding&&state.binding->kind==ctl::Kind::Axis&&state.binding->cal.empty()&&read(path)==original,
          "real capture flow stages a new axis without inventing calibration or saving");
    editor.cancel();check(editor.open()&&read(path)==original,"Cancel after capture leaves exact original");
    sample.next();sample.frame.samples[0].physical.axes[0]=32767;editor.observe(sample.rows(3),3,true);
    check(editor.select_action("confirm")&&editor.select_device(sample.rows(3)[0],3,true)&&editor.begin_capture(3,true),
          "an unbound action can explicitly use an already owned capture device");
    sample.next();editor.observe(sample.rows(3.01),3.01,true);
    sample.next();sample.frame.samples[0].physical.buttons[1]=0x80;editor.observe(sample.rows(3.02),3.02,true);
    state=editor.view(3.02,true);
    check(state.binding&&state.binding->kind==ctl::Kind::Button&&state.binding->index==1&&state.dirty&&read(path)==original,
          "button rising edge stages a draft binding");
    check(editor.begin_capture(3.03,true),"begin another bounded capture");
    sample.next();editor.observe(sample.rows(3.04),3.04,false);
    check(!editor.view(3.04,false).listening&&editor.view(3.04,false).dirty&&read(path)==original,"focus/eligibility loss cancels capture and preserves draft");
    editor.observe(sample.rows(3.04),3.04,true);
    check(editor.begin_capture(3.05,true),"capture can restart only through an explicit action");
    editor.observe(sample.rows(4),4,true); // repeated physical sequence, newly painted local timestamp
    check(!editor.view(4,true).listening&&editor.view(4,true).devices.empty(),"cached read repaint cannot refresh capture freshness");
    sample.next();sample.frame.samples[0].epoch=4;sample.reports[0].epoch=4;editor.observe(sample.rows(4.01),4.01,true);
    check(editor.view(4.01,true).selection==b::edit::SelectionMode::Unavailable&&!editor.begin_capture(4.01,true),
          "reconnect cannot silently replace an explicit selected device epoch");
    check(editor.select_device(sample.rows(4.01)[0],4.01,true)&&editor.begin_capture(4.01,true),"explicit reselection admits the new epoch");
    editor.observe({},4.02,true);check(!editor.view(4.02,true).listening,"reader loss cancels immediately");
    check(!editor.save(false)&&editor.view(4.02,false).dirty&&read(path)==original,"ineligible Save preserves draft and file");
    write(path,original+"[External]\r\nChange = kept\r\n");const auto changed=read(path);
    check(!editor.save(true)&&editor.view(4.03,true).dirty&&!editor.view(4.03,true).restart_required&&read(path)==changed,
          "external file change refuses Save before replacement and retains draft");
    editor.cancel();check(read(path)==changed,"Cancel never overwrites an external edit");
    check(!editor.select_action("clutch"),"unsupported actions are never offered as functional bindings");
}
void transmission_and_stop(const fs::path& root){
    const auto path=root/"stop.ini";auto original=profile();
    original.replace(original.find("Output = none"),13,"Output = wheel");write(path,original);
    outrun::desktop::Ini before;check(before.load(path.u8string()),"read original metadata and bindings");
    b::Editor editor(path.u8string(),true);check(editor.open(),"open mode-only draft without capture");
    check(editor.transmission("H-pattern",true)&&editor.save(true),"mode-only Save uses the shared draft store without device capture");
    outrun::desktop::Ini saved;check(saved.load(path.u8string()),"read mode-only Save");
    check(saved.text("Controls","Transmission")=="H-pattern"&&saved.text("Controls","Revision").find("f6-")==0&&
          saved.text("Controls","Profile")==before.text("Controls","Profile")&&
          saved.text("Controls","steer")==before.text("Controls","steer")&&
          saved.text("Controls","throttle")==before.text("Controls","throttle"),
          "mode-only editor Save replaces Revision and preserves Profile and exact binding values");
    check(editor.open()&&editor.invert(true,true),"open another dirty draft before Stop FFB");
    editor.hide();check(editor.opened()&&editor.view(1,false).dirty,"page hide retains an open dirty draft; other panel saves must stay blocked");
    std::string error;
    check(outrun::desktop::write_key(path.u8string(),"FFB","Output","none",error),"Stop FFB persists Off despite an open draft");
    editor.saved_file_changed();const auto stopped=read(path);
    auto state=editor.view(1,true);
    check(state.opened&&state.dirty&&state.file_changed&&!state.listening,"Stop marks the retained draft stale and stops capture");
    check(!editor.save(true)&&read(path)==stopped&&editor.view(1,true).dirty,"stale binding Save cannot restore Output wheel or discard the draft");
    editor.cancel();check(!editor.opened()&&read(path)==stopped,"explicit Cancel discards stale edits without changing saved Off");
    check(editor.open()&&!editor.view(1,true).file_changed&&editor.transmission("Sequential",true)&&editor.save(true),
          "explicit reopen uses the stopped file for a new draft");
    check(saved.load(path.u8string())&&saved.text("FFB","Output")=="none","later new-draft Save retains Stop FFB Off");
}
// The Native reader's sample of an opened device (wheel_input), and the installed Native shape: Schema 1 Wheelkit lines
// as in the owner's 2026-10-18 F6-saved OutRunDesktop.ini, with the shared fixture GUIDs.
b::NativeSample native_sample(){
    b::NativeSample s;s.dev=Wheel;s.prod=Product;s.name="MOZA R12 Base";s.epoch=1;s.sequence=1;s.buttons=64;s.povs=1;
    s.axes[0]=33265;s.pov_state[0]=0xffffffffu;return s;
}
const std::string NativeSteer="steer = axis 0 dev="+Wheel+" prod="+Product+" range=0..65535 rest=33265 travel=-1 name=\"MOZA R12 Base\"";
std::string native_profile(){
    const std::string at=" dev="+Wheel+" prod="+Product,name=" name=\"MOZA R12 Base\"";
    return "[Controls]\r\nSchema = 1\r\nProfile = Moza KS Pro Triple\r\nRevision = wheelkit-default-2026-10-10T00:32:05Z\r\nTransmission = Sequential\r\n"+
        NativeSteer+"\r\nthrottle = axis 2"+at+" range=0..65535 rest=0 travel=+1"+name+"\r\nclutch = axis 6"+at+" range=0..65535 rest=0 travel=+1"+name+
        "\r\nreverse = button 7"+at+name+"\r\nnavUp = hat 0 0"+at+name+"\r\ngear7 = button 6"+at+name+
        "\r\n\r\n[Wheel]\r\nSteeringSensitivity = 1.00\r\n[FFB]\r\nModel = outrun-producer@2\r\nOutput = wheel\r\n";
}
void native_envelope(){
    const auto rows=b::native_observations({native_sample()},1);
    check(rows.size()==1&&rows[0].identity_key=="native:"+Wheel&&rows[0].epoch==1&&rows[0].sequence==1&&rows[0].axis_mask==0xffu&&
          rows[0].range_mask==0xffu&&rows[0].maximum[0]==65535&&rows[0].sample.axes[0]==33265&&rows[0].button_mask.count()==64&&
          rows[0].hat_mask==1&&rows[0].sample.hats[0]==-1,"a Native read becomes a capture observation at the reader's 0..65535 range");
    auto refused=[](b::NativeSample s,double now=1){return b::native_observations({s},now).empty();};
    {auto s=native_sample();s.epoch=0;check(refused(s),"no open epoch refuses");}
    {auto s=native_sample();s.sequence=0;check(refused(s),"no successful read refuses");}
    {auto s=native_sample();s.axes[2]=-1;check(refused(s),"a read outside the reader's range refuses");}
    {auto s=native_sample();s.prod="bad";check(refused(s),"unproven product identity refuses");}
    {auto s=native_sample();s.pov_state[0]=36000;check(refused(s),"an invalid POV angle refuses");}
    check(refused(native_sample(),std::nan("")),"a non-finite clock refuses");
    {auto s=native_sample();s.buttons=200;s.button_state[127]=0x80;const auto big=b::native_observations({s},1);
     check(big.size()==1&&big[0].button_mask.count()==128&&big[0].sample.buttons[127],"button counts above DIJOYSTATE2 are clamped");}
}
void native_draft(const fs::path& root){
    const auto path=root/"native.ini";const auto original=native_profile();write(path,original);
    b::Editor editor(path.u8string(),true);check(editor.open()&&read(path)==original,"the installed Native Schema 1 shape opens losslessly; opening writes nothing");
    check(!editor.select_action("clutch")&&!editor.select_action("reverse")&&!editor.select_action("gear7"),"Native keeps the unsupported action refusals");
    auto sample=native_sample();auto rows=[&](double now){return b::native_observations({sample},now);};
    editor.observe(rows(1),1,true);check(editor.begin_capture(1,true),"capture on the saved Native steering device");
    ++sample.sequence;editor.observe(rows(2.01),2.01,true); // one-second rest, then baseline
    ++sample.sequence;sample.axes[0]=50000;editor.observe(rows(2.02),2.02,true);
    const auto state=editor.view(2.02,true);
    check(state.dirty&&state.binding&&state.binding->index==0&&state.binding->min==0&&state.binding->max==65535&&
          state.binding->rest==33265&&state.binding->travel==1&&read(path)==original,"Native capture stages a Schema 1 axis without saving");
    check(editor.transmission("H-pattern",true)&&editor.save(true),"explicit Native Save of the binding and shifter mode");
    outrun::desktop::Ini saved;check(saved.load(path.u8string()),"read the Native Save");
    const auto revision=saved.text("Controls","Revision");
    auto expected=original;
    const auto at=[&](const std::string& from,const std::string& to){expected.replace(expected.find(from),from.size(),to);};
    at("Revision = wheelkit-default-2026-10-10T00:32:05Z","Revision = "+revision);at("Transmission = Sequential","Transmission = H-pattern");
    at(NativeSteer,NativeSteer.substr(0,NativeSteer.find("travel=-1"))+"travel=+1 name=\"MOZA R12 Base\"");
    check(revision.find("f6-")==0&&read(path)==expected,"Native Save changes only the edited binding, Transmission and Revision lines");
    const auto section=ctl::parseSection(saved.section_lines("Controls"),outrun::desktop::ControlsReads);
    check(section.error.empty()&&section.schema==1&&section.transmission=="H-pattern","the Native reader admits the saved Schema 1 section");
}
}
int main(){
    fs::path root;
    try{
        root=fs::temp_directory_path()/("or2_bindings_"+std::to_string(GetCurrentProcessId())+"_"+std::to_string(GetTickCount64()));
        if(!fs::create_directory(root))throw std::runtime_error("Fixture directory must be fresh");
        envelope();draft_save(root);capture_and_conflict(root);transmission_and_stop(root);native_envelope();native_draft(root);
        fs::remove_all(root);
        std::cout<<"desktop bindings: "<<checks<<" checks passed; fake samples/disposable files only\n";return 0;
    }catch(const std::exception& error){std::cerr<<"desktop bindings: FAIL: "<<error.what()<<"; retained fixture "<<root.u8string()<<"\n";return 1;}
}
