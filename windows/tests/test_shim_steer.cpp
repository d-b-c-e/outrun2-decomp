// The shim's steering wheel for force feedback (source/shim_steer.hpp, INPUT-SHIM.md section 6): the [Controls] steer
// binding joined to the real Core's device reports, over the scripted provider. No DirectInput, no files, no game.
#include "shim_steer.hpp"
#include "shim_fake_provider.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using namespace dbce::shim;
namespace fk=dbce::shim::fake;
using outrun::desktop::ShimSteer;

namespace {
int checks=0;
void check(bool v,const std::string& what){++checks;if(!v)throw std::runtime_error(what);}

const std::string W="{d71b8350-61b7-11f1-8001-444553540000}",WP="{0006346e-0000-0000-0000-504944564944}";
const std::string S="{d9b1d890-6f3f-11f1-8002-444553540000}",SP="{0002f3a1-0000-0000-0000-504944564944}";
std::string steer(const std::string& dev,const std::string& prod){
    return "steer = axis 0 dev="+dev+" prod="+prod+" range=0..65535 rest=32768 travel=+1";}
std::string profile(const std::vector<std::string>& lines){
    std::string s="[Controls]\nSchema = 1\nProfile = Test\nRevision = r1\n";
    for(const auto& l:lines)s+=l+"\n";
    return s;
}

// One Core over a listed wheel and shifter, the profile parsed once: Core and the steer binding from the same bytes.
struct Rig {
    fk::Provider p;
    std::unique_ptr<Core> core;
    Profile parsed;
    const Frame* frame{};
    std::size_t w=0,s=0;
    explicit Rig(const std::string& text,CoreConfig cfg={}){
        fk::Device d;d.listed={W,WP,"Wheel"};d.caps=fk::wheel_caps();w=p.add(d);
        fk::Device x;x.listed={S,SP,"Shifter"};x.caps=fk::wheel_caps();s=p.add(x);
        parsed=read_profile(text);
        core=std::make_unique<Core>(p,cfg);
        core->set_profile(parsed);
    }
    void step(){core->service_devices();p.advance(10000);frame=&core->tick();}
    ShimSteer steer()const{return outrun::desktop::shim_steer(dbce::controls::find(parsed.section,"steer"),core->device_reports(),frame);}
    fk::Device& dev(std::size_t i){return p.at(i);}
};

void test_live_and_epochs(){
    Rig r(profile({steer(W,WP),"confirm = button 0 dev="+W+" prod="+WP}));
    auto x=r.steer();
    check(x.dev==W&&x.prod==WP&&!x.opened&&!x.live&&x.epoch==0,"before the first open: the binding's identity, not open, epoch 0");
    r.step();
    x=r.steer();
    check(x.dev==W&&x.prod==WP&&x.opened&&x.live&&x.epoch==1&&x.status=="live","opened by Core: open, live, epoch 1: "+x.status);
    r.dev(r.w).script.push_back({Call::Gone,Call::NotCalled});r.step();
    x=r.steer();
    check(!x.opened&&!x.live&&x.epoch==1&&x.status!="live","the device lost: not open, not live, the same epoch: "+x.status);
    r.step();
    x=r.steer();
    check(x.opened&&x.live&&x.epoch==2,"reopened: live in a new epoch (the FFB device epoch ends with the old one)");
    // An open device whose read is pending is not live steering (romi 31707: opened + epoch alone is not enough).
    r.dev(r.w).script.push_back({Call::Pending,Call::NotCalled});r.step();
    x=r.steer();
    check(!x.live,"a pending read: not live, whatever the report says: "+x.status);
}

void test_identity_rules(){
    // The parser writes the instance in its one lowercase form, so an uppercase profile still names Core's device.
    {std::string up=W;for(auto& c:up)if(c>='a'&&c<='f')c=char(c-'a'+'A');
     Rig r(profile({steer(up,WP)}));r.step();
     const auto x=r.steer();
     check(x.dev==W&&x.opened,"an uppercase GUID in the profile: the same instance, open");}
    // steer on the shifter instance: the identity is the one steer names, never the first or the force-capable one.
    {Rig r(profile({"confirm = button 0 dev="+W+" prod="+WP,steer(S,SP)}));r.step();
     const auto x=r.steer();
     check(x.dev==S&&x.prod==SP&&x.opened&&x.epoch==1,"steer bound to the second device: that device");}
    // Core keeps an instance's first binding's product; a steer binding with another product is not the wheel.
    {Rig r(profile({"confirm = button 0 dev="+W+" prod="+WP,steer(W,SP)}));r.step();
     const auto x=r.steer();
     check(!x.opened&&x.epoch==0&&x.status.find("product "+WP)!=std::string::npos,"a product disagreeing with Core's device: not open: "+x.status);}
    // The listing names another product for the instance: Core never opens it.
    {Rig r(profile({steer(W,WP)}));r.dev(r.w).listed.prod=SP;r.step();
     const auto x=r.steer();
     check(!x.opened&&x.status=="product-mismatch","the listed product mismatches: not open: "+x.status);}
    // The open reads back another instance: Core refuses the handle.
    {Rig r(profile({steer(W,WP)}));r.dev(r.w).opened={S,SP,"Other"};r.step();
     const auto x=r.steer();
     check(!x.opened,"the opened handle is another device: not open: "+x.status);}
    // The instance is not listed at all.
    {Rig r(profile({steer("{00000000-0000-0000-0000-000000000001}",WP)}));r.step();
     const auto x=r.steer();
     check(!x.opened&&x.epoch==0&&x.status=="device-missing","an instance not listed: not open: "+x.status);}
}

// Open but not usable: the steer axis's range is not qualified on the opened handle.
void test_open_not_live(){
    Rig r(profile({steer(W,WP)}));r.dev(r.w).caps.range_ok[0]=false;r.step();
    const auto x=r.steer();
    check(x.opened&&x.epoch==1&&!x.live,"an unqualified steer range: open, not live: "+x.status);
    // Live on another action of the same device does not make the steer live.
    Rig q(profile({steer(W,WP),"confirm = button 0 dev="+W+" prod="+WP}));q.dev(q.w).caps.range_ok[0]=false;q.step();
    check(q.steer().opened&&!q.steer().live,"the same device's other action live: the steer still not live");
}

void test_no_identity(){
    {Rig r(profile({"confirm = button 0 dev="+W+" prod="+WP}));r.step();
     const auto x=r.steer();
     check(x.dev.empty()&&!x.opened&&x.status=="steer is not bound","steer not bound: no identity, even with the wheel open");}
    // Steer not supported by the game's capability: Core reads no device for it.
    {CoreConfig cfg;cfg.capability.unsupported[std::size_t(action_index("steer"))]="test";
     Rig r(profile({steer(W,WP)}),cfg);r.step();
     const auto x=r.steer();
     check(x.dev==W&&!x.opened&&x.status=="the shim reads no device for this instance","steer unsupported: no device: "+x.status);}
    // A profile Core does not use (Schema 2): nothing bound, nothing opened.
    {Rig r("[Controls]\nSchema = 2\n"+steer(W,WP)+"\n");r.step();
     const auto x=r.steer();
     check(x.dev.empty()&&!x.opened,"an unusable profile: no identity");}
}
}

int main(){
    try{test_live_and_epochs();test_identity_rules();test_open_not_live();test_no_identity();}
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    std::cout<<"shim steer identity: "<<checks<<" checks passed\n";
    return 0;
}
