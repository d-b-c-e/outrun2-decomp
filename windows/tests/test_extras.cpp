// Game-scoped extras (extension 2; hub artifacts/extra-actions-design-20261014 with the sana and deli reviews): the
// declaration grammar, the [Extras] plan beside the 31, and Core's extras-aware tick against the scripted provider. No
// DirectInput, file or game.
#include "shim.hpp"
#include "shim_fake_provider.hpp"
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using namespace dbce::shim;
namespace fk=dbce::shim::fake;

namespace {
int checks=0;
void check(bool v,const std::string& what){++checks;if(!v)throw std::runtime_error(what);}

const std::string W="{d71b8350-61b7-11f1-8001-444553540000}",WP="{0006346e-0000-0000-0000-504944564944}";
const std::string B="{a1b2c3d4-0000-11f1-8003-444553540000}",BP="{0000beef-0000-0000-0000-504944564944}";
const std::string OtherP="{0000cafe-0000-0000-0000-504944564944}";
std::string on(const std::string& dev,const std::string& prod,const std::string& rest){return rest+" dev="+dev+" prod="+prod;}
std::string wheel(const std::string& rest){return on(W,WP,rest);}
std::string box(const std::string& rest){return on(B,BP,rest);}
std::string axis(int i){return "axis "+std::to_string(i)+" dev="+W+" prod="+WP+" range=0..65535 rest=32768 travel=+1";}

const std::string Truckful="game truckful\ncontexts observed driving\n"
                           "truckful.look   stick circle   contexts=driving\n"
                           "truckful.reset  digital        contexts=driving\n";
Declaration declaration(const std::string& text){
    Declaration d;std::string why;
    if(!parse_declaration(text.data(),text.size(),d,why))throw std::runtime_error("declaration: "+why);
    return d;
}
bool refuses(const std::string& text,const std::string& part){
    Declaration d;std::string why;
    return !parse_declaration(text.data(),text.size(),d,why)&&why.find(part)!=std::string::npos;
}
std::string common(){return "[Controls]\nSchema = 1\nProfile = Rig\nRevision = r1\nTransmission = Sequential\nsteer = "+axis(0)+"\n"+
                            wheel("confirm = button 3")+"\n";}
std::string extras(const Declaration& d,const std::vector<std::string>& lines,const std::string& game="truckful",bool with_declaration=true){
    std::string s="[Extras]\nSchema = 1\nGame = "+game+"\n";
    if(with_declaration)s+="Declaration = "+hex(d.hash)+"\n";
    for(const auto& l:lines)s+=l+"\n";
    return s;
}
std::vector<DeviceInfo> common_devices(const std::string& profile){return plan_profile(read_profile(profile)).devices;}

void test_declaration(){
    const auto d=declaration(Truckful);
    check(d.game=="truckful"&&d.mode==ContextMode::Observed&&d.contexts.size()==1&&d.slots==3,"Truckful: one context, three slots");
    check(d.extras.size()==2&&d.extras[0].stick&&d.extras[0].circle&&d.extras[0].first_slot==0&&d.extras[0].components==2&&
          d.extras[1].shape==ctl::Shape::Digital&&d.extras[1].first_slot==2&&d.extras[0].contexts==1u&&d.context_bits()==1u,"stick X then Y, then reset");
    check(d.hash==sha256(Truckful),"the hash is of the exact bytes");
    const auto adv=declaration("game sonic\ncontexts advisory race\nsonic.trick stick square contexts=race\n");
    check(adv.mode==ContextMode::Advisory&&!adv.extras[0].circle,"advisory, square");
    check(declaration("game hct\nhct.nitro digital\n").mode==ContextMode::None,"no contexts line");
    check(refuses("","empty"),"empty refused");
    check(refuses(std::string(MaxDeclarationBytes+1,'a'),"4096"),"over 4096 bytes refused");
    check(refuses(std::string("game hct\nhct.nitro\0 digital\n",27),"ASCII"),"a NUL refused");
    check(refuses("game hct\nhct.n\xC3\xA9 digital\n","ASCII"),"non-ASCII refused");
    check(refuses("hct.nitro digital\n","first line"),"game must come first");
    check(refuses("game HCT\nHCT.nitro digital\n","first line"),"a game id outside [a-z0-9-] refused");
    check(refuses("game hct\nhct.nitro digital\ncontexts observed race\n","right after"),"contexts after an extra refused");
    check(refuses("game hct\ncontexts observed race\ncontexts observed menu\nhct.nitro digital\n","once"),"contexts twice refused");
    check(refuses("game hct\ncontexts sometimes race\nhct.nitro digital\n","observed|advisory"),"an unknown context mode refused");
    check(refuses("game hct\ncontexts observed race\nhct.nitro digital contexts=menu\n","not on the contexts line"),"an unknown context refused");
    check(refuses("game hct\nhct.nitro digital contexts=race\n","needs a"),"contexts= without a contexts line refused");
    check(refuses("game hct\ntruckful.nitro digital\n","must be"),"another game's prefix refused");
    check(refuses("game hct\nhct.Nitro digital\n","must be"),"a name not starting lower case refused");
    check(refuses("game hct\nhct.nitro digital\nhct.nitro pedal\n","twice"),"a duplicate id refused");
    check(refuses("game hct\nhct.nitro trigger\n","not digital"),"an unknown shape refused");
    check(refuses("game hct\nhct.look stick\n","range"),"a stick without its range refused");
    std::string many="game hct\n";
    for(int i=0;i<17;++i)many+="hct.s"+std::to_string(i)+" stick circle\n";
    check(refuses(many,"32 slots"),"more than 32 slots refused");
    check(refuses("game hct\n","no extra"),"no extra refused");
}

void test_plan(){
    const auto d=declaration(Truckful);
    const auto base=common();
    const auto devs=common_devices(base);
    // On the wheel the 31 already bind: no new device.
    auto p=plan_extras(base+extras(d,{"truckful.look.x = "+axis(2),wheel("truckful.look.y.neg = hat 0 18000"),wheel("truckful.look.y.pos = hat 0 0"),
                                     wheel("truckful.reset = button 9"),"Revision = eff-1"}),d,devs);
    check(p.error.empty()&&p.devices.empty()&&p.extras[0].base==Status::Opening&&p.extras[1].base==Status::Opening&&p.revision=="eff-1","bound on the common wheel: "+p.error);
    check(p.extras[0].component[0].form==ExtraComponent::Form::Single&&p.extras[0].component[1].form==ExtraComponent::Form::Pair&&
          p.extras[0].component[0].a.device==0,"X an axis, Y a hat pair, on device 0");
    // An extras-only device is appended after the common table.
    p=plan_extras(base+extras(d,{box("truckful.reset = button 4")}),d,devs);
    check(p.devices.size()==1&&p.devices[0].dev==B&&p.extras[1].component[0].a.device==int(devs.size()),"an extras-only device goes after the common table");
    // A prod conflict with the common table invalidates only the extra; the 31's plan is the same object.
    p=plan_extras(base+extras(d,{on(W,OtherP,"truckful.reset = button 4"),"truckful.look.x = "+axis(2)}),d,devs);
    check(p.extras[1].base==Status::Invalid&&p.extras[1].reason.find("differs from the common binding")!=std::string::npos&&
          p.extras[0].base==Status::Opening,"a common-device conflict invalidates the extra only: "+p.extras[1].reason);
    // Extras-only instances named with two products: every extra on that instance is invalid, whatever the order.
    const auto two=declaration("game truckful\ntruckful.a digital\ntruckful.b digital\ntruckful.c digital\n");
    const auto twob=declaration("game truckful\ntruckful.b digital\ntruckful.c digital\ntruckful.a digital\n");
    const std::vector<std::string> lines={box("truckful.a = button 1"),on(B,OtherP,"truckful.b = button 2"),wheel("truckful.c = button 5")};
    for(const auto* dd:{&two,&twob}){
        p=plan_extras(base+extras(*dd,lines),*dd,devs);
        int invalid=0,bound=0;
        for(std::size_t i=0;i<dd->extras.size();++i){
            if(p.extras[i].base==Status::Invalid&&p.extras[i].reason.find("conflicting products")!=std::string::npos)++invalid;
            if(p.extras[i].base==Status::Opening)++bound;
        }
        check(invalid==2&&bound==1&&p.devices.empty(),"conflicting extras-only products invalidate every extra on the instance, in either order");
    }
    // Grammar refusals invalidate that extra, never another.
    p=plan_extras(base+extras(d,{"truckful.look.x = "+axis(2),wheel("truckful.look.x.neg = button 1"),wheel("truckful.reset = button 9")}),d,devs);
    check(p.extras[0].base==Status::Invalid&&p.extras[0].reason.find("two representations")!=std::string::npos&&p.extras[1].base==Status::Opening,
          "an axis and a pair for one component invalidate that extra: "+p.extras[0].reason);
    p=plan_extras(base+extras(d,{wheel("truckful.reset = button 9"),wheel("truckful.reset = button 8")}),d,devs);
    check(p.extras[1].base==Status::Invalid&&p.extras[1].reason.find("duplicate")!=std::string::npos,"a duplicate key invalidates the extra");
    p=plan_extras(base+extras(d,{"truckful.look.z = "+axis(2),wheel("truckful.reset = button 9")}),d,devs);
    check(p.extras[0].base==Status::Invalid&&p.extras[1].base==Status::Opening,"an unknown component of a declared extra invalidates it");
    p=plan_extras(base+extras(d,{wheel("truckful.oldname = button 2"),wheel("truckful.reset = button 9")}),d,devs);
    check(p.ignored.size()==1&&p.ignored[0].find("truckful.oldname")==0&&p.extras[1].base==Status::Opening&&p.extras[0].base==Status::Unbound,
          "an undeclared id is ignored with a note");
    p=plan_extras(base+extras(d,{"truckful.reset = "+axis(2)}),d,devs);
    check(p.extras[1].base==Status::Invalid&&p.extras[1].reason.find("wrong-kind")!=std::string::npos,"a digital extra on an axis misfits");
    p=plan_extras(base+extras(d,{wheel("truckful.look.y.pos = hat 0 0"),"truckful.look.x ="}),d,devs);
    check(p.extras[0].base==Status::Opening&&p.extras[0].component[0].form==ExtraComponent::Form::Unbound&&
          p.extras[0].component[1].form==ExtraComponent::Form::Pair&&!p.extras[0].component[1].a.configured,"one-sided pair, X deliberately unbound");
    // Whole-section errors leave every extra unbound with the reason; the 31 are a separate plan.
    p=plan_extras(base,d,devs);
    check(p.error.find("no [Extras]")!=std::string::npos&&p.extras[0].base==Status::Unbound,"no section: unbound");
    p=plan_extras(base+extras(d,{wheel("truckful.reset = button 9")},"sonic"),d,devs);
    check(p.error.find("Game")!=std::string::npos&&p.extras[1].base==Status::Unbound,"another Game refused as a section");
    p=plan_extras(base+extras(d,{wheel("truckful.reset = button 9")},"truckful",false),d,devs);
    check(p.error.find("another declaration")!=std::string::npos,"a missing Declaration refused");
    p=plan_extras(base+extras(declaration("game truckful\ntruckful.reset digital\n"),{wheel("truckful.reset = button 9")}),d,devs);
    check(p.error.find("another declaration")!=std::string::npos,"another declaration's hash refused");
    p=plan_extras(base+"[Extras]\nSchema = 2\nGame = truckful\nDeclaration = "+hex(d.hash)+"\n",d,devs);
    check(p.error.find("Schema 2 is not supported")!=std::string::npos,"Schema 2 at reads 1 refused");
    p=plan_extras(base+extras(d,{})+"[Extras]\nSchema = 1\n",d,devs);
    check(p.error.find("second [Extras]")!=std::string::npos,"a second section refused");
    p=plan_extras(base+extras(d,{"Revision = a",wheel("truckful.reset = button 9"),"Revision = b"}),d,devs);
    check(p.error.find("second Revision")!=std::string::npos&&p.revision.empty()&&p.extras[1].base==Status::Unbound,
          "a second Revision refuses the section, as a second Schema, Game or Declaration does (deli 101880)");
    check(read_profile(base+extras(d,{wheel("truckful.reset = button 9")})).ok,"the profile reader ignores [Extras]");
}

struct Rig {
    fk::Provider p;
    std::unique_ptr<Core> core;
    std::size_t w=0,b=0;
    Rig(const std::string& profile,const Declaration* d,bool with_box=false){
        fk::Device x;x.listed={W,WP,"Wheel"};x.caps=fk::wheel_caps();w=p.add(x);
        if(with_box){fk::Device y;y.listed={B,BP,"Box"};y.caps=fk::wheel_caps();b=p.add(y);}
        CoreConfig cfg;cfg.missing_rescan_us=0;
        core=std::make_unique<Core>(p,cfg);
        if(d)core->set_profile(read_profile(profile),*d,profile);else core->set_profile(read_profile(profile));
    }
    const Frame& step(std::uint32_t contexts,std::int64_t us=10000){core->service_devices();p.advance(us);return core->tick(contexts);}
    const Frame& step_plain(std::int64_t us=10000){core->service_devices();p.advance(us);return core->tick();}
    fk::Device& dev(std::size_t i){return p.at(i);}
    const ExtrasFrame& x()const{return core->extras_frame();}
};
const ActionValue& act(const Frame& f,const char* id){return f.action[std::size_t(action_index(id))];}

void test_core_no_extras_unchanged(){
    // The same rig and the same 31: a Core with a declaration (its [Extras] binding nothing) and one without tick alike.
    const auto d=declaration(Truckful);
    const auto profile=common();
    Rig a(profile,nullptr),b(profile,&d);
    for(auto* r:{&a,&b}){auto& w=r->dev(r->w);w.state.axes[0]=49151;w.state.buttons[3]=0;}
    for(int n=0;n<4;++n){
        const auto& fa=a.step_plain();
        const auto& fb=b.step(1);
        bool same=fa.seq==fb.seq&&fa.down==fb.down&&fa.up==fb.up&&fa.samples.size()==fb.samples.size();
        for(int i=0;i<ActionCount;++i)same=same&&fa.action[std::size_t(i)].status==fb.action[std::size_t(i)].status&&fa.action[std::size_t(i)].value==fb.action[std::size_t(i)].value;
        check(same,"tick "+std::to_string(n)+": the 31 and the samples are identical with and without a declaration");
    }
    check(b.x().slot.size()==3&&b.x().slot[0].status==Status::Unbound&&b.x().seq==b.core->extras_frame().seq,"extras unbound without [Extras]");
    check(a.core->device_reports().size()==b.core->device_reports().size(),"the same device table");
}

void test_core_stick_and_pairs(){
    const auto d=declaration("game truckful\ntruckful.look stick circle\ntruckful.reset digital\n");
    const auto sq=declaration("game truckful\ntruckful.look stick square\ntruckful.reset digital\n");
    const std::vector<std::string> pairs={wheel("truckful.look.x.neg = button 10"),wheel("truckful.look.x.pos = button 11"),
                                          wheel("truckful.look.y.neg = button 12"),wheel("truckful.look.y.pos = button 13"),wheel("truckful.reset = button 9")};
    Rig c(common()+extras(d,pairs),&d),s(common()+extras(sq,pairs),&sq);
    for(auto* r:{&c,&s})r->step(0);
    for(auto* r:{&c,&s}){auto& w=r->dev(r->w);w.state.buttons[11]=0x80;w.state.buttons[13]=0x80;}
    c.step(0);s.step(0);
    check(c.x().slot[0].status==Status::Live&&std::fabs(c.x().slot[0].value-0.70710678f)<1e-4f&&std::fabs(c.x().slot[1].value-0.70710678f)<1e-4f,
          "circle: a diagonal from buttons is unit length");
    check(s.x().slot[0].value==1.f&&s.x().slot[1].value==1.f,"square: each component keeps its value");
    auto& w=c.dev(c.w);w.state.buttons[10]=0x80;   // left and right together
    c.step(0);
    check(c.x().slot[0].status==Status::Live&&c.x().slot[0].value==0.f&&c.x().slot[1].value==1.f,"opposite halves together: 0");
    // Edges: reset is digital, its slot is 2.
    w.state.buttons[9]=0x80;
    c.step(0);
    check(c.x().slot[2].value==1.f&&(c.x().down>>2&1)&&c.x().up==0,"a reset press is a down edge on its slot");
    c.step(0);
    check(!(c.x().down>>2&1),"held: no second edge");
    w.state.buttons[9]=0;
    c.step(0);
    check((c.x().up>>2&1)&&c.x().slot[2].value==0.f,"release: an up edge");
    // A stick from an axis, with Y unbound: X works.
    const auto one=declaration("game truckful\ntruckful.look stick circle\n");
    Rig a(common()+extras(one,{"truckful.look.x = "+axis(2),"truckful.look.y ="}),&one);
    a.dev(a.w).state.axes[2]=65535;
    a.step(0);a.step(0);
    check(a.x().slot[0].status==Status::Live&&a.x().slot[0].value==1.f&&a.x().slot[1].status==Status::Unbound,"an unbound component stays neutral, the other works");
    // One-sided pair.
    const auto c1=declaration("game truckful\ntruckful.lean centred\n");
    Rig o(common()+extras(c1,{wheel("truckful.lean.pos = button 6"),"truckful.lean.neg ="}),&c1);
    o.step(0);
    o.dev(o.w).state.buttons[6]=0x80;
    o.step(0);
    check(o.x().slot[0].status==Status::Live&&o.x().slot[0].value==1.f,"a deliberately one-sided pair works");
}

void test_core_unit_and_devices(){
    // X on the wheel, Y on the box: the box missing makes the whole stick neutral with the worst status.
    const auto d=declaration("game truckful\ntruckful.look stick circle\ntruckful.reset digital\n");
    const auto profile=common()+extras(d,{"truckful.look.x = "+axis(2),box("truckful.look.y.neg = button 1"),box("truckful.look.y.pos = button 2"),
                                          wheel("truckful.reset = button 9")});
    Rig r(profile,&d,true);
    r.dev(r.b).attached=false;
    r.dev(r.w).state.axes[2]=65535;
    r.step(0);r.step(0);
    check(r.x().slot[0].status==Status::DeviceMissing&&r.x().slot[0].value==0.f&&r.x().slot[1].status==Status::DeviceMissing,
          "a configured component unavailable: the stick is neutral as a unit");
    check(r.x().slot[2].status==Status::Live,"another extra is unaffected");
    check(r.core->device_reports().size()==2,"the extras-only box is in the device table, after the wheel");
    r.dev(r.b).attached=true;
    r.step(0);r.step(0);
    check(r.x().slot[0].status==Status::Live&&r.x().slot[0].value==1.f,"the box back: the stick is Live");
    // A held digital extra at an epoch's start waits for release, as the 31 do.
    const auto h=declaration("game truckful\ntruckful.reset digital\n");
    Rig e(common()+extras(h,{wheel("truckful.reset = button 9")}),&h);
    e.dev(e.w).state.buttons[9]=0x80;
    e.step(0);
    check(e.x().slot[0].status==Status::Rearming&&e.x().down==0,"held at the epoch start: rearming, no edge");
    e.dev(e.w).state.buttons[9]=0;e.step(0);e.dev(e.w).state.buttons[9]=0x80;e.step(0);
    check(e.x().slot[0].status==Status::Live&&(e.x().down&1),"released then pressed: Live with an edge");
    // A prod conflict on a common device: the 31 tick exactly as a Core without the extra.
    const auto c=declaration("game truckful\ntruckful.reset digital\n");
    Rig with(common()+extras(c,{on(W,OtherP,"truckful.reset = button 9")}),&c),without(common(),nullptr);
    for(auto* q:{&with,&without})q->dev(q->w).state.axes[0]=49151;
    with.step(0);without.step_plain();
    const auto& fw=with.step(0);const auto& fo=without.step_plain();
    check(act(fw,"steer").status==Status::Live&&act(fw,"steer").value==act(fo,"steer").value&&with.x().slot[0].status==Status::Invalid,
          "a conflicting extra never poisons the common binding");
}

void test_core_contexts(){
    const auto d=declaration(Truckful);
    const auto profile=common()+extras(d,{"truckful.look.x = "+axis(2),wheel("truckful.look.y.pos = button 13"),wheel("truckful.reset = button 9")});
    Rig r(profile,&d);
    auto& w=r.dev(r.w);
    r.step(1);
    w.state.buttons[9]=0x80;w.state.axes[2]=60000;
    r.step(1);
    check(r.x().slot[2].status==Status::Live&&r.x().slot[0].status==Status::Live&&r.x().contexts==1,"in context: Live");
    r.step(0);
    check(r.x().slot[2].status==Status::ContextInactive&&r.x().slot[2].value==0.f&&r.x().slot[0].status==Status::ContextInactive&&(r.x().up>>2&1),
          "out of context: ContextInactive, neutral, with the release edge");
    r.step(1);   // back in context with reset still held and the stick off centre
    check(r.x().slot[2].status==Status::Rearming&&r.x().down==0,"held across the change: rearming, no new press");
    check(r.x().slot[0].status==Status::Rearming,"an off-centre axis waits for centre on the return");
    w.state.buttons[9]=0;w.state.axes[2]=32768;
    r.step(1);
    w.state.buttons[9]=0x80;
    r.step(1);
    check(r.x().slot[2].status==Status::Live&&(r.x().down>>2&1)&&r.x().slot[0].status==Status::Live,"released, then a real press after the return");
    check(r.core->extras_report().find("\"acceptedSchema\":1")!=std::string::npos,"the report names the accepted [Extras] Schema: 1 by default");
    {
        fk::Provider p2;fk::Device x;x.listed={W,WP,"Wheel"};x.caps=fk::wheel_caps();p2.add(x);
        Core two(p2,CoreConfig{});
        two.set_profile(read_profile(profile,2),d,profile,2);
        check(two.extras_report().find("\"acceptedSchema\":2")!=std::string::npos,"a reads-2 Core reports 2");
    }
    // Advisory: Core never gates.
    const auto a=declaration("game truckful\ncontexts advisory driving\ntruckful.reset digital contexts=driving\n");
    Rig v(common()+extras(a,{wheel("truckful.reset = button 9")}),&a);
    v.step(0);
    check(v.x().slot[0].status==Status::Live,"advisory: never ContextInactive");
}
}

int main(){
    try{
        test_declaration();test_plan();test_core_no_extras_unchanged();test_core_stick_and_pairs();test_core_unit_and_devices();test_core_contexts();
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    std::cout<<"shim extras: "<<checks<<" checks passed\n";
    return 0;
}
