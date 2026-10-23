// The OutRun 2006 input-shim adapter (INPUT-SHIM.md section 15). Pure: no Windows headers, no runtime access.
#include "outrun_adapter.hpp"
#include "outrun_actions.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
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

// The unavailable actions and their reasons: outrun_actions.hpp, one table with the controls report.
shim::Capability outrun_capability(){
    shim::Capability c;
    for(const auto& u:UnavailableActions)c.unsupported[std::size_t(A(u.id))]=u.why;
    return c;
}

// ---------------------------------------------------------------- H-pattern
void HPattern::set(State s,const std::string& why){if(s!=state_||why!=status_){state_=s;status_=why;}}
void HPattern::reset(){state_=State::Idle;status_.clear();target_=0;direction_=0;tries_=0;deadline_us_=0;gear_at_pulse_=0;}

int HPattern::step_mode(const bool gates[6],std::uint32_t mode,bool car,std::uint32_t gear,std::int64_t now){
    if(mode!=mode_){reset();released_tick();mode_=mode;return 0;}
    if(mode==18u){released_tick();return 0;}
    return step(gates,mode==16u,car,gear,now);
}

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
Adapter::Adapter(std::string transmission,HPattern::Config gearbox,Steering steering)
    :hpattern_(transmission=="H-pattern"),gearbox_(gearbox),steering_(steering){}

void Adapter::apply(const shim::Frame& f,const GameView& g,platform::NativeInputState& out,const Keyboard* keyboard){
    const bool race=g.mode==16u||g.mode==18u;
    // Ordinary play only: a latched frame (suppress_native) never takes the keyboard.
    const Keyboard* kb=f.suppress_native?nullptr:keyboard;
    signals_=Signals{};
    signals_.frame_seq=f.seq;signals_.mode=g.mode;signals_.in_gameplay=race;signals_.speed=g.speed;signals_.gear=g.gear;
    signals_.consumed_steer=g.consumed_steer;signals_.consumed_accel=g.consumed_accel;signals_.consumed_brake=g.consumed_brake;
    if(f.suppress_native)signals_.native_suppressed=suppress_native(out);
    // A scene change resets what the adapter derived itself: no shift sequence is carried into another mode, and the
    // change's own tick sends no pulse, so a pulse is always followed by a released tick.
    const bool scene_change=g.mode!=last_mode_;
    if(scene_change){pulse_prev_=0;last_mode_=g.mode;}

    auto& d=out.pc_device;
    const bool steer=live(f,"steer"),throttle=live(f,"throttle"),brake=live(f,"brake");
    // The record carries the wheel (5A7B50[0]: steer word 0x0E, accelerator 0x0D, brake 0x0C). In a latched process it
    // always does, so the game reads the (neutral) owned values rather than a suppressed pad through the PC layer.
    // Ordinary play with the keyboard: the PC layer stays on for the keyboard (a padless desktop: nothing else sets it,
    // src/input/dual_sense.cpp returns first without a pad), and the yield below decides which record the race reads.
    if(kb)out.pc_input_layer=true;
    else if(steer||throttle||brake||f.suppress_native)out.pc_input_layer=false;
    std::int32_t steer_word=0;
    if(steer){
        double n=double(value(f,"steer"));
        const double zone=steering_.dead_zone;
        if(zone>0.0)n=std::fabs(n)<zone?0.0:(n>0?1.0:-1.0)*(std::fabs(n)-zone)/(1.0-zone);
        n=std::clamp(n*double(steering_.sensitivity),-1.0,1.0);
        steer_word=std::int32_t(std::lround(n*32767.0));
        d.axes_94[0x0e]=std::int16_t(steer_word);
        out.steering=std::clamp(steer_word/256,-128,127);
    }else if(f.suppress_native)d.axes_94[0x0e]=0;
    std::uint32_t wheel_accel=0,wheel_brake=0;
    auto pedal=[&](bool on,const char* id,std::uint32_t& native,std::size_t word,std::uint32_t& own){
        if(!on)return;
        const auto v=std::uint32_t(std::lround(double(value(f,id))*255.0));
        own=v;native=std::max(native,v);
        d.axes_94[word]=std::int16_t(std::max<std::uint32_t>(std::uint16_t(d.axes_94[word]),v));
    };
    pedal(throttle,"throttle",out.accelerator,0x0d,wheel_accel);
    pedal(brake,"brake",out.brake,0x0c,wheel_brake);
    // Ordinary play: the menu keys, outside the race only (in the race the keyboard is the PC layer's own), merged
    // with the wheel's actions as wheel_input.cpp merges them: held for the bits, their own edges for the menus.
    const std::uint32_t KEnter=1u,KEsc=2u,KUp=4u,KDown=8u,KLeft=16u,KRight=32u;
    std::uint32_t keys=0;
    if(kb&&!race)keys=(kb->enter?KEnter:0u)|(kb->esc?KEsc:0u)|(kb->up?KUp:0u)|(kb->down?KDown:0u)|(kb->left?KLeft:0u)|(kb->right?KRight:0u);
    const std::uint32_t key_down=keys&~keys_prev_;
    keys_prev_=keys;
    auto held_k=[&](const char* id,std::uint32_t key){return held(f,id)||(keys&key)!=0u;};
    auto down_k=[&](const char* id,std::uint32_t key){return down(f,id)||(key_down&key)!=0u;};

    // H-pattern gates (held, from the core) into shift pulses toward the gate.
    std::uint32_t pulse=0;
    if(hpattern_){
        bool gates[6];
        static constexpr const char* G[6]{"gear1","gear2","gear3","gear4","gear5","gear6"};
        for(int k=0;k<6;++k)gates[k]=held(f,G[k]);
        // The race's pause (mode 18) takes no shift (C1h run1: 16 pulses there, no gear change), so the gearbox is not
        // stepped: no pulse, no retry spent, no deadline running (sana 44117). The pause's own scene changes reset it,
        // so on the resume the gate held then starts afresh from the consumed gear, after a released tick.
        const int dir=gearbox_.step_mode(gates,g.mode,g.car,g.gear,f.boundary_us);
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
    if(held(f,"shiftDown"))d.buttons_04|=0x8u;
    if(held(f,"camera"))d.buttons_04|=0x10u;
    d.buttons_04|=pulse;
    if(!race){
        if(held_k("confirm",KEnter))d.buttons_04|=0x2u;
        if(held_k("navRight",KRight))d.buttons_04|=0x80u;
        if(held_k("navLeft",KLeft))d.buttons_04|=0x100u;
    }
    // Edges: the core's for actions; the gearbox's own for its pulses; the menu keys' own.
    out.shift_up|=down(f,"shiftUp")||(pulse_down&0x2u);
    out.shift_down|=down(f,"shiftDown")||(pulse_down&0x8u);
    out.menu_up|=down_k("navUp",KUp);out.menu_down|=down_k("navDown",KDown);
    out.menu_left|=down_k("navLeft",KLeft);out.menu_right|=down_k("navRight",KRight);
    out.menu_confirm|=down_k("confirm",KEnter)||down(f,"start");out.menu_cancel|=down_k("back",KEsc);
    out.start|=down(f,"start");
    // The horn ([Audio] AllowHorn): its own held action, sounded by native_race_horn; no record bit, so it never shifts.
    out.horn|=held(f,"horn");
    // Frontend features (48F5F0), as the pad adapter sets them from its button edges.
    auto& m=out.frontend.feature_mask;
    if(down(f,"start"))m|=1u;
    if(down_k("confirm",KEnter))m|=4u;
    if(down_k("back",KEsc))m|=8u;
    if(down_k("navUp",KUp))m|=0x400u;
    if(down_k("navDown",KDown))m|=0x800u;
    if(down_k("navLeft",KLeft))m|=0x1000u;
    if(down_k("navRight",KRight))m|=0x2000u;
    if(!race){if(down(f,"shiftDown"))m|=0x10u;if(down(f,"shiftUp"))m|=0x20u;}
    if(down(f,"camera"))m|=0x40000u;
    if(d.buttons_04||steer||throttle||brake)out.connected=true;

    // Ordinary play: the last device used drives the race (wheel_input.cpp ea34c8b). Without force feedback nothing
    // centres the wheel, so its resting position is not a test of idleness: a key pressed while the pedals and wheel
    // buttons are at rest hands the race to the keyboard, until the wheel turns 3% (983 of 32767) from where it was then,
    // a pedal is pressed or a wheel button is held. A profile with no wheel axes leaves the race to the keyboard.
    if(kb){
        // Held buttons as wheel_input's ButtonActions, without the gears: an H-pattern gate is a button held for as long
        // as the lever sits in it, so a lever at rest in a gate is not wheel use (zero 25230). A gear change is: a gate's
        // own edge, or the gearbox's shift pulse, as wheel_input counts its gear_pulses.
        static constexpr const char* Buttons[]{"start","shiftUp","back","shiftDown","camera","confirm","navUp","navDown","navLeft","navRight","horn"};
        static constexpr const char* Gates[]{"gear1","gear2","gear3","gear4","gear5","gear6"};
        bool wheel_button=false;for(const char* b:Buttons)wheel_button=wheel_button||held(f,b);
        bool gear_change=pulse!=0u;for(const char* gate:Gates)gear_change=gear_change||down(f,gate);
        const bool wheel_pressed=wheel_accel>=3u||wheel_brake>=3u||wheel_button||gear_change;
        if(!keyboard_drives_&&kb->any&&!wheel_pressed){keyboard_drives_=true;keyboard_anchor_=steer_word;}
        else if(keyboard_drives_&&(wheel_pressed||std::abs(steer_word-keyboard_anchor_)>983))keyboard_drives_=false;
        out.pc_overlay=steer||throttle||brake;
        out.pc_overlay_yield=keyboard_drives_;
    }

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
        if(!owned.empty()&&!pads[i].known)continue;   // unidentified: it could be the owned pad
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

std::vector<std::string> owned_products(const dbce::controls::Section& profile){
    std::vector<std::string> out;
    for(const auto& e:profile.bound)
        if(!e.binding.prod.empty()&&std::find(out.begin(),out.end(),e.binding.prod)==out.end())out.push_back(e.binding.prod);
    return out;
}
}
