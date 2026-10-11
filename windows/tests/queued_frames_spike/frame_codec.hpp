#pragma once
// Fixture codec v2: copied from beba6b1 audit, with explicit Pending support.
// Historical session_spike remains unchanged. This is not a portable replay schema.
#include "session_writer.h"
#define main upstream_adapter_tests_main
#include "test_outrun_adapter.cpp"
#undef main
#include <filesystem>
#include <fstream>
#include <limits>
#include <tuple>
namespace fs=std::filesystem;
using Channels=dbce::session::Channels;

// Numeric v1 channels are doubles. Split identifiers/timestamps rather than silently rounding uint64.
void u64(Channels& c,const std::string& key,std::uint64_t n){
    c[key+".lo"]=std::uint32_t(n);c[key+".hi"]=std::uint32_t(n>>32);
}
std::int64_t number(const Channels& c,const std::string& key,std::int64_t low,std::int64_t high){
    const auto n=c.at(key);
    if(!std::isfinite(n)||n!=std::floor(n)||n<double(low)||n>double(high))
        throw std::runtime_error("invalid integer: "+key);
    return std::int64_t(n);
}
std::uint64_t u64(const Channels& c,const std::string& key){
    return std::uint64_t(number(c,key+".lo",0,UINT32_MAX))|
        (std::uint64_t(number(c,key+".hi",0,UINT32_MAX))<<32);
}
void raw(Channels& c,const std::string& key,const RawState& r){
    for(int k=0;k<8;++k)c[key+".axis"+std::to_string(k)]=r.axes[k];
    for(int k=0;k<4;++k)c[key+".pov"+std::to_string(k)]=r.povs[k];
    // Preserve all 128 bytes, including low bits, in 32 exact unsigned 32-bit words.
    for(int k=0;k<32;++k){
        std::uint32_t word=0;
        for(int b=0;b<4;++b)word|=std::uint32_t(r.buttons[4*k+b])<<(8*b);
        c[key+".buttons"+std::to_string(k)]=word;
    }
}
RawState raw(const Channels& c,const std::string& key){
    RawState r;
    for(int k=0;k<8;++k)r.axes[k]=std::int32_t(number(c,key+".axis"+std::to_string(k),INT32_MIN,INT32_MAX));
    for(int k=0;k<4;++k)r.povs[k]=std::uint32_t(number(c,key+".pov"+std::to_string(k),0,UINT32_MAX));
    for(int k=0;k<32;++k){
        const auto word=std::uint32_t(number(c,key+".buttons"+std::to_string(k),0,UINT32_MAX));
        for(int b=0;b<4;++b)r.buttons[4*k+b]=std::uint8_t(word>>(8*b));
    }
    return r;
}
void bits(Channels& c,const std::string& key,const std::bitset<128>& bs){
    for(int k=0;k<4;++k){std::uint32_t w=0;for(int b=0;b<32;++b)if(bs[32*k+b])w|=1u<<b;
        c[key+std::to_string(k)]=w;}
}
std::bitset<128> bits(const Channels& c,const std::string& key){
    std::bitset<128> bs;
    for(int k=0;k<4;++k){auto w=std::uint32_t(number(c,key+std::to_string(k),0,UINT32_MAX));
        for(int b=0;b<32;++b)bs[32*k+b]=(w&(1u<<b))!=0;}
    return bs;
}
Channels encode(const Frame& f){
    Channels c;
    u64(c,"frame",f.seq);u64(c,"tickUs",std::uint64_t(f.tick_us));
    u64(c,"boundaryUs",std::uint64_t(f.boundary_us));u64(c,"eventSeq",f.event_seq);
    u64(c,"down",f.down);u64(c,"up",f.up);
    c["run"]=int(f.run);c["latched"]=f.latched;c["suppressNative"]=f.suppress_native;
    c["sampleCount"]=double(f.samples.size());
    for(int k=0;k<ActionCount;++k){
        const auto p="action."+std::string(action_name(k))+".";const auto& a=f.action[k];
        c[p+"value"]=a.value;c[p+"status"]=int(a.status);c[p+"sample"]=a.sample;c[p+"injected"]=a.injected;
    }
    for(std::size_t k=0;k<f.samples.size();++k){
        const auto p="sample."+std::to_string(k)+".";const auto& s=f.samples[k];
        c[p+"device"]=s.device;c[p+"epoch"]=s.epoch;u64(c,p+"seq",s.seq);
        u64(c,p+"readStartUs",std::uint64_t(s.read_start_us));u64(c,p+"readEndUs",std::uint64_t(s.read_end_us));
        c[p+"poll"]=int(s.poll);c[p+"state"]=int(s.state);c[p+"valid"]=s.valid;c[p+"stale"]=s.stale;
        raw(c,p+"physical",s.physical);raw(c,p+"delivered",s.delivered);
        u64(c,p+"batch",s.injected.batch);c[p+"axes"]=s.injected.axes;c[p+"povs"]=s.injected.povs;
        bits(c,p+"buttonMask",s.injected.buttons);
        for(int word=0;word<2;++word){std::uint32_t v=0;
            for(int b=0;b<4;++b)v|=std::uint32_t(s.injected.hash[word*4+b])<<(8*b);
            c[p+"hash"+std::to_string(word)]=v;}
    }
    return c;
}
Frame decode(const Channels& c){
    Frame f;
    f.seq=u64(c,"frame");f.tick_us=std::int64_t(u64(c,"tickUs"));f.boundary_us=std::int64_t(u64(c,"boundaryUs"));
    if(f.tick_us<0||f.boundary_us<f.tick_us)throw std::runtime_error("invalid frame times");
    f.event_seq=u64(c,"eventSeq");f.down=u64(c,"down");f.up=u64(c,"up");
    if((f.down>>ActionCount)||(f.up>>ActionCount))throw std::runtime_error("unknown action edge bits");
    f.run=Run(number(c,"run",0,int(Run::Inert)));f.latched=number(c,"latched",0,1)!=0;
    f.suppress_native=number(c,"suppressNative",0,1)!=0;
    f.samples.resize(std::size_t(number(c,"sampleCount",0,31)));
    for(int k=0;k<ActionCount;++k){
        const auto p="action."+std::string(action_name(k))+".";auto& a=f.action[k];
        const double v=c.at(p+"value");
        if(!std::isfinite(v)||v < (k==action_index("steer")?-1:0)||v>1)throw std::runtime_error("invalid action value");
        a.value=float(v);a.status=Status(number(c,p+"status",0,int(Status::TestNeutral)));
        a.sample=std::int16_t(number(c,p+"sample",-1,std::int64_t(f.samples.size())-1));
        a.injected=number(c,p+"injected",0,1)!=0;
    }
    for(std::size_t k=0;k<f.samples.size();++k){
        const auto p="sample."+std::to_string(k)+".";auto& s=f.samples[k];
        s.device=std::uint32_t(number(c,p+"device",0,30));s.epoch=std::uint32_t(number(c,p+"epoch",0,UINT32_MAX));
        s.seq=u64(c,p+"seq");s.read_start_us=std::int64_t(u64(c,p+"readStartUs"));s.read_end_us=std::int64_t(u64(c,p+"readEndUs"));
        if(s.read_start_us<f.tick_us||s.read_end_us<s.read_start_us||s.read_end_us>f.boundary_us)
            throw std::runtime_error("sample time outside its frame");
        s.poll=Call(number(c,p+"poll",0,int(Call::Pending)));s.state=Call(number(c,p+"state",0,int(Call::Pending)));
        s.valid=number(c,p+"valid",0,1)!=0;s.stale=number(c,p+"stale",0,1)!=0;
        s.physical=raw(c,p+"physical");s.delivered=raw(c,p+"delivered");
        s.injected.batch=u64(c,p+"batch");s.injected.axes=std::uint8_t(number(c,p+"axes",0,255));
        s.injected.povs=std::uint8_t(number(c,p+"povs",0,15));s.injected.buttons=bits(c,p+"buttonMask");
        for(int word=0;word<2;++word){const auto v=std::uint32_t(number(c,p+"hash"+std::to_string(word),0,UINT32_MAX));
            for(int b=0;b<4;++b)s.injected.hash[word*4+b]=std::uint8_t(v>>(8*b));}
    }
    // Reject extensions or missing fields rather than silently omitting provenance.
    if(encode(f)!=c)throw std::runtime_error("noncanonical/missing/unknown frame channels");
    return f;
}

void same_sink(const NativeInputState& a,const NativeInputState& b){
    auto fields=[](const NativeInputState& v){return std::tie(v.frontend.axes,v.frontend.feature_mask,v.frontend.device_held,
        v.pc_device.buttons_04,v.pc_device.axes_94,v.pc_pad.axes,v.pc_pad.pov,v.pc_pad.buttons,
        v.pc_input_layer,v.pc_record_used,v.connected,v.steering,v.accelerator,v.brake,v.shift_up,v.shift_down,
        v.menu_left,v.menu_right,v.menu_up,v.menu_down,v.menu_confirm,v.menu_cancel,v.menu_preview,v.menu_start_probe,
        v.start,v.exit_requested,v.raw_buttons_held,v.raw_buttons_down,v.raw_buttons_up,
        v.left_stick_x,v.left_stick_y,v.right_stick_x,v.right_stick_y);};
    check(fields(a)==fields(b),"action replay matches every NativeInputState field");
}
void same_signals(const Signals& a,const Signals& b){
    auto fields=[](const Signals& v){return std::tie(v.frame_seq,v.mode,v.in_gameplay,v.speed,v.gear,
        v.consumed_steer,v.consumed_accel,v.consumed_brake,v.record_steer,v.record_accel,v.record_brake,
        v.record_buttons,v.native_suppressed,v.gearbox);};
    check(fields(a)==fields(b),"action replay matches every adapter signal");
}
