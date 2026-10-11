#pragma once
// A scripted device provider for the input shim's tests (INPUT-SHIM.md section 6): listing, opening with an identity
// read back from the new handle, Poll and the state read as separate calls with their own scripted results,
// reacquisition, closing, and optional delays on the worker calls. Thread-safe: the worker and the input thread may
// call it at once. Records misuse (a read of a closed handle, a close during a read).
#include "shim.hpp"
#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
namespace dbce::shim::fake {
struct Device {
    DeviceInfo listed;                    // what enumerate() shows
    DeviceInfo opened;                    // what open() reads back from the new handle (empty: same as listed)
    ObjectCaps caps;
    bool attached=true;
    bool open_fails=false;
    RawState state;                       // the physical state reads return
    std::deque<std::pair<Call,Call>> script;   // the next reads' (poll, state) results; empty: (Ok, Ok)
    std::deque<Call> reacquire_script;    // empty: Ok
    std::deque<std::int64_t> state_advance_us;   // the next state reads each take this long on the fake clock
    int opens=0,closes=0,polls=0,state_reads=0,reacquires=0;
};
class Provider final:public shim::Provider {
public:
    std::mutex m;
    std::vector<Device> devices;
    int enumerate_delay_ms=0,open_delay_ms=0;
    int enumerations=0;
    bool real_clock=false;
    std::int64_t clock=1000000;
    int misuse=0;                         // reads of closed handles, closes during reads
    std::atomic<int> in_read{0};

    std::size_t add(Device d){std::lock_guard<std::mutex> l(m);devices.push_back(std::move(d));return devices.size()-1;}
    Device& at(std::size_t i){return devices[i];}
    void advance(std::int64_t us){std::lock_guard<std::mutex> l(m);clock+=us;}

    std::vector<DeviceInfo> enumerate()override{
        if(enumerate_delay_ms)std::this_thread::sleep_for(std::chrono::milliseconds(enumerate_delay_ms));
        std::lock_guard<std::mutex> l(m);
        ++enumerations;
        std::vector<DeviceInfo> out;
        for(const auto& d:devices)if(d.attached)out.push_back(d.listed);
        return out;
    }
    OpenResult open(const DeviceInfo& listed)override{
        if(open_delay_ms)std::this_thread::sleep_for(std::chrono::milliseconds(open_delay_ms));
        std::lock_guard<std::mutex> l(m);
        OpenResult r;
        for(std::size_t i=0;i<devices.size();++i){
            auto& d=devices[i];
            if(d.listed.dev!=listed.dev||!d.attached)continue;
            ++d.opens;
            if(d.open_fails){r.why="scripted open failure";return r;}
            r.ok=true;r.opened=d.opened.dev.empty()?d.listed:d.opened;r.caps=d.caps;
            r.handle=next_++;handles_[r.handle]=i;
            return r;
        }
        r.why="not attached";
        return r;
    }
    Call reacquire(Handle h)override{
        std::lock_guard<std::mutex> l(m);
        auto* d=find(h);if(!d)return Call::Gone;
        ++d->reacquires;
        if(d->reacquire_script.empty())return Call::Ok;
        const auto c=d->reacquire_script.front();d->reacquire_script.pop_front();return c;
    }
    void close(Handle h)override{
        if(in_read.load())++misuse;
        std::lock_guard<std::mutex> l(m);
        auto* d=find(h);if(d)++d->closes;
        handles_.erase(h);closed_[h]=true;
    }
    Call poll(Handle h)override{
        ++in_read;
        std::lock_guard<std::mutex> l(m);
        Call c=Call::Ok;
        auto* d=find(h);
        if(!d){if(closed_.count(h))++misuse;c=Call::Gone;}
        else{
            ++d->polls;
            pending_state_[h]=Call::Ok;
            if(!d->script.empty()){c=d->script.front().first;pending_state_[h]=d->script.front().second;d->script.pop_front();}
        }
        --in_read;
        return c;
    }
    Call state(Handle h,RawState& out)override{
        ++in_read;
        std::lock_guard<std::mutex> l(m);
        Call c=Call::Gone;
        auto* d=find(h);
        if(!d){if(closed_.count(h))++misuse;}
        else{
            ++d->state_reads;
            if(!d->state_advance_us.empty()){clock+=d->state_advance_us.front();d->state_advance_us.pop_front();}
            const auto p=pending_state_.find(h);
            c=p==pending_state_.end()?Call::Ok:p->second;
            if(c==Call::Ok)out=d->state;
        }
        --in_read;
        return c;
    }
    std::int64_t now_us()override{
        if(real_clock)return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        std::lock_guard<std::mutex> l(m);return clock;
    }
private:
    Handle next_=1;
    std::map<Handle,std::size_t> handles_;
    std::map<Handle,bool> closed_;
    std::map<Handle,Call> pending_state_;
    Device* find(Handle h){const auto i=handles_.find(h);return i==handles_.end()?nullptr:&devices[i->second];}
};
// A wheel-like device: axes X, Z and Rz in 0..65535, 128 buttons, one hat.
inline ObjectCaps wheel_caps(){
    ObjectCaps c;
    for(int a:{0,2,5}){c.axis[std::size_t(a)]=true;c.range_ok[std::size_t(a)]=true;c.min[std::size_t(a)]=0;c.max[std::size_t(a)]=65535;}
    c.button.set();c.pov[0]=true;
    return c;
}
}
