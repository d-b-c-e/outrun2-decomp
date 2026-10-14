#pragma once
#include <cstddef>
#include <cstdint>

namespace outrun::platform {
// Read-only, ephemeral view of the just-completed GamePlCar control.
struct NativeCarProducerObservation {
    const std::uint8_t* car{};
    std::size_t car_bytes{};
    std::uint32_t mode{},car_generation{},continuity{};
    std::uint64_t attempt{};
    std::uint32_t frame_counter{},update_index{};
    bool producer_fresh{},direct{},car_active{},recovery_pending{};
};
struct NativeCarProducerObserver {
    void* user{};
    void (*observe)(void* user,const NativeCarProducerObservation&){};
    // Points at the ordinary shadow's live admission flag, so detaching its
    // writer revokes a runtime context's copied observer without another tick.
    const bool* ordinary_shadow_admitted{};
};
inline bool game_steer_written(std::int32_t mode,std::int16_t stale_counter) noexcept {
    return mode==16&&stale_counter<=120;
}
inline bool fresh_car_producer(bool direct,bool active,std::uint32_t generation,bool succeeded,
                               bool steer_written,std::uint32_t car_flags) noexcept {
    return direct&&active&&generation!=0u&&succeeded&&steer_written&&
        (car_flags&0x00800000u)==0u;
}
// An optional diagnostic can fail without changing the game's result or error latch.
inline void notify_car_producer(NativeCarProducerObserver& observer,const NativeCarProducerObservation& o) noexcept {
    if(!observer.observe)return;
    try{observer.observe(observer.user,o);}catch(...){observer={};}
}
}
