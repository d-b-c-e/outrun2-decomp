#pragma once
#include "platform/cd_switcher.hpp"
#include "platform/frontend_text.hpp"
namespace outrun::platform {
// Uses the existing recovered game font producer. Local style/cursor and a
// separate glyph list do not mutate any native HUD/font/widget state.
inline bool cd_title_glyphs(CdSwitcher& cd,const FrontendFontPack* fonts,std::uint32_t frame,
                            unsigned updates,bool owns_hud,std::vector<FrontendGlyph>& out){
    if(!cd.enabled||!cd.title.options.enabled)return false;
    const auto alpha=cd.title.sample(frame,updates,owns_hud);
    if(!alpha||!fonts||cd.cursor>=cd.tracks.size()||cd.title.options.font>=fonts->fonts.size())return false;
    const auto& font=fonts->fonts[cd.title.options.font];
    // An unavailable/unsupported font ages the bounded title without submitting.
    if(font.width<=0||font.width>128||font.height<=0||font.height>128)return false;
    FrontendTextStyle style;style.scale_x=cd.title.options.scale_x;style.scale_y=cd.title.options.scale_y;
    style.color=(alpha<<24)|0x00ffffffu;style.mode=4u;
    const auto x=std::int16_t(cd.title.options.x),y=std::int16_t(cd.title.options.y);
    return frontend_text_draw(font,style,{x,x,y},cd_title_label(cd.cursor,cd.tracks[cd.cursor].title),out);
}
}
