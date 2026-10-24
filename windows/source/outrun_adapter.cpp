// The OutRun 2006 input-shim adapter (INPUT-SHIM.md section 15). Pure: no Windows headers, no runtime access.
#include "outrun_adapter.hpp"
#include <algorithm>
#include <cmath>
namespace outrun::desktop {
namespace {
int A(const char* id){return shim::action_index(id);}
bool held(const shim::Frame& f,const char* id){return f.action[std::size_t(A(id))].value>=0.5f;}
bool down(const shim::Frame& f,const char* id){return ((f.down>>A(id))&1u)!=0;}
bool live(const shim::Frame& f,const char* id){return f.action[std::size_t(A(id))].status==shim::Status::Live;}
float value(const shim::Frame& f,const char* id){return f.action[std::size_t(A(id))].value;}

// Clears the pad's gameplay contribution, keeping the stop path (exit_requested) and the connection state. Returns how
// many fields were non-neutral, so a test run can report native input it suppressed.
std::uint32_t suppress_native(platform::NativeInputState& o){
    std::uint32_t n=0;
    auto c=[&](bool nonneutral){if(nonneutral)++n;};
    c(o.steering!=0);c(o.accelerator!=0);c(o.brake!=0);
    c(o.shift_up);c(o.shift_down);c(o.menu_left);c(o.menu_right);c(o.menu_up);c(o.menu_down);
    c(o.menu_confirm);c(o.menu_cancel);c(o.menu_preview);c(o.menu_start_probe);c(o.start);
    c(o.left_stick_x!=0||o.left_stick_y!=0||o.right_stick_x!=0||o.right_stick_y!=0);
    c(o.frontend.feature_mask!=0);c(o.frontend.device_held!=0);
    c(std::any_of(o.frontend.axes.begin(),o.frontend.axes.end(),[](std::int16_t v){return v!=0;}));
    c(o.pc_device.buttons_04!=0);
    c(std::any_of(o.pc_device.axes_94.begin(),o.pc_device.axes_94.end(),[](std::int16_t v){return v!=0;}));
    c(std::any_of(o.pc_pad.axes.begin(),o.pc_pad.axes.end(),[](std::int32_t v){return v!=0;})||o.pc_pad.pov!=0xffffffffu||
      std::any_of(o.pc_pad.buttons.begin(),o.pc_pad.buttons.end(),[](std::uint8_t v){return v!=0;}));
    const bool exit=o.exit_requested,connected=o.connected;
    const auto raw_held=o.raw_buttons_held,raw_down=o.raw_buttons_down,raw_up=o.raw_buttons_up;
    o=platform::NativeInputState{};
    o.exit_requested=exit;o.connected=connected;
    // The raw pad bits stay for the host's own stop handling (they never reach the game's controls).
    o.raw_buttons_held=raw_held;o.raw_buttons_down=raw_down;o.raw_buttons_up=raw_up;
    return n;
}
}

shim::Capability outrun_capability(){
    shim::Capability c;
    auto no=[&](const char* id,const char* why){c.unsupported[std::size_t(A(id))]=why;};
    no("clutch","OutRun 2006 has no clutch");
    no("handbrake","OutRun 2006 has no handbrake");
    no("neutral","OutRun 2006 has no neutral gear");
    no("reverse","OutRun 2006 has no reverse gear");
    no("gear7","the H-pattern mapping covers gears 1-6");
    no("gear8","the H-pattern mapping covers gears 1-6");
    no("select","no game action");
    no("lookBack","not mapped yet");
    no("reset","not mapped yet");
    no("modMenu","no F6 panel yet");
    no("muteFfb","no force feedback yet");
    return c;
}

// ---------------------------------------------------------------- H-pattern
void HPattern::set(State s,const std::string& why){if(s!=state_||why!=status_){state_=s;status_=why;}}
void HPattern::reset(){state_=State::Idle;status_.clear();target_=0;direction_=0;tries_=0;deadline_us_=0;gear_at_pulse_=0;}

int HPattern::step(const bool gates[6],bool race,bool car,std::uint32_t gear,std::int64_t now){
    const bool just_pulsed=pulsed_last_;
    pulsed_last_=false;
    int count=0;std::uint32_t target=0;
    for(int k=0;k<6;++k)if(gates[k]){++count;target=std::uint32_t(k+1);}
    if(count==0){if(state_!=State::Idle)reset();return 0;}
    if(count>1){target_=0;set(State::Ambiguous,"ambiguous: "+std::to_string(count)+" gates held");return 0;}
    if(target!=target_){reset();target_=target;}   // a new gate restarts the sequence and its retries
    if(!race||!car){set(State::Unavailable,"gear unavailable (not racing)");return 0;}
    // A missing or corrupt read is never a gear: no pulse is sent on it (slice 2a review 1).
    if(gear<1||gear>6){set(State::Unavailable,"gear word "+std::to_string(gear)+" is not a gear 1-6");return 0;}
    if(state_==State::Stopped)return 0;            // logged once; only a gate change restarts
    switch(state_){
    case State::Pulse:
        set(State::Release,"released after a shift pulse");   // at least one released tick
        return 0;
    case State::Release:
        set(State::Wait,"waiting for the gear to change");
        [[fallthrough]];
    case State::Wait:
        if(gear!=gear_at_pulse_){tries_=0;break;}   // the consumed gear moved: next step, if any
        if(now<deadline_us_)return 0;
        if(++tries_>cfg_.retries){
            set(State::Stopped,"gear target not reached (gear "+std::to_string(gear)+", target "+std::to_string(target_)+")");
            return 0;
        }
        break;                                      // retry the same step
    default:break;
    }
    if(gear==target_){set(State::AtTarget,"at gear "+std::to_string(gear));return 0;}
    // The tick after any pulse is released, even when the gate or the direction changed meanwhile (slice 2a review 2).
    // The state is left as it is, so the pulse goes out on the next tick.
    if(just_pulsed)return 0;
    direction_=gear<target_?1:-1;
    gear_at_pulse_=gear;
    deadline_us_=now+cfg_.deadline_us;
    set(State::Pulse,std::string("shift ")+(direction_>0?"up":"down")+" toward gear "+std::to_string(target_));
    pulsed_last_=true;
    return direction_;
}

// ---------------------------------------------------------------- the sink
Adapter::Adapter(std::string transmission,HPattern::Config gearbox):hpattern_(transmission=="H-pattern"),gearbox_(gearbox){}

void Adapter::apply(const shim::Frame& f,const GameView& g,platform::NativeInputState& out){
    const bool race=g.mode==16u||g.mode==18u;
    signals_=Signals{};
    signals_.frame_seq=f.seq;signals_.mode=g.mode;signals_.in_gameplay=race;signals_.speed=g.speed;signals_.gear=g.gear;
    signals_.consumed_steer=g.consumed_steer;signals_.consumed_accel=g.consumed_accel;signals_.consumed_brake=g.consumed_brake;
    if(f.suppress_native)signals_.native_suppressed=suppress_native(out);
    // A scene change resets what the adapter derived itself: no shift sequence is carried into another mode, and the
    // change's own tick sends no pulse, so a pulse is always followed by a released tick.
    const bool scene_change=g.mode!=last_mode_;
    if(scene_change){gearbox_.reset();gearbox_.released_tick();pulse_prev_=0;last_mode_=g.mode;}

    auto& d=out.pc_device;
    const bool steer=live(f,"steer"),throttle=live(f,"throttle"),brake=live(f,"brake");
    // The record carries the wheel (5A7B50[0]: steer word 0x0E, accelerator 0x0D, brake 0x0C). In a latched process it
    // always does, so the game reads the (neutral) owned values rather than a suppressed pad through the PC layer.
    if(steer||throttle||brake||f.suppress_native)out.pc_input_layer=false;
    if(steer){
        const auto w=std::int32_t(std::lround(double(value(f,"steer"))*32767.0));
        d.axes_94[0x0e]=std::int16_t(w);
        out.steering=std::clamp(w/256,-128,127);
    }else if(f.suppress_native)d.axes_94[0x0e]=0;
    auto pedal=[&](bool on,const char* id,std::uint32_t& native,std::size_t word){
        if(!on)return;
        const auto v=std::uint32_t(std::lround(double(value(f,id))*255.0));
        native=std::max(native,v);
        d.axes_94[word]=std::int16_t(std::max<std::uint32_t>(std::uint16_t(d.axes_94[word]),v));
    };
    pedal(throttle,"throttle",out.accelerator,0x0d);
    pedal(brake,"brake",out.brake,0x0c);

    // H-pattern gates (held, from the core) into shift pulses toward the gate.
    std::uint32_t pulse=0;
    if(hpattern_){
        bool gates[6];
        static constexpr const char* G[6]{"gear1","gear2","gear3","gear4","gear5","gear6"};
        for(int k=0;k<6;++k)gates[k]=held(f,G[k]);
        const int dir=scene_change?0:gearbox_.step(gates,race,g.car,g.gear,f.boundary_us);
        if(dir>0)pulse=0x2u;else if(dir<0)pulse=0x8u;
    }
    signals_.gearbox=gearbox_.state();
    const std::uint32_t pulse_down=pulse&~pulse_prev_;
    pulse_prev_=pulse;

    // Race: held bits. Outside the race, confirm answers the result prompts (0x2) and left / right are the D-pad bits
    // (0x80 / 0x100), which would be digital steering in the race.
    if(held(f,"start"))d.buttons_04|=0x1u;
    if(held(f,"shiftUp"))d.buttons_04|=0x2u;
    if(held(f,"back"))d.buttons_04|=0x4u;
    // The game's B button is horn in a race; preserve Back's existing B mapping for imported Tweaks profiles.
    if(race&&held(f,"horn"))d.buttons_04|=0x4u;
    if(held(f,"shiftDown"))d.buttons_04|=0x8u;
    if(held(f,"camera"))d.buttons_04|=0x10u;
    d.buttons_04|=pulse;
    if(!race){
        if(held(f,"confirm"))d.buttons_04|=0x2u;
        if(held(f,"navRight"))d.buttons_04|=0x80u;
        if(held(f,"navLeft"))d.buttons_04|=0x100u;
    }
    // Edges: the core's for actions; the gearbox's own for its pulses.
    out.shift_up|=down(f,"shiftUp")||(pulse_down&0x2u);
    out.shift_down|=down(f,"shiftDown")||(pulse_down&0x8u);
    out.menu_up|=down(f,"navUp");out.menu_down|=down(f,"navDown");
    out.menu_left|=down(f,"navLeft");out.menu_right|=down(f,"navRight");
    out.menu_confirm|=down(f,"confirm")||down(f,"start");out.menu_cancel|=down(f,"back");
    out.start|=down(f,"start");
    // Frontend features (48F5F0), as the pad adapter sets them from its button edges.
    auto& m=out.frontend.feature_mask;
    if(down(f,"start"))m|=1u;
    if(down(f,"confirm"))m|=4u;
    if(down(f,"back"))m|=8u;
    if(down(f,"navUp"))m|=0x400u;
    if(down(f,"navDown"))m|=0x800u;
    if(down(f,"navLeft"))m|=0x1000u;
    if(down(f,"navRight"))m|=0x2000u;
    if(!race){if(down(f,"shiftDown"))m|=0x10u;if(down(f,"shiftUp"))m|=0x20u;}
    if(down(f,"camera"))m|=0x40000u;
    if(d.buttons_04||steer||throttle||brake)out.connected=true;

    signals_.record_steer=d.axes_94[0x0e];
    signals_.record_accel=std::uint16_t(d.axes_94[0x0d]);
    signals_.record_brake=std::uint16_t(d.axes_94[0x0c]);
    signals_.record_buttons=d.buttons_04;
}

void Adapter::apply_neutral(bool latched,platform::NativeInputState& out){
    if(latched)signals_.native_suppressed=suppress_native(out);
    gearbox_.reset();
    pulse_prev_=0;
    // Nothing owned is added: the wheel's record words and bits are simply not written, so the game sees the pad alone
    // (ordinary) or nothing (latched).
    signals_.record_steer=out.pc_device.axes_94[0x0e];
    signals_.record_accel=std::uint16_t(out.pc_device.axes_94[0x0d]);
    signals_.record_brake=std::uint16_t(out.pc_device.axes_94[0x0c]);
    signals_.record_buttons=out.pc_device.buttons_04;
}

int select_pad(const std::vector<PadIdentity>& pads,const std::vector<std::string>& owned){
    for(std::size_t i=0;i<pads.size();++i){
        const std::uint32_t id=std::uint32_t(pads[i].pid)<<16|pads[i].vid;
        bool mine=false;
        for(const auto& prod:owned){
            dbce::controls::Binding b;b.prod=prod;
            if(dbce::controls::productId(b)==id)mine=true;
        }
        if(!mine)return int(i);
    }
    return -1;
}
}
