#pragma once
// The OutRun 2006 decomp's action table: the contract actions the game takes and, for each other one, why not. It is
// one list for the shim capability (outrun_adapter.cpp outrun_capability), the controls report and the native wheel
// path (controls_store.cpp unavailable_reason). The capability that Wheelkit admits from a package (the consumer's
// output) and the runtime reports therefore cannot drift (nene 40406, sana 40764).
#include <string_view>
namespace outrun::desktop {
struct UnavailableAction { const char* id; const char* why; };
inline constexpr const char* DeliveredActions[]{"steer","throttle","brake","shiftUp","shiftDown","confirm","back","start",
    "navUp","navDown","navLeft","navRight","camera","gear1","gear2","gear3","gear4","gear5","gear6","horn"};
inline constexpr UnavailableAction UnavailableActions[]{
    {"clutch","OutRun 2006 has no clutch"},{"handbrake","OutRun 2006 has no handbrake"},
    {"neutral","OutRun 2006 has no neutral gear"},{"reverse","OutRun 2006 has no reverse gear"},
    {"gear7","the H-pattern mapping covers gears 1-6"},{"gear8","the H-pattern mapping covers gears 1-6"},
    {"select","no game action"},{"lookBack","not mapped yet"},{"reset","not mapped yet"},
    {"modMenu","not mapped yet (the F6 key opens the panel)"},{"muteFfb","not mapped yet (force output is set in the F6 panel)"}};
// nullptr for a delivered action; the reason for an unavailable one; "unknown action" for anything else.
inline const char* outrun_unavailable_reason(std::string_view action){
    for(const char* d:DeliveredActions)if(action==d)return nullptr;
    for(const auto& u:UnavailableActions)if(action==u.id)return u.why;
    return "unknown action";
}
}
