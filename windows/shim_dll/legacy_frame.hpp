#pragma once
// ABI2 is a fixed 31-action/296-byte facade over the extended source Core.
#include "dbce_shim.h"
#include "shim.hpp"
namespace dbce::shim {
inline Capability legacy_abi2_capability(){
    Capability c;
    for(int i=LegacyActionCount;i<ActionCount;++i)c.unsupported[std::size_t(i)]="camera actions require a source consumer; ABI2 exposes31";
    return c;
}
inline void legacy_abi2_frame(const Frame& f,dbce_shim_frame& frame){
    constexpr std::uint64_t mask=(1ull<<LegacyActionCount)-1;
    frame.abi=DBCE_SHIM_ABI;frame.seq=f.seq;frame.tick_us=f.tick_us;frame.down=f.down&mask;frame.up=f.up&mask;
    frame.run=std::uint8_t(f.run);frame.latched=f.latched;frame.suppress_native=f.suppress_native;frame.reserved0=0;
    frame.sample_count=std::uint32_t(f.samples.size());
    for(int i=0;i<DBCE_SHIM_ACTIONS;++i){const auto& a=f.action[std::size_t(i)];auto& o=frame.action[i];
        o.value=a.value;o.status=std::uint8_t(a.status);o.injected=a.injected;o.reserved[0]=o.reserved[1]=0;}
}
}
