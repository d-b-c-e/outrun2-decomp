// Synthetic ordered catalog, file existence, input and PC script records only.
// Calls the production goal_script_486730, with a callback instead of an audio
// device/decoder. No retail files, game window, controller or hardware output.
#include "platform/cd_switcher.hpp"
#include "platform/race_goal_camera.hpp"
#include "../windows/source/cd_switcher.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <set>
using namespace outrun::platform;
namespace {
unsigned checks=0;
void check(bool ok,const char* message){++checks;if(!ok){std::fprintf(stderr,"CDSwitcher FAIL: %s\n",message);std::exit(1);}}
struct Player {
    bool fail{},loop{true};unsigned calls{};std::size_t index{};std::string path;
    static bool play(void* user,const CdTrack& track,std::size_t index,bool loop){
        auto& p=*static_cast<Player*>(user);++p.calls;p.index=index;p.path=track.path;p.loop=loop;return !p.fail;
    }
};
void catalog(){
    const auto c=parse_cd_tracks({"   "," # comment","; comment"," Z.ogg = Display / not a path ",
        "a.ogg=First=Second","Z.ogg=Duplicate","b.ogg="});
    check(c.error.empty()&&c.tracks.size()==4,"ordered duplicate catalog accepted");
    check(c.tracks[0].path=="Z.ogg"&&c.tracks[0].title=="Display / not a path","separate title and path; case and trim");
    check(c.tracks[1].path=="a.ogg"&&c.tracks[1].title=="First=Second"&&c.tracks[2].path=="Z.ogg","first equals, order and duplicate retained");
    check(c.tracks[3].title.empty(),"empty title permitted");
    check(!parse_cd_tracks({"=bad"}).error.empty(),"empty path refused");
    check(!parse_cd_tracks({"no equals"}).error.empty(),"malformed section refused atomically");
    check(parse_cd_tracks(std::vector<std::string>(256,"x.ogg=t")).tracks.size()==256,"256 entries accepted");
    check(parse_cd_tracks(std::vector<std::string>(257,"x.ogg=t")).tracks.empty(),"257 entries no partial catalog");
    check(!parse_cd_tracks({std::string(4096,'p')+"=t"}).error.empty(),"path bound");
    check(!parse_cd_tracks({"p="+std::string(513,'t')}).error.empty(),"title bound");
    check(!parse_cd_tracks(std::vector<std::string>(4097,"#")).error.empty(),"line bound");
    check(!parse_cd_tracks({std::string(1024*1024+1,' ')}).error.empty(),"section byte bound");
    check(!parse_cd_tracks({std::string("bad\0path=t",10)}).error.empty(),"embedded NUL refused");
}
void resolve(){
    std::set<std::string> files;
    auto exists=[&](const std::string& path){return files.count(path)!=0;};
    auto get=[&](MusicReplacementPolicy p){return resolve_cd_track("song.ogg","Sound",p,exists);};
    check(get({true,true}).path.empty(),"missing entry does not invent a fallback");
    files={"Sound/song.ogg","Sound/song.wav","Sound/song.flac"};
    check(get({false,false}).path=="Sound/song.ogg","disabled formats use original catalog Ogg");
    check(get({true,false}).path=="Sound/song.wav","enabled WAV");
    check(get({false,true}).path=="Sound/song.flac"&&get({true,true}).format==MusicFileFormat::Flac,"enabled FLAC wins");
    files.insert("song.ogg");
    check(get({true,true}).path=="song.ogg","explicit/current-directory base wins before Sound siblings");
    files.insert("song.wav");files.insert("song.flac");
    check(get({true,true}).path=="song.flac","same-stem replacement for explicit file");
    files={"absolute/SONG.FLAC"};
    check(resolve_cd_track("absolute/SONG.FLAC","Sound",{false,true},exists).path=="absolute/SONG.FLAC","explicit uppercase FLAC allowed");
    check(resolve_cd_track("absolute/SONG.FLAC","Sound",{},exists).path.empty(),"explicit FLAC cannot bypass opt-in");
    files={"song.wav"};
    check(resolve_cd_track("song.wav","Sound",{true,false},exists).format==MusicFileFormat::Wav,"explicit WAV");
    check(resolve_cd_track("song.wav","Sound",{},exists).path.empty(),"explicit WAV cannot bypass opt-in");
    files={"song.mp3"};check(resolve_cd_track("song.mp3","Sound",{true,true},exists).path.empty(),"unsupported format refused");
}
void edges(){
    std::uint64_t back=0,previous=0,bad=9;
    check(parse_cd_combo(" Back ",back)&&back==(1ull<<11),"Back mask");
    check(parse_cd_combo(" rS + bAcK ",previous)&&previous==(back|(1ull<<5)),"case-insensitive combo");
    check(!parse_cd_combo("RS+unknown",bad)&&bad==0,"invalid combo atomic refusal");
    check(!parse_cd_combo("",bad)&&!parse_cd_combo("Back+",bad)&&!parse_cd_combo("ls-diagonal",bad),"empty/trailing/unknown aliases refused");
    CdEdges e;auto step=[&](bool z,bool x,std::uint64_t pad=0,bool connected=true,bool block=false){return e.step(z,x,pad,connected,previous,back,block);};
    check(!step(false,false).next,"initial neutral arms");
    check(step(false,true).next&&!step(false,true).next&&!step(false,false).next,"X one press; hold and release no repeats");
    check(step(true,false).previous&&!step(true,false).previous,"Z one press");
    (void)step(false,false);(void)step(false,false,previous);
    const auto specific=step(false,false);check(specific.previous&&!specific.next,"overlapping pad releases: larger combination wins");
    (void)step(false,false,back);check(step(false,false).next&&!step(false,false).next,"pad release once");
    (void)step(false,false,back);check(!step(false,false,0,false).next,"disconnect is not a pad release");
    (void)step(false,true,0,true,true);check(!step(false,true).next&&!step(false,false).next&&step(false,true).next,"blocked/panel/focus held key must release before a fresh press");
    (void)step(false,false,0,true,true);(void)step(false,false);check(step(true,true).previous,"simultaneous keyboard previous retained");
    e={};(void)e.step(false,false,0,true,back,back,false);
    (void)e.step(false,false,back,true,back,back,false);const auto tie=e.step(false,false,0,true,back,back,false);
    check(!tie.previous&&tie.next,"equal pad specificity favors Next");
    e={};(void)e.step(false,false,1,true,previous,back,false);
    check(e.step(false,true,1,true,previous,back,false).next,"unrelated held accelerator button does not prevent arming");
}
void cursor(){
    Player p;CdSwitcher s{true,{{"a.ogg","A"},{"b.ogg","B"},{"c.ogg","C"}},0,&p,Player::play};
    check(s.change({true,false})&&s.cursor==2&&!p.loop,"previous wraps and loop=false");
    check(s.change({false,true})&&s.cursor==0,"next wraps");
    check(s.change({true,true})&&s.cursor==0&&p.calls==3,"keyboard previous then next: one final playback");
    check(s.select(999)&&s.cursor==0,"race index outside catalog clamps to zero");
    p.fail=true;check(!s.change({false,true})&&s.cursor==0,"failed entry preserves cursor");
    s.enabled=false;const auto n=p.calls;check(!s.change({false,true})&&p.calls==n,"off does not call audio");
    s.enabled=true;s.tracks.clear();check(!s.change({true,false})&&p.calls==n,"absent catalog does not call audio");
    s.tracks={{"a","title"}};s.play=nullptr;check(!s.select(0),"unbound host transparent");
}
void actual_pad_adapter(){
    using namespace outrun::ps5;using outrun::desktop::cd_switcher_pad_sample;
    DualSenseInput adapter;PadSample sample;sample.connected=true;
    // Actual production adapter output, not an idealized host-layout mask.
    struct Named { const char* name;std::uint32_t button; };
    static constexpr Named names[]={{"A",Cross},{"B",Circle},{"X",Square},{"Y",Triangle},
        {"LS",StickL},{"RS",StickR},{"LB",L1},{"RB",R1},{"LT",L2},{"RT",R2},
        {"Start",Options},{"Back",Share},{"Left",Left},{"Up",Up},{"Right",Right},{"Down",Down}};
    for(const auto& entry:names){
        sample.buttons=entry.button;const auto native=adapter.update(sample,16);
        std::uint64_t configured=0;check(parse_cd_combo(entry.name,configured),"adapter case binding parses");
        const auto normalized=cd_switcher_pad_sample(native);
        check(normalized.connected&&normalized.held==configured,"actual DualSense sample maps to configured combination");
        check(native.raw_buttons_held==entry.button,"normalization leaves native raw input unchanged");
    }
    std::uint64_t back=0,previous=0;(void)parse_cd_combo("Back",back);(void)parse_cd_combo("RS+Back",previous);
    CdEdges edges;
    auto step=[&](std::uint32_t buttons,bool connected=true,bool blocked=false,bool tie=false){
        sample.buttons=buttons;sample.connected=connected;
        auto native=adapter.update(sample,16);const auto pad=cd_switcher_pad_sample(native);
        native.connected=true; // a later wheel filter must not mask pad disconnect
        if(!connected)check(native.connected&&!pad.connected,"native-pad connection snapshot survives wheel connection");
        return edges.step(false,false,pad.held,pad.connected,tie?back:previous,back,blocked);
    };
    (void)step(0);check(!step(Share).next,"actual Back press waits for release");
    check(!step(Share).next,"actual Back hold does not repeat");
    const auto next=step(0);check(next.next&&!next.previous,"actual Share/Back release selects Next");
    (void)step(Share|StickR);const auto prev=step(0);
    check(prev.previous&&!prev.next,"actual RS+Back release wins specificity");
    (void)step(Up|L1);const auto wrong=step(0);
    check(!wrong.previous&&!wrong.next,"PS5 Up/L1 never masquerade as configured Back/RS");
    (void)step(Share);const auto disconnected=step(0,false);
    check(!disconnected.previous&&!disconnected.next,"actual adapter disconnect ignored even if wheel later connects");
    (void)step(0);(void)step(Share,true,false,true);const auto tied=step(0,true,false,true);
    check(tied.next&&!tied.previous,"actual adapter equal combinations favor Next");
    (void)step(Share,true,true);check(!step(Share).next&&!step(0).next,"actual held pad crossing race/panel boundary is disarmed");
    (void)step(Share);check(step(0).next,"actual fresh pad press/release after boundary works");
}
void analog_pad_adapter(){
    using namespace outrun::ps5;using outrun::desktop::cd_switcher_pad_sample;
    struct Direction {
        const char* name;std::int32_t NativeInputState::* axis;std::uint8_t PadSample::* raw;
        int sign,deadzone;std::uint8_t extreme;
    };
    const Direction directions[]={
        {"ls-up",&NativeInputState::left_stick_y,&PadSample::left_y,1,7849,0},
        {"ls-down",&NativeInputState::left_stick_y,&PadSample::left_y,-1,7849,255},
        {"ls-left",&NativeInputState::left_stick_x,&PadSample::left_x,-1,7849,0},
        {"ls-right",&NativeInputState::left_stick_x,&PadSample::left_x,1,7849,255},
        {"rs-up",&NativeInputState::right_stick_y,&PadSample::right_y,1,8689,0},
        {"rs-down",&NativeInputState::right_stick_y,&PadSample::right_y,-1,8689,255},
        {"rs-left",&NativeInputState::right_stick_x,&PadSample::right_x,-1,8689,0},
        {"rs-right",&NativeInputState::right_stick_x,&PadSample::right_x,1,8689,255}};
    std::uint64_t back{},seen{};check(parse_cd_combo("Back",back),"analog modifier Back parses");
    for(const auto& direction:directions){
        std::uint64_t bit{},combo{};
        check(parse_cd_combo(direction.name,bit)&&parse_cd_combo(std::string(direction.name)+" + BACK",combo)&&
              combo==(bit|back)&&cd_bit_count(bit)==1u&&!(seen&bit)&&!(bit&0xffffu),
              "all eight direction aliases are distinct switcher-local bits and accept digital modifiers");
        seen|=bit;
        NativeInputState boundary{};boundary.connected=true;
        for(const int offset:{-1,0,1}){
            boundary.*direction.axis=direction.sign*(direction.deadzone+offset);
            check(cd_switcher_pad_sample(boundary).held==(offset>0?bit:0u),
                  "Tweaks strict signed threshold: exact deadzone and inside are neutral, one past is held");
        }
        DualSenseInput adapter;PadSample sample;sample.connected=true;
        auto read=[&](){return cd_switcher_pad_sample(adapter.update(sample,16));};
        const auto neutral=read();check(neutral.connected&&neutral.held==0,"actual neutral stick sample arms");
        sample.*direction.raw=direction.extreme;
        const auto native=adapter.update(sample,16);const auto held=cd_switcher_pad_sample(native);
        check(held.held==bit&&native.raw_buttons_held==0&&(native.*direction.axis)*direction.sign>direction.deadzone,
              "actual DualSense axis sign selects only its direction without synthesizing native button bits");
        sample.buttons=Share;auto pad=read();check(pad.held==combo,"actual stick plus Back combination");
        CdEdges edges;Player player;
        CdSwitcher switcher{true,{{"a.ogg","A"},{"b.ogg","B"}},0,&player,Player::play};
        auto step=[&](CdPadSample p,bool blocked=false){
            return edges.step(false,false,p.held,p.connected,combo,back,blocked);
        };
        (void)step(neutral);check(!switcher.change(step(pad)),"stick combo waits for release");
        check(!switcher.change(step(pad)),"held stick combo does not repeat");
        sample.buttons=0;sample.*direction.raw=128;pad=read();
        const auto release=step(pad);
        check(release.previous&&!release.next&&switcher.change(release)&&switcher.cursor==1&&player.calls==1&&!player.loop,
              "actual stick and digital release favors the more specific combo and plays one wrapped selection");
        check(!switcher.change(step(pad))&&player.calls==1,"neutral after release does not replay");
        sample.buttons=Share;sample.*direction.raw=direction.extreme;pad=read();(void)step(pad);
        sample.connected=false;pad=read();
        const auto unplug=step(pad);check(!unplug.previous&&!unplug.next&&pad.held==0,"stick disconnect is not a release");
        sample.connected=true;pad=read();(void)step(pad,true);
        check(!switcher.change(step(pad)),"held stick crossing race/panel boundary remains disarmed");
        sample.buttons=0;sample.*direction.raw=128;pad=read();
        check(!switcher.change(step(pad)),"boundary release only rearms");
        sample.buttons=Share;sample.*direction.raw=direction.extreme;pad=read();(void)step(pad);
        sample.*direction.raw=128;pad=read();const auto axis_release=step(pad);
        check(axis_release.previous&&!axis_release.next,"stick leaving combo alone releases while Back remains held");
    }
    NativeInputState disconnected{};disconnected.raw_buttons_held=Share;disconnected.left_stick_x=32767;
    check(cd_switcher_pad_sample(disconnected).held==0,"stale axes and buttons on a disconnected record are ignored");
    NativeInputState diagonal{};diagonal.connected=true;diagonal.left_stick_x=7850;diagonal.left_stick_y=7850;
    check(cd_switcher_pad_sample(diagonal).held==(CdLsRight|CdLsUp),"diagonal axes use independent thresholds");
    std::uint64_t previous{};(void)parse_cd_combo("RS+Back",previous);CdEdges defaults;
    (void)defaults.step(false,false,CdLsRight,true,previous,back,false);
    (void)defaults.step(false,false,CdLsRight|back,true,previous,back,false);
    const auto stock=defaults.step(false,false,CdLsRight,true,previous,back,false);
    check(stock.next&&!stock.previous,"unrelated held steering direction preserves default Back release selection");
    std::uint64_t mixed{};check(parse_cd_combo(" lS-Up + rS-rIgHt ",mixed)&&cd_bit_count(mixed)==2u,
                              "mixed-case directions from both sticks combine");
}
struct Script {
    PcRaceMemory memory;std::array<std::uint8_t,0x58> records{};
    std::array<std::uint8_t,4> table{},camera{},car{},mode{};std::array<std::uint8_t,0x30> scene{};
    outrun::driving::PcMatrixStack matrices{{nullptr,0},0,0,0};std::vector<PcRaceCall> calls;
    Script(unsigned op,unsigned game_mode,bool map_mode=true){
        memory.map(0x653788,table.data(),table.size());memory.put32(0x653788,0x100000);
        memory.map(0x100000,records.data(),records.size());memory.put32(0x100000,op);
        memory.put32(0x100004,7);memory.put32(0x100008,1);memory.put32(0x10000c,1);
        memory.put16(0x100024,0);memory.put32(0x100028,1);memory.put16(0x100050,0xffff);
        memory.map(0x82e7c0,scene.data(),scene.size());memory.map(0x79f574,camera.data(),camera.size());
        memory.map(0x799d18,car.data(),car.size());
        if(map_mode){memory.map(0x78026c,mode.data(),mode.size());memory.put32(0x78026c,game_mode);}
    }
    void run(){
        PcRaceContext context{memory,matrices,[&](const PcRaceCall& call){calls.push_back(call);return call.pc==0x48b1e0u?1u:0u;}};
        goal_script_486730(context);
    }
    unsigned count(unsigned pc)const{unsigned n=0;for(const auto& c:calls)if(c.pc==pc)++n;return n;}
};
void race(){
    Player p;auto& s=cd_switcher();s={false,{{"a.ogg","A"},{"b.ogg","B"}},0,&p,Player::play};
    // Off reads no new global: this synthetic context intentionally has no mode mapping.
    for(unsigned op:{1u,4u}){Script world(op,16,false);world.run();check(world.count(0x401000)==1&&p.calls==0,"both off call sites retain original without reading mode");
        for(const auto& call:world.calls)if(call.pc==0x401000)check(call.argc==3&&call.args[0]==7&&call.args[1]==1&&call.args[2]==1,"original channel/track/loop preserved exactly");}
    s.enabled=true;
    for(unsigned m:{13u,18u,19u,20u,32u,34u,35u})for(unsigned op:{1u,4u}){
        Script world(op,m);world.run();check(world.count(0x401000)==1&&p.calls==0,"outside GAME retains caller channel/track/loop path");
    }
    for(unsigned op:{1u,4u}){Script world(op,16);world.run();check(world.count(0x401000)==0&&p.index==1&&!p.loop,"both actual race script boundaries select catalog with loop=false");
        check(world.count(0x48b1e0)==(op==4?2u:0u),"selection and volume accessors preserved");}
    p.fail=true;const auto old=s.cursor;Script failed(1,16);failed.run();
    check(failed.count(0x401000)==1&&s.cursor==old,"failed race entry falls through to original call");
    const auto n=p.calls;Script stop(3,16);stop.run();check(stop.count(0x401030)==1&&p.calls==n,"stop case never routed to catalog");
    s={};
}
}
int main(){catalog();resolve();edges();cursor();actual_pad_adapter();analog_pad_adapter();race();std::printf("CDSwitcher: %u checks PASS (synthetic behavior only)\n",checks);}
