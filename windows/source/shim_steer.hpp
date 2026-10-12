#pragma once
// The shim's steering wheel for force feedback (shim_input_steer in shim_input.hpp, INPUT-SHIM.md section 6): pure, over
// the [Controls] steer binding and Core's device reports, so tests/test_shim_steer.cpp checks it against the real Core.
#include "shim_input.hpp"
#include "shim.hpp"
#include <vector>
namespace outrun::desktop {
// `steer`: the binding as the profile parser read it (null: steer is not bound). The parser writes every GUID in one
// lowercase form and Core keeps one device per instance, so the instance is matched exactly.
inline ShimSteer shim_steer(const dbce::controls::Binding* steer,const std::vector<dbce::shim::DeviceReport>& reports){
    ShimSteer s;
    if(!steer){s.status="steer is not bound";return s;}
    s.dev=steer->dev;s.prod=steer->prod;
    for(const auto& d:reports){
        if(d.identity.dev!=steer->dev)continue;
        // Core keeps the instance's first binding's product; a steer binding that disagrees with it is not applied.
        if(d.identity.prod!=steer->prod){s.status="the shim reads this instance as product "+d.identity.prod+", not the binding's";return s;}
        s.opened=d.opened;s.epoch=d.epoch;s.status=d.status;
        return s;
    }
    s.status="the shim reads no device for this instance";
    return s;
}
}
