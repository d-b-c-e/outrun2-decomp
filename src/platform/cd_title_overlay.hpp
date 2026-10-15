#pragma once
#include "platform/cd_switcher.hpp"
#include "platform/frontend_text.hpp"
#include <stdexcept>
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
    const auto glyph_fits=[&](const FrontendGlyph& g){
        return g.right-g.left==font.width&&std::isfinite(g.x)&&g.x>=float(style.clip_left)&&
            g.x+float(g.right-g.left)*g.scale_x<=float(style.clip_right);
    };
    const auto fits=[&](std::string_view text){
        const auto count=std::size_t(std::count_if(text.begin(),text.end(),[](char c){return c!=' ';}));
        if(draws.size()!=count)return false; // The producer dropped a clipped glyph.
        return std::all_of(draws.begin(),draws.end(),glyph_fits);
    };
    // Preserve the producer's atomic refusal of unavailable glyphs/pair data.
    if(!frontend_text_draw(font,style,cursor,full,draws))return false;
    if(!fits(full)){
        const auto prefix=cd_title_label(index,{}).size();
        // Remove the byte-limit ellipsis before considering a shorter body.
        const auto limit=full.size()-(title.size()>96u?3u:1u);
        std::size_t keep{};auto current=cursor;
        try{
            // One pass over the bounded label. Earlier prefix glyph positions
            // are unchanged by a cut; only the last retained character's
            // advance must be recomputed with '.' as its next character.
            for(std::size_t i=0;i<limit;++i){
                const auto c=std::uint8_t(full[i]);FrontendGlyph g;
                if(c!=' '&&(!frontend_text_glyph_42c860(font,style,current,c,g)||!glyph_fits(g)))break;
                if(i+1>=prefix){
                    auto dots=current;frontend_text_advance_42c610(font,style,dots,c,'.');
                    draws.clear();
                    if(!frontend_text_draw(font,style,dots,"...",draws))return false;
                    if(fits("..."))keep=i+1;
                }
                frontend_text_advance_42c610(font,style,current,c,std::uint8_t(full[i+1]));
            }
        }catch(const std::out_of_range&){return false;}
        if(keep<prefix)return false; // Even the track prefix plus ... cannot fit.
        const auto text=full.substr(0,keep)+"...";draws.clear();
        if(!frontend_text_draw(font,style,cursor,text,draws)||!fits(text))return false;
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
