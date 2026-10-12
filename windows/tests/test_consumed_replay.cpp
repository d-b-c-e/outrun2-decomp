// Consumed playback (source/consumed_replay.hpp; sana 35159): the replay channels, the strict tape, the seam's rows then
// neutral, and the seam fixture: what the race update consumes from a replayed tick equals what it consumed from the
// recorded tick, for the keyboard table and all four joystick configurations (race_input.cpp 5A7B30 / 5A7B50).
#include "consumed_replay.hpp"
#include "platform/race_input.hpp"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
using namespace outrun::desktop;
namespace fs=std::filesystem;
namespace pf=outrun::platform;
namespace {
int checks=0;
void check(bool v,const std::string& what){++checks;if(!v){std::fprintf(stderr,"consumed replay: FAIL: %s\n",what.c_str());std::exit(1);}}
const fs::path Root=fs::temp_directory_path()/("outrun-consumed-replay-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
template<class F> bool throws(F f){try{f();return false;}catch(const std::exception&){return true;}}
// A recorded tick: a drive with every field the seam puts back set, varying by i.
Snapshot tick(std::uint64_t i){
    Snapshot s;s.frame.seq=100+i;s.frame.tick_us=1000000+std::int64_t(i)*16667;s.frame.boundary_us=s.frame.tick_us;   // decode: boundary >= tick
    s.signals.frame_seq=s.frame.seq;s.signals.mode=i<5?32u:16u;s.signals.in_gameplay=i>=5;s.view.mode=s.signals.mode;
    auto& f=s.consumed;s.has_consumed=true;
    f.steering=std::int32_t(i*7%255)-127;f.accelerator=std::uint32_t(i*13%256);f.brake=std::uint32_t(i*5%256);
    f.shift_up=i%3==0;f.shift_down=i%4==1;f.menu_left=i%5==0;f.menu_right=i%5==1;f.menu_up=i%5==2;f.menu_down=i%5==3;
    f.menu_confirm=i%7==0;f.menu_cancel=i%7==3;f.start=i%9==0;
    f.frontend.feature_mask=std::uint32_t(0x2004u+i);f.frontend.device_held=std::uint32_t(i&0xffu);
    for(std::size_t k=0;k<f.frontend.axes.size();++k)f.frontend.axes[k]=std::int16_t(int(i*31+k*1000)%30000-15000);
    f.connected=true;f.pc_input_layer=true;f.pc_record_used=i%2==0;f.pc_overlay=i%6==0;f.pc_overlay_yield=i%6==1;
    f.pc_device.buttons_04=std::uint32_t(0x2002u^(i*0x111u));
    for(const int a:{0x01,0x03,0x0c,0x0d,0x0e,0x17,0x18})f.pc_device.axes_94[std::size_t(a)]=std::int16_t(int(i*997+a*37)%60000-30000);
    s.pc_drive=i%3!=1;
    return s;
}
// The managed export's format (P21Finalizer --read-export): "ROW n", then the sorted channels with round-trip values.
void write_export(const fs::path& p,const std::vector<rec::Channels>& rows){
    std::ofstream o(p,std::ios::binary);
    for(const auto& c:rows){o<<"ROW "<<c.size()<<"\n";for(const auto& v:c){char b[64];std::snprintf(b,sizeof b,"%.17g",v.second);o<<v.first<<" "<<b<<"\n";}}
}
rec::Channels row_channels(const Snapshot& s){
    // The Core frame channels (one device slot) plus OutRun's, as the recorder writes a row.
    auto c=rec::encode(s.frame,1);for(const auto& v:game_channels(s))c[v.first]=v.second;return c;
}
}

int run(){
    fs::create_directories(Root);
    // 1. The replay channels: additive to @2, strict.
    {const auto s=tick(4);const auto c=game_channels(s);
     for(const char* k:{"final.pcDrive","final.pcAxis01","final.pcAxis03","final.pcAxis17","final.pcAxis18"})check(c.count(k)==1,std::string("channel ")+k);
     const auto r=decoded_replay(c);
     check(r.pc_drive==s.pc_drive&&r.input.pc_device.axes_94[0x17]==s.consumed.pc_device.axes_94[0x17]&&r.input.pc_device.axes_94[0x01]==s.consumed.pc_device.axes_94[0x01],
           "decoded_replay round-trips pcDrive and the extra axes");
     auto old=c;old.erase("final.pcDrive");
     check(throws([&]{(void)decoded_replay(old);}),"a row without final.pcDrive (before the replay seam) is refused");
     check(!throws([&]{(void)decoded_consumed(old);}),"decoded_consumed still reads such a row (analysis only)");
     auto two=c;two["final.pcDrive"]=2;check(throws([&]{(void)decoded_replay(two);}),"pcDrive 2 is refused");
     auto half=c;half["final.pcDrive"]=0.5;check(throws([&]{(void)decoded_replay(half);}),"pcDrive 0.5 is refused");
     auto axis=c;axis.erase("final.pcAxis18");check(throws([&]{(void)decoded_replay(axis);}),"a missing replay axis is refused");
     check(frame_channels(c)==frame_channels(game_channels(tick(4))),"the frame decoder still sees only the Core frame");}

    // 2. The tape: strict, consecutive frames.
    std::vector<Snapshot> recorded;std::vector<rec::Channels> rows;
    for(std::uint64_t i=0;i<40;++i){recorded.push_back(tick(i));rows.push_back(row_channels(recorded.back()));}
    const auto good=Root/"export.tsv";write_export(good,rows);
    ConsumedTape tape(good);
    check(tape.size()==40&&tape.row(0).seq==100&&tape.row(39).seq==139,"the tape reads every row, menus included");
    {auto gap=rows;gap.erase(gap.begin()+7);write_export(Root/"gap.tsv",gap);
     check(throws([&]{ConsumedTape t(Root/"gap.tsv");}),"a gap in the frames is refused");}
    {auto old=rows;for(auto& c:old)c.erase("final.pcDrive");write_export(Root/"old.tsv",old);
     check(throws([&]{ConsumedTape t(Root/"old.tsv");}),"a tape recorded before the replay seam is refused");}
    {write_export(Root/"empty.tsv",{});check(throws([&]{ConsumedTape t(Root/"empty.tsv");}),"an empty tape is refused");}

    // 3. The seam: row i at tick i, then neutral (connected and the PC layer kept, nothing else); a refused tape: neutral.
    {ConsumedReplay replay(std::make_unique<ConsumedTape>(good),"");
     check(replay.admitted()&&!replay.ended(),"admitted");
     for(std::size_t i=0;i<40;++i){
         pf::NativeInputState in{};in.steering=99;in.connected=true;in.pc_input_layer=true;bool drive=!recorded[i].pc_drive;
         replay.apply(in,drive);
         const auto& want=recorded[i].consumed;
         if(!(in.steering==want.steering&&in.accelerator==want.accelerator&&in.menu_confirm==want.menu_confirm&&in.frontend.feature_mask==want.frontend.feature_mask&&
              in.pc_device.buttons_04==want.pc_device.buttons_04&&in.pc_device.axes_94==want.pc_device.axes_94&&drive==recorded[i].pc_drive&&replay.last_row()==std::int64_t(i)))
             check(false,"tick "+std::to_string(i)+" plays row "+std::to_string(i));
     }
     check(replay.ended()&&replay.played()==40,"all rows played");
     pf::NativeInputState in{};in.steering=50;in.accelerator=200;in.menu_confirm=true;in.connected=true;in.pc_input_layer=true;bool drive=true;
     replay.apply(in,drive);
     check(in.steering==0&&in.accelerator==0&&!in.menu_confirm&&in.connected&&in.pc_input_layer&&!drive&&replay.last_row()==-1,"after the last row: neutral, never the live input");}
    {ConsumedReplay refused(nullptr,"a tape recorded before the replay seam");
     pf::NativeInputState in{};in.steering=50;in.start=true;in.connected=true;bool drive=true;
     refused.apply(in,drive);
     check(!refused.admitted()&&refused.ended()&&in.steering==0&&!in.start&&in.connected&&!drive,"a refused tape holds neutral from the first tick");}

    // 4. The seam fixture: the race update's two readers (453640 switches, 453860 analogue) over the recorded and the
    //    replayed ticks give the same records, for the keyboard table (pcDrive) and every joystick configuration.
    for(int config=-1;config<4;++config){
        ConsumedReplay replay(std::make_unique<ConsumedTape>(good),"");
        pf::PcInputSwitchRecord sw_a{},sw_b{};pf::PcInputAnalog an_a{},an_b{};float fa=0,fb=0;
        for(std::size_t i=0;i<40;++i){
            const auto& rec_in=recorded[i].consumed;const bool rec_drive=recorded[i].pc_drive;
            pf::NativeInputState in{};bool drive=false;replay.apply(in,drive);
            const auto cfg_a=config<0?(rec_drive?pf::pc_input_keyboard_config():pf::pc_input_joystick_config(0)):pf::pc_input_joystick_config(unsigned(config));
            const auto cfg_b=config<0?(drive?pf::pc_input_keyboard_config():pf::pc_input_joystick_config(0)):pf::pc_input_joystick_config(unsigned(config));
            const std::int32_t mode=std::int32_t(recorded[i].signals.mode);
            pf::pc_input_switch_453640(sw_a,rec_in.pc_device,cfg_a);pf::pc_input_analog_453860(an_a,fa,rec_in.pc_device,cfg_a,4,mode);
            pf::pc_input_switch_453640(sw_b,in.pc_device,cfg_b);pf::pc_input_analog_453860(an_b,fb,in.pc_device,cfg_b,4,mode);
            if(!(sw_a.held==sw_b.held&&sw_a.pressed==sw_b.pressed&&sw_a.released==sw_b.released&&an_a.current==an_b.current&&
                 an_a.accel_edge_1c==an_b.accel_edge_1c&&an_a.brake_edge_2c==an_b.brake_edge_2c&&fa==fb))
                check(false,"configuration "+std::to_string(config)+" tick "+std::to_string(i)+": the race update consumed the same input");
        }
        check(true,"configuration "+std::to_string(config)+": every tick consumed alike");
    }

    std::error_code ec;fs::remove_all(Root,ec);
    std::printf("consumed replay: %d checks passed\n",checks);
    return 0;
}
int main(){
    try{return run();}
    catch(const std::exception& e){std::fprintf(stderr,"consumed replay: FAIL: an exception after %d checks: %s\n",checks,e.what());return 1;}
}
