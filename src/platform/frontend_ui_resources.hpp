#pragma once
#include "platform/frontend_sprites.hpp"
#include "driving/pc_common_control.hpp"
#include "platform/frontend_text.hpp"
#include <vector>
namespace outrun::platform {
// Reusable native leaves for welcome/owner/license UI resources. No singleton
// handles and no synthetic ready result. Unsupported services are observable.
struct FrontendUiResources {
    FrontendSprites& sprites;
    std::uint32_t missing_pc{};
    std::uint32_t pause_domain{};
    float motion_step{}; // explicit 449E30 milliseconds * PC float 0.001
    void* effect_user{};
    bool (*effect_4249f0)(void*,std::uint32_t){};
    // 4656B0's follow pointer: a resource with +9C set takes +40..+48 from the three floats at
    // the guest address +98 (the C2C request manager's popups, 4FE6C0: manager +7684). The
    // caller's guest memory reads them (false: unreadable); without a reader such a resource
    // stays an observable missing 4656B0.
    void* follow_user{};
    bool (*follow_4656b0)(void*,std::uint32_t address,std::uint8_t* out12){};
    bool input_feedback(driving::Bytes root,std::uint32_t key,std::int32_t argument);
    bool call(std::uint32_t pc,driving::Bytes resource,const std::uint32_t* args,
              std::size_t count,std::uint32_t& result);
    driving::PcUiNotifyServices notify();
    driving::PcUiResourceCommitServices465970 commit();
    driving::PcUiResourceTickServices4659f0 ticks();
    driving::PcEmbeddedSlotsServices446cf0 embedded_slots();
    bool commands(std::uint32_t pc,driving::Bytes embedded,const std::uint32_t* args,
                  std::size_t count,driving::PcUiNotifyGlobals&,std::uint32_t& result);
    std::uint32_t sprite_call(std::uint32_t pc,const std::uint32_t* args,std::size_t count);
    bool finalize(driving::Bytes resource);
};
// 446A50 / 446510: the captions of the four button slots (owner +0x51C, the
// help bar labels next to the button icons: SELECT, BACK, LICENSE...), font 9,
// mode 0xE, white, left-aligned for slots 0..1 and right-aligned for 2..3 at
// the 59DC84 / 59DC88 / 59DCA4 positions. False when the font, a caption
// string or the position table is missing.
bool frontend_slot_captions_446a50(driving::Bytes embedded,const FrontendFontPack* fonts,
                                   const FrontendTextTable* table,std::vector<FrontendGlyph>& glyphs);
}
