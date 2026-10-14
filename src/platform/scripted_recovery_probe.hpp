#pragma once
// Developer-only, process-lifetime admission for one scripted PlWrecker call.
// This has no game effect unless the special desktop build calls take().
#include "platform/car_producer_observer.hpp"
#include <cstdint>
#include <string_view>
namespace outrun::platform {
inline bool scripted_recovery_requested(const char* value) noexcept {
    return value&&std::string_view(value)=="delayed-once";
}
inline bool scripted_recovery_shadow_admitted(const NativeCarProducerObserver& observer) noexcept {
    return observer.observe&&observer.ordinary_shadow_admitted&&*observer.ordinary_shadow_admitted;
}
struct ScriptedRecoveryProbe {
    static constexpr std::uint32_t ReadyCalls=300u;
    std::uint32_t eligible_calls{};
    bool attempted{};
    bool take(bool eligible) noexcept {
        if(attempted)return false;
        if(!eligible){eligible_calls=0u;return false;}
        if(++eligible_calls<ReadyCalls)return false;
        attempted=true; // latch before the production call: even a failure cannot silently retry
        return true;
    }
};
}
