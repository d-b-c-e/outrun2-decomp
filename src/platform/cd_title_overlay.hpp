#pragma once
#include "platform/cd_switcher.hpp"
#include "platform/frontend_text.hpp"
namespace outrun::platform {
// Keep complete source cells as well as their scaled rendered extents inside
// the existing 640-wide HUD. The recovered producer clips unscaled cells; a
// byte-count limit alone can silently cut the suffix or part of a character.
inline bool cd_title_draw_bounded(const FrontendFont& font,FrontendTextStyle style,
                                  FrontendTextCursor cursor,std::size_t index,
                                  std::string_view title,std::vector<FrontendGlyph>& out){
    const auto full=cd_title_label(index,title);
    if(full.empty())return false;
    std::vector<FrontendGlyph> draws;
    const auto fits=[&](std::string_view text){
        const auto count=std::size_t(std::count_if(text.begin(),text.end(),[](char c){return c!=' ';}));
        if(draws.size()!=count)return false; // The producer dropped a clipped glyph.
        return std::all_of(draws.begin(),draws.end(),[&](const FrontendGlyph& g){
            return g.right-g.left==font.width&&std::isfinite(g.x)&&g.x>=float(style.clip_left)&&
                g.x+float(g.right-g.left)*g.scale_x<=float(style.clip_right);
        });
    };
    // Preserve the producer's atomic refusal of unavailable glyphs/pair data.
    if(!frontend_text_draw(font,style,cursor,full,draws))return false;
    if(!fits(full)){
        const auto prefix=cd_title_label(index,{}).size();
        // Remove the byte-limit ellipsis before considering a shorter body.
        auto keep=full.size()-(title.size()>96u?3u:1u);
        for(;;){
            if(keep<prefix)return false; // Even the track prefix plus ... cannot fit.
            const auto text=full.substr(0,keep)+"...";
            draws.clear();
            if(!frontend_text_draw(font,style,cursor,text,draws))return false;
            if(fits(text))break;
            --keep;
        }
    }
    out.insert(out.end(),draws.begin(),draws.end());return true;
}
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
    return cd_title_draw_bounded(font,style,{x,x,y},cd.cursor,cd.tracks[cd.cursor].title,out);
}
}
