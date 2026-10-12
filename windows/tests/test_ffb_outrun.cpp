// OutRun's own force model (source/ffb_outrun, "outrun-legacy@1") against the OutRun 2006 mod's production calculation:
// the mod's offline fixture (dbce-mods-outrun-2006 tools/tests/signal_calculation_fixture.cpp) runs CalculateSignals on
// a numeric input and prints every force call; its goldens (tests/data/ffb/*.expected, README.md there) must
// reproduce byte for byte here. Plus the periodic envelopes, the contact edge, reset and the non-finite guards.
//     test_ffb_outrun <tests/data/ffb>
#include "ffb_outrun.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace outrun::desktop::ffb;
int checks=0;
void check(bool v,const std::string& what){++checks;if(!v)throw std::runtime_error(what);}

struct Frame { unsigned seq{},tick{}; CarSignals car; float roughness{}; std::uint32_t water{}; };
// The fixture's input schema: "dbce.outrun2006.calculation-input,1,legacy-defaults,60", then rows of
// sequence, tick, 1C4, 1D0, 1D4, 264, 268, gear, flags8, pedal34, roughness, water; then "complete,<n>".
std::vector<Frame> parse(const std::string& text){
    std::istringstream in(text);std::string line;std::vector<Frame> frames;
    if(!std::getline(in,line)||line!="dbce.outrun2006.calculation-input,1,legacy-defaults,60")throw std::runtime_error("input header");
    while(std::getline(in,line)){
        if(line.rfind("complete,",0)==0){if(line!="complete,"+std::to_string(frames.size()))throw std::runtime_error("footer");return frames;}
        std::vector<double> v;std::istringstream cells(line);std::string cell;
        while(std::getline(cells,cell,',')){std::istringstream n(cell);n.imbue(std::locale::classic());double d;n>>d;v.push_back(d);}
        if(v.size()!=12)throw std::runtime_error("columns");
        Frame f;f.seq=unsigned(v[0]);f.tick=unsigned(v[1]);
        f.car.speed_1c4=float(v[2]);f.car.steer_1d0=float(v[3]);f.car.steer_rate_1d4=float(v[4]);
        f.car.lateral_264=float(v[5]);f.car.lateral_268=float(v[6]);f.car.gear_208=unsigned(v[7]);
        f.car.state_8=unsigned(v[8]);f.car.pedal_34=unsigned(v[9]);f.roughness=float(v[10]);f.water=unsigned(v[11]);
        frames.push_back(f);
    }
    throw std::runtime_error("no footer");
}
// The fixture's Run(): its observation sinks and format, periodic slots 0 and 1.
std::string observe(const std::vector<Frame>& frames,const LegacySettings& settings,bool periodic){
    LegacyForce model(settings);model.set_periodics(periodic,0,1);model.reset();
    std::ostringstream o;o.imbue(std::locale::classic());unsigned tick=0;
    struct Obs:Sink {
        std::ostringstream& o;unsigned& tick;float scale;
        Obs(std::ostringstream& oo,unsigned& t,float s):o(oo),tick(t),scale(s){}
        void constant(std::int32_t value)override{
            value=std::clamp(value,std::int32_t(-10000),std::int32_t(10000));
            const long scaled=long(std::clamp(float(value)*scale,-10000.0f,10000.0f));
            o<<tick<<",constant,"<<scaled<<"\n";
        }
        void periodic(int slot,float magnitude,float hz)override{
            o<<tick<<",periodic,"<<slot<<","<<int(std::clamp(magnitude,0.0f,1.0f)*scale*10000)<<","<<int(std::clamp(hz,1.0f,100.0f)*1000)<<"\n";
        }
    } sink(o,tick,model.strength_scale());
    o<<"dbce.outrun2006.calculation-observation,1,observe,physicalOutput=false,legacy-defaults,60\n";
    for(const auto& f:frames){tick=f.tick;o<<"frame,"<<f.seq<<","<<tick<<"\n";model.calculate(f.car,f.roughness,f.water,sink);}
    o<<"complete,"<<frames.size()<<"\n";
    return o.str();
}
std::string read(const std::string& path){std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("cannot read "+path);
    std::stringstream s;s<<f.rdbuf();return s.str();}

// The mod's own synthetic case (tools/tests/Test-SignalCalculation.ps1), whose output is signal-legacy-v1.expected.
std::string synthetic_v1(){
    std::string t="dbce.outrun2006.calculation-input,1,legacy-defaults,60\n";
    for(int i=0;i<48;++i){
        const int gear=i<20?2:3,flags=(i>=32&&i<=36)?4096:0;const char* speed=i<32?"0.8":"0.5";
        t+=std::to_string(i)+","+std::to_string(i*17)+","+speed+",0.2,0.01,15,2,"+std::to_string(gear)+","+std::to_string(flags)+",180,0.8,1\n";
    }
    return t+"complete,48\n";
}

void test_goldens(const std::string& dir){
    const auto v1=parse(synthetic_v1());
    const auto mine=observe(v1,{},false);
    check(mine==read(dir+"/signal-legacy-v1.expected"),"the mod's golden signal-legacy-v1 reproduces byte for byte");
    check(mine==observe(v1,{},false),"deterministic, and reset() restores the start state");
    // Cases generated with the mod's fixture (README.md): <name>.csv in, <name>.expected out.
    std::ifstream list(dir+"/cases.txt");std::string name;int n=0;
    while(std::getline(list,name)){
        if(!name.empty()&&name.back()=='\r')name.pop_back();   // a CRLF checkout of cases.txt
        if(name.empty()||name[0]=='#')continue;
        const auto frames=parse(read(dir+"/"+name+".csv"));
        check(observe(frames,{},false)==read(dir+"/"+name+".expected"),"the mod's fixture output for "+name);
        ++n;
    }
    check(n>0,"cases.txt lists the generated cases");
}

void test_periodics(){
    // With hardware periodics: the envelopes go to slots 0 / 1 every 4th tick and leave the constant force smooth.
    std::vector<Frame> frames;
    for(unsigned i=0;i<16;++i){Frame f;f.seq=i;f.tick=i*17;f.car.speed_1c4=0.6f;f.car.lateral_264=30.0f;f.car.gear_208=3;f.roughness=0.5f;frames.push_back(f);}
    const auto out=observe(frames,{},true);
    int periodic=0;std::istringstream in(out);std::string line;
    while(std::getline(in,line))if(line.find(",periodic,")!=std::string::npos)++periodic;
    check(periodic==8,"two envelopes every 4th tick of 16");
    // Road: roughness 0.5 x speed 0.6 x RoadTexture 0.6 = 0.18 -> 1800; 25 + 12 x 0.6 = 32.2 Hz.
    check(out.find("51,periodic,0,1800,32200")!=std::string::npos,"road envelope = roughness x speed x RoadTexture, 25 + 12 x speed Hz");
    LegacyForce m;m.set_periodics(false,0,1);
    struct Count:Sink{int c=0;void constant(std::int32_t)override{++c;}void periodic(int,float,float)override{throw std::runtime_error("no periodics without them");}} k;
    for(const auto& f:frames)m.calculate(f.car,f.roughness,f.water,k);
    check(k.c>0,"without periodics the vibration rides the constant force");
}

void test_contact_edge(){
    // The contact flag's rising edge: a jolt for 10 ticks (5 full, 5 decaying), then an 80-tick cooldown in which another
    // edge changes nothing, then re-armed. After the jolt the force returns under the 600-per-tick slew limit.
    struct Seq:Sink{std::vector<std::int32_t> v;void constant(std::int32_t x)override{v.push_back(x);}void periodic(int,float,float)override{}};
    auto run=[](bool edge_in_cooldown,Seq& s){
        LegacyForce m;m.set_periodics(true,0,1);
        CarSignals c;c.speed_1c4=0.5f;c.gear_208=3;
        for(int i=0;i<200;++i){
            const bool onset=i>=40&&i<44,cooldown_edge=edge_in_cooldown&&i>=60&&i<62,rearmed=i>=140&&i<142;
            c.state_8=(onset||cooldown_edge||rearmed)?0x1000u:0u;
            m.calculate(c,0.0f,0,s);
        }
    };
    Seq with,without;run(true,with);run(false,without);
    check(with.v==without.v,"an edge inside the cooldown changes nothing");
    LegacyForce m;m.set_periodics(true,0,1);Seq s;CarSignals c;c.speed_1c4=0.5f;c.gear_208=3;
    for(int i=0;i<40;++i)m.calculate(c,0.0f,0,s);
    const auto quiet=s.v.size();c.state_8=0x1000;
    for(int i=0;i<10;++i)m.calculate(c,0.0f,0,s);
    check(s.v.size()==quiet+10,"every jolt tick is sent (the deadband is bypassed)");
    check(std::abs(s.v[quiet])>5000&&s.v[quiet]==s.v[quiet+4],"5 ticks at full strength");
    check(std::abs(s.v[quiet+9])<std::abs(s.v[quiet+4]),"then decaying");
    c.state_8=0;for(int i=0;i<100;++i)m.calculate(c,0.0f,0,s);
    check(std::abs(s.v.back())<100,"back near zero after the cooldown");
    c.state_8=0x1000;m.calculate(c,0.0f,0,s);
    check(std::abs(s.v.back())>5000,"re-armed: a new edge jolts again");
    m.reset();Seq t;CarSignals z;for(int i=0;i<5;++i)m.calculate(z,0.0f,0,t);
    check(t.v.empty()||std::abs(t.v.back())<100,"reset: a quiet, warmed-up start");
}

void test_guards(){
    LegacyForce m;m.set_periodics(false,0,1);
    struct Fin:Sink{bool ok=true;void constant(std::int32_t v)override{ok=ok&&v>=-10000&&v<=10000;}void periodic(int,float m,float h)override{ok=ok&&std::isfinite(m)&&std::isfinite(h);}} s;
    CarSignals c;c.speed_1c4=NAN;c.steer_1d0=INFINITY;c.steer_rate_1d4=-INFINITY;c.lateral_264=NAN;c.lateral_268=1e30f;c.gear_208=2;
    for(int i=0;i<50;++i)m.calculate(c,NAN,1,s);
    check(s.ok,"non-finite signals and roughness: bounded output, no NaN");
    CarSignals neg;neg.speed_1c4=-1.0f;neg.steer_1d0=-0.5f;neg.gear_208=1;
    for(int i=0;i<10;++i)m.calculate(neg,-0.5f,0,s);
    check(s.ok,"negative speed and roughness: bounded");
    check(surface_roughness(1,0,0,nullptr)==0.0,"asphalt (1): silent");
    std::uint32_t water=0;
    check(surface_roughness(2,0,0x29,&water)==0.72999996&&water==1u,"lake water (2) on a water stage: 0.73, water set");
    water=0;check(surface_roughness(2,1,0x29,&water)==0.25&&water==0u,"water with a load collision: 0.25, no water");
    check(surface_roughness(0x400000u,0,0x12,&water)==0.25&&surface_roughness(0x400000u,0,0x1,&water)==0.89999998,"0x400000 depends on the stage");
    check(surface_roughness(0x80,0,0,&water)==0.85000002&&surface_roughness(0x12345,0,0,&water)==0.31,"rough surface 0x80; an unknown mask 0.31");
}

int main(int argc,char** argv){
    try{
        if(argc!=2)throw std::runtime_error("usage: test_ffb_outrun <tests/data/ffb>");
        test_goldens(argv[1]);test_periodics();test_contact_edge();test_guards();
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    std::cout<<"outrun force model: "<<checks<<" checks passed\n";
    return 0;
}
