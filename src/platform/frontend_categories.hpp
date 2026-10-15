#pragma once
#include "frontend_profiles.hpp"
#include "frontend_ui_resources.hpp"
#include "frontend_text.hpp"
#include "title_owner.hpp"
namespace outrun::platform {
constexpr unsigned PcCategoryOwnerBytes=0x674;
// EnableHollyCourse2 (pc_screen.hpp): Tweaks allocates 0x714 (its 4425A6 patch) for an eighth tile at +494 and moves
// the bottom frame from +494 to +674. The PC's size stays the allocation and guest-visible size.
constexpr unsigned PcCategoryOwnerHolly2Bytes=0x714;
struct FrontendCategoryState {
    std::array<std::uint8_t,PcCategoryOwnerHolly2Bytes> object{};
    // PC process globals, retained when this child is suspended/reconstructed.
    unsigned selected_84b7f0{},selection_84b7f4{},selection_84b7f8{},query_83639c{};
    // EnableHollyCourse2 mode 3: Tweaks patches the 5CE400 selection table only when group 7's unlock state changes,
    // starting from locked, and the patch persists for the process (its static in the draw hook).
    bool holly2_nav_patched{},holly2_nav_unlocked{};
    unsigned fault{};
    bool constructed{};
    std::vector<FrontendGlyph> glyphs;
};
struct FrontendCategoryServices {
    FrontendUiResources& ui;
    driving::Bytes root;
    const PcLicense& profile;
    const LicenseProgressTables& progress;
    const FrontendInputSnapshot& input;
    unsigned& repeat;
    const FrontendFontPack* fonts{};
    const FrontendTextTable* text{};
    unsigned missing{};
};
bool frontend_categories_construct_4e8130(FrontendCategoryState&,unsigned& repeat);
bool frontend_categories_init_4e8760(FrontendCategoryState&,FrontendCategoryServices&);
bool frontend_categories_control_4e8780(FrontendCategoryState&,FrontendCategoryServices&,unsigned& result);
bool frontend_categories_display_4e8c60(FrontendCategoryState&,FrontendCategoryServices&);
bool frontend_categories_suspend_4e8a30(FrontendCategoryState&,FrontendCategoryServices&);
}
