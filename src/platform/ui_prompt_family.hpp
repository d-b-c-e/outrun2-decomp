#pragma once
#include <cstdint>

namespace outrun::platform {
// Presentation only: the already selected physical WGI pad, never its common
// PadSample button layout, wheel/shim actions, replay or synthesized connected bit.
enum class UiPromptFamily : std::uint8_t { PC, Xbox, PlayStation, Switch };

// Conservative first-party identities present in Tweaks' pinned SDL preview-3.1.8
// controller_list.h (22422f7). No manufacturer-wide guess or controller database.
// Unsupported products/unknown identity keep the generic PC art.
constexpr UiPromptFamily ui_prompt_family_for_identity(bool known,std::uint16_t vid,std::uint16_t pid){
    if(!known)return UiPromptFamily::PC;
    if(vid==0x045eu&&(pid==0x028eu||pid==0x02d1u||pid==0x02e0u||pid==0x02eau||pid==0x02fdu||pid==0x0b12u||pid==0x0b13u))return UiPromptFamily::Xbox;
    if(vid==0x054cu&&(pid==0x0268u||pid==0x05c4u||pid==0x09ccu||pid==0x0ba0u||pid==0x0ce6u||pid==0x0df2u))return UiPromptFamily::PlayStation;
    if(vid==0x057eu&&(pid==0x2006u||pid==0x2007u||pid==0x2008u||pid==0x2009u))return UiPromptFamily::Switch;
    return UiPromptFamily::PC;
}
constexpr bool ui_prompt_hash(std::uint32_t hash){
    return hash==0x455717B2u||hash==0x1F77CB88u||hash==0xFA7BBB13u||hash==0x39229D64u||hash==0xACF61D7Cu||hash==0x31C58963u;
}
struct UiPromptSelection {
    UiPromptFamily current{UiPromptFamily::PC},latched{UiPromptFamily::PC};
    // Tweaks51acc: only a recognized prompt lookup latches the first non-PC
    // family. A later disconnect/family change cannot switch a loaded atlas.
    const char* folder(bool ui,std::uint32_t post_fxt_hash){
        if(!ui||!ui_prompt_hash(post_fxt_hash))return nullptr;
        if(latched==UiPromptFamily::PC)latched=current;
        switch(latched){
        case UiPromptFamily::Xbox:return "xbox";
        case UiPromptFamily::PlayStation:return "playstation";
        case UiPromptFamily::Switch:return "switch";
        default:return nullptr;
        }
    }
};
// Called only by the existing physical provider, before the sample is mapped or
// synthetic/replay buttons can make it connected. Configure resets selection.
void texture_replacement_set_pad_family(UiPromptFamily family);
}
