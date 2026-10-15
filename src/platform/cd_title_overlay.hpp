#pragma once
#include "platform/cd_switcher.hpp"
#include "platform/frontend_text.hpp"
#include <limits>
#include <stdexcept>
namespace outrun::platform {
// Title-only readability: retain native glyphs and normal letter kerning, but
// do not let integer cursor truncation collapse word spaces or period gaps.
// Fitting and final submission both use this exact advance policy.
inline void cd_title_advance(const FrontendFont& font,const FrontendTextStyle& style,
                              FrontendTextCursor& cursor,std::uint8_t c,std::uint8_t next){
    const int before=cursor.x;
    frontend_text_advance_42c610(font,style,cursor,c,next);
    float minimum{};
    if(c==' ')minimum=std::ceil(std::max(1.f,float(font.width)*style.scale_x*0.5f));
    else if(c=='.'||next=='.'){
        int ink=font.width;
        if(!font.metrics.empty()){
            if(c<font.first||std::size_t(c-font.first)>=font.metrics.size())throw std::out_of_range("title glyph outside font metrics");
            ink=std::max(0,int(font.metrics[c-font.first][1]));
        }
        minimum=std::ceil(float(ink)*style.scale_x+std::max(1.f,style.scale_x));
    }
    if(minimum>0.f){
        if(!std::isfinite(minimum)||minimum>1024.f||before+int(minimum)>std::numeric_limits<std::int16_t>::max())
            throw std::out_of_range("title advance outside bounded cursor");
        cursor.x=std::int16_t(std::max(int(cursor.x),before+int(minimum)));
    }
}
inline bool cd_title_layout(const FrontendFont& font,FrontendTextStyle style,
                             FrontendTextCursor cursor,std::string_view text,std::vector<FrontendGlyph>& out){
    std::vector<FrontendGlyph> draws;
    try{
        for(std::size_t i=0;i<text.size();++i){
            const auto c=std::uint8_t(text[i]);FrontendGlyph g;
            if(frontend_text_glyph_42c860(font,style,cursor,c,g))draws.push_back(g);
            cd_title_advance(font,style,cursor,c,i+1<text.size()?std::uint8_t(text[i+1]):0);
        }
    }catch(const std::out_of_range&){return false;}
    out.insert(out.end(),draws.begin(),draws.end());return true;
}
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
    if(!cd_title_layout(font,style,cursor,full,draws))return false;
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
                    auto dots=current;cd_title_advance(font,style,dots,c,'.');
                    draws.clear();
                    if(!cd_title_layout(font,style,dots,"...",draws))return false;
                    if(fits("..."))keep=i+1;
                }
                cd_title_advance(font,style,current,c,std::uint8_t(full[i+1]));
            }
        }catch(const std::out_of_range&){return false;}
        if(keep<prefix)return false; // Even the track prefix plus ... cannot fit.
        const auto text=full.substr(0,keep)+"...";draws.clear();
        if(!cd_title_layout(font,style,cursor,text,draws)||!fits(text))return false;
    }
    out.insert(out.end(),draws.begin(),draws.end());return true;
}
// Uses the existing recovered game glyph producer. Local style/cursor and a
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
