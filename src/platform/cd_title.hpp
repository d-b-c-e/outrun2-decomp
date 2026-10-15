#pragma once
// Optional CD title presentation. All state belongs to the game/render thread.
// Tweaks: 120 game updates total, with alpha fading during the last 45.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
namespace outrun::platform {
struct CdTitleOptions {
    bool enabled{};unsigned font{2};float scale_x{0.3f},scale_y{0.5f};int x{375},y{450};
};
inline bool cd_title_race_owned(std::uint32_t mode,bool blocked,bool hud_visible){
    return mode==16u&&!blocked&&hud_visible;
}
inline CdTitleOptions cd_title_options(bool enabled,int font,float sx,float sy,int x,int y){
    const auto scale=[](float v,float fallback){return std::isfinite(v)?std::clamp(v,0.05f,2.0f):fallback;};
    return {enabled,unsigned(std::clamp(font,0,9)),scale(sx,0.3f),scale(sy,0.5f),
            std::clamp(x,0,639),std::clamp(y,0,479)};
}
struct CdTitleState {
    CdTitleOptions options{};unsigned remaining{};std::uint32_t last_frame{};bool frame_seen{};
    void clear(){remaining=0;}
    void changed(){if(options.enabled)remaining=120;}
    // One submission per rendered frame, but age only by existing 60 Hz game
    // updates (display-only 120 Hz frames do not shorten the two-second title).
    unsigned sample(std::uint32_t frame,unsigned updates,bool owns_hud){
        if(!owns_hud){clear();return 0;}
        if(!options.enabled||!remaining||(frame_seen&&frame==last_frame))return 0;
        frame_seen=true;last_frame=frame;
        const unsigned alpha=remaining>45?255u:remaining*255u/45u;
        remaining=updates>=remaining?0:remaining-updates;
        return alpha;
    }
};
inline std::string cd_title_label(std::size_t index,std::string_view title){
    // Preserve catalog bytes/path separately. The game's byte font gets one
    // literal line, at most 96 title bytes; controls/non-ASCII become '?'.
    if(index>=256)return {};
    const auto number=index+1;
    std::string out="#"+(number<10?std::string("0"):std::string())+std::to_string(number)+". ";
    const bool shortened=title.size()>96u;const auto n=shortened?93u:title.size();
    for(std::size_t i=0;i<n;++i){const auto c=static_cast<unsigned char>(title[i]);out+=c>=32u&&c<=126u?char(c):'?';}
    if(shortened)out+="...";
    return out;
}
}
