#include "controls_store.hpp"
#include <algorithm>
namespace outrun::desktop {
namespace {
std::string joined(const std::vector<std::string>& ids){
    std::string s;for(const auto& i:ids)s+=(s.empty()?"":",")+i;return s;
}
ctl::Binding base(const DeviceIdentity& d){ctl::Binding b;b.dev=d.dev;b.prod=d.prod;b.name=d.name;return b;}
ctl::Binding axis(const DeviceIdentity& d,int index,bool centred,bool inverted){
    auto b=base(d);b.kind=ctl::Kind::Axis;b.index=index;b.min=0;b.max=65535;b.hasRange=b.hasRest=true;
    b.rest=centred?32768:0;b.travel=1;b.inverted=inverted;   // Tweaks: raw / 65535, 1 - that when inverted
    return b;
}
// A Tweaks button number: 0..127 a button, 128 + hat*4 + direction a POV direction.
bool digital(const DeviceIdentity& d,int number,ctl::Binding& b){
    if(number<0||number>143)return false;
    b=base(d);
    if(number<128){b.kind=ctl::Kind::Button;b.index=number;}
    else{b.kind=ctl::Kind::Hat;b.index=(number-128)/4;b.angle=((number-128)%4)*9000;}
    return true;
}
}

const char* unavailable_reason(const std::string& a){
    if(a=="steer"||a=="throttle"||a=="brake"||a=="shiftUp"||a=="shiftDown"||a=="confirm"||a=="back"||a=="start"||
       a=="navUp"||a=="navDown"||a=="navLeft"||a=="navRight"||a=="camera"||a=="horn")return nullptr;
    if(a.size()==5&&a.compare(0,4,"gear")==0&&a[4]>='1'&&a[4]<='6')return nullptr;
    if(a=="clutch")return "OutRun 2006 has no clutch";
    if(a=="handbrake")return "OutRun 2006 has no handbrake";
    if(a=="neutral"||a=="reverse")return "OutRun 2006 has no neutral or reverse gear";
    if(a=="gear7"||a=="gear8")return "the H-pattern mapping covers gears 1-6";
    if(a=="select")return "no game action";
    if(a=="lookBack"||a=="reset")return "no game action mapped yet";
    if(a=="modMenu")return "no F6 panel yet";
    if(a=="muteFfb")return "no force feedback yet";
    return "unknown action";
}

ControlsReport report_controls(const ctl::Section& s){
    ControlsReport r;
    for(const auto& [action,why]:s.invalid){r.invalid.push_back(action);r.notes.push_back("controls: "+action+" invalid: "+why);}
    if(!s.error.empty()){
        r.start_line="controls: nothing applied via ini-controls-1: "+s.error;
        return r;
    }
    r.ok=true;
    for(const auto& e:s.bound){
        if(const char* why=unavailable_reason(e.action)){r.unsupported.push_back(e.action);r.notes.push_back("controls: "+e.action+" unavailable: "+why);}
        else r.applied.push_back(e.action);
    }
    r.start_line="controls: profile \""+s.profile+"\" revision "+(s.revision.empty()?std::string("-"):s.revision)+" via ini-controls-1: applied "+
        std::to_string(r.applied.size())+" ("+joined(r.applied)+"); unsupported "+std::to_string(r.unsupported.size())+" ("+joined(r.unsupported)+
        "); invalid "+std::to_string(r.invalid.size())+" ("+joined(r.invalid)+")";
    return r;
}

std::vector<std::string> import_legacy(const LegacyControls& c,const LegacyDevices& d,std::vector<std::string>& notes){
    std::vector<std::string> lines{"Schema = 1","Profile = Imported from OutRun2006Tweaks [DirectInput]","Revision = legacy-import",
                                   std::string("Transmission = ")+(c.hpattern&&d.shifter.present()?"H-pattern":"Sequential")};
    std::vector<std::string> done;
    auto put=[&](const std::string& action,const ctl::Binding& b){
        if(std::find(done.begin(),done.end(),action)!=done.end()){notes.push_back(action+": a second binding was not imported (one per action)");return;}
        done.push_back(action);lines.push_back(action+" = "+ctl::format(b));};
    if(!d.wheel.present()){notes.push_back("no wheel device: nothing imported");return lines;}
    if(c.steer_axis>=0&&c.steer_axis<=7)put("steer",axis(d.wheel,c.steer_axis,true,c.steer_invert));
    const auto& throttle=d.throttle.present()?d.throttle:d.wheel;
    const auto& brake=d.brake.present()?d.brake:d.wheel;
    if(c.accel_axis>=0&&c.accel_axis<=7)put("throttle",axis(throttle,c.accel_axis,false,c.accel_invert));
    if(c.brake_axis>=0&&c.brake_axis<=7)put("brake",axis(brake,c.brake_axis,false,c.brake_invert));
    // Tweaks Button<Name> -> action; X and Y have no controls-contract action.
    static constexpr const char* Actions[13]{"confirm","back",nullptr,nullptr,"start","select","shiftUp","shiftDown","camera",
                                             "navUp","navDown","navLeft","navRight"};
    static constexpr const char* Names[13]{"A","B","X","Y","Start","Back","GearUp","GearDown","ChangeView","SelUp","SelDown","SelLeft","SelRight"};
    auto buttons=[&](const DeviceIdentity& dev,const std::array<int,13>& set,const char* section){
        for(unsigned i=0;i<13;++i){
            if(set[i]<0)continue;
            ctl::Binding b;
            if(!Actions[i]){notes.push_back(std::string(section)+" Button"+Names[i]+": no controls-contract action, not imported");continue;}
            if(!digital(dev,set[i],b)){notes.push_back(std::string(section)+" Button"+Names[i]+" = "+std::to_string(set[i])+": not a button or POV number");continue;}
            put(Actions[i],b);
        }};
    buttons(d.wheel,c.buttons,"[DirectInput]");
    if(d.shifter.present()){
        ctl::Binding b;
        if(c.hpattern){
            static constexpr const char* Gears[7]{"gear1","gear2","gear3","gear4","gear5","gear6","reverse"};
            for(unsigned i=0;i<7;++i)if(digital(d.shifter,c.gears[i],b))put(Gears[i],b);
        }else{
            if(digital(d.shifter,c.shifter_up,b))put("shiftUp",b);
            if(digital(d.shifter,c.shifter_down,b))put("shiftDown",b);
        }
    }
    if(d.aux.present())buttons(d.aux,c.aux_buttons,"[DirectInput.Aux]");
    return lines;
}
}
