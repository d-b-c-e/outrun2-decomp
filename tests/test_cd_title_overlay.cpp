// Synthetic font/track/clock records only. No retail assets, decoder, device,
// game window, hardware, renderer setup, or input injection.
#include "platform/cd_title_overlay.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace outrun::platform;
namespace {
unsigned checks{};
void check(bool ok,const char* message){++checks;if(!ok){std::fprintf(stderr,"CD title FAIL: %s\n",message);std::exit(1);}}
struct Player {
    bool fail{};unsigned calls{};
    static bool play(void* p,const CdTrack&,std::size_t,bool){auto& s=*static_cast<Player*>(p);++s.calls;return !s.fail;}
};
void timing(){
    CdTitleState t;t.changed();check(t.remaining==0&&t.sample(0,1,true)==0,"display defaults off");
    t.options.enabled=true;t.changed();
    check(t.remaining==120,"manual title starts at 120 updates");
    check(t.sample(0,0,true)==255&&t.remaining==120,"120 Hz display-only frame does not age title");
    check(t.sample(0,50,true)==0&&t.remaining==120,"multiple flushes in one render frame do not repeat or age");
    check(t.sample(1,75,true)==255&&t.remaining==45,"first 75 updates full opacity");
    check(t.sample(2,1,true)==255&&t.remaining==44,"fade starts at 45 remaining");
    check(t.sample(3,43,true)==249&&t.remaining==1,"44 remaining fades, game-update batch advances");
    check(t.sample(4,1,true)==5&&t.remaining==0&&t.sample(5,1,true)==0,"last update fades then expires");
    t.changed();check(t.sample(6,~0u,true)==255&&t.remaining==0,"large update count saturates instead of wrapping");
    t.changed();check(t.sample(7,1,false)==0&&t.remaining==0&&t.sample(8,1,true)==0,"lost HUD ownership clears, does not resume");
    for(const auto mode:{0u,13u,17u,18u,19u,32u})check(!cd_title_race_owned(mode,false,true),"menu/loading/pause/goal do not own title");
    check(cd_title_race_owned(16,false,true),"GAME visible HUD owns title");
    check(!cd_title_race_owned(16,true,true),"existing panel/focus/entry block clears title");
    check(!cd_title_race_owned(16,false,false),"hidden native HUD does not own title");
}
void transaction(){
    Player p;CdSwitcher cd{true,{{"first.ogg","One"},{"second.ogg","Two"}},0,&p,Player::play};
    cd.title.options.enabled=true;
    check(cd.select(1)&&cd.title.remaining==0,"initial/race selection does not arm title");
    check(cd.change({false,true})&&cd.cursor==0&&cd.title.remaining==120,"successful manual wrap arms actual current title");
    (void)cd.title.sample(1,20,true);p.fail=true;
    check(!cd.change({false,true})&&cd.cursor==0&&cd.title.remaining==100,"failed open preserves cursor and timer, does not refresh");
    p.fail=false;check(cd.change({true,true})&&cd.cursor==0&&cd.title.remaining==120,"simultaneous action one successful open refreshes once");
    const auto calls=p.calls;cd.enabled=false;
    check(!cd.change({false,true})&&p.calls==calls,"off does not add playback or arm title");
    cd.enabled=true;cd.play=nullptr;
    check(!cd.change({false,true})&&p.calls==calls,"unbound audio cannot display a new selected title");
}
void labels_and_options(){
    check(cd_title_label(0,"100% %s")=="#01. 100% %s","title percent signs are literal, not a format");
    check(cd_title_label(255,"")=="#256. ","empty title keeps bounded track number");
    check(cd_title_label(256,"invalid").empty(),"index outside catalog bound refused");
    check(cd_title_label(1,std::string("A\n\t")+char(0xff))=="#02. A???","control/non-ASCII bytes cannot produce extra lines");
    const auto long_title=cd_title_label(255,std::string(512,'A'));
    check(long_title.size()==102&&long_title.substr(long_title.size()-3)=="...","long title has fixed display bound; catalog is separate");
    const auto o=cd_title_options(true,99,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-1,9999);
    check(o.font==9&&o.scale_x==0.3f&&o.scale_y==0.5f&&o.x==0&&o.y==479,"invalid finite settings normalized without NaN casts");
    const auto c=cd_title_options(true,-1,-3.f,8.f,9999,-1);
    check(c.font==0&&c.scale_x==0.05f&&c.scale_y==2.f&&c.x==639&&c.y==0,"finite font scale/position bounds");
}
void font_path(){
    Player p;CdSwitcher cd{true,{{"not-a-title.ogg","Actual title"}},0,&p,Player::play};
    FrontendFontPack pack;auto& f=pack.fonts[2];f.width=8;f.height=8;f.first=32;f.token=7;
    f.metrics.assign(96,{0,8});
    std::vector<FrontendGlyph> glyphs;
    check(!cd_title_glyphs(cd,&pack,0,1,true,glyphs)&&glyphs.empty()&&p.calls==0,"off adds no glyph/audio work");
    cd.title.options=cd_title_options(true,2,0.3f,0.5f,375,450);cd.title.changed();
    check(cd_title_glyphs(cd,&pack,1,1,true,glyphs)&&!glyphs.empty(),"production recovered font producer creates title glyphs");
    check(glyphs.front().token==7&&glyphs.front().x==375.f&&glyphs.front().y==450.f&&glyphs.front().color==0xffffffffu&&glyphs.front().mode==4,"game font/style/priority/location and full alpha preserved");
    check(glyphs.front().scale_x==0.3f&&glyphs.front().scale_y==0.5f&&p.calls==0,"drawing uses configured scale without any audio request");
    const auto count=glyphs.size();
    check(!cd_title_glyphs(cd,&pack,1,1,true,glyphs)&&glyphs.size()==count,"second flush adds no duplicate glyphs");
    check(!cd_title_glyphs(cd,&pack,2,1,false,glyphs)&&cd.title.remaining==0,"ownership loss submits nothing and clears timer");
    cd.title.changed();check(!cd_title_glyphs(cd,nullptr,3,1,true,glyphs)&&cd.title.remaining==119,"missing font ages without new resource/queue path");
    f.metrics.resize(1);cd.title.changed();
    check(!cd_title_glyphs(cd,&pack,4,1,true,glyphs)&&glyphs.size()==count,"unsupported glyph refuses atomically without altering other draws");
    check(cd.tracks[0].path=="not-a-title.ogg"&&cd.tracks[0].title=="Actual title","display never changes path/catalog title");
}
bool inside_hud(const FrontendFont& font,const std::vector<FrontendGlyph>& glyphs){
    return std::all_of(glyphs.begin(),glyphs.end(),[&](const FrontendGlyph& g){
        return g.right-g.left==font.width&&g.x>=0.f&&g.x+float(g.right-g.left)*g.scale_x<=640.f;
    });
}
std::string glyph_text(const FrontendFont& font,const std::vector<FrontendGlyph>& glyphs){
    std::string text;
    for(const auto& g:glyphs)text+=char(font.first+unsigned(g.top/font.height)*16u+unsigned(g.left/font.width));
    return text;
}
void viewport_ellipsis(){
    Player p;CdSwitcher cd{true,{{"unchanged.ogg","100% %s"}},0,&p,Player::play};
    FrontendFontPack pack;auto& f=pack.fonts[2];f.width=32;f.height=32;f.first=32;f.token=7;
    f.metrics.assign(96,{0,32});
    cd.title.options=cd_title_options(true,2,0.3f,0.5f,375,450);
    std::vector<FrontendGlyph> glyphs;
    cd.title.changed();
    check(cd_title_glyphs(cd,&pack,1,1,true,glyphs)&&glyph_text(f,glyphs)=="#01.100%%s"&&inside_hud(f,glyphs),"fitting literal title keeps native glyphs with title-only readable spacing");
    check(glyphs.front().x==375.f&&glyphs.front().y==450.f&&glyphs.front().scale_x==0.3f&&glyphs.front().scale_y==0.5f,"width fitting preserves configured position and scale");
    cd.tracks[0].title=std::string(512,'A');cd.title.changed();glyphs.clear();
    check(cd_title_glyphs(cd,&pack,2,1,true,glyphs)&&glyph_text(f,glyphs).rfind("#01.",0)==0&&glyphs.size()>=3&&glyph_text(f,glyphs).compare(glyphs.size()-3,3,"...")==0&&glyphs.size()<98&&inside_hud(f,glyphs),"long title retains full prefix and three complete dots inside viewport");
    check(cd.tracks[0].title==std::string(512,'A')&&cd.tracks[0].path=="unchanged.ogg"&&p.calls==0,"width ellipsis does not modify catalog or audio");
    cd.title.options=cd_title_options(true,2,1.f,0.5f,396,450);cd.title.changed();glyphs.clear();
    check(cd_title_glyphs(cd,&pack,3,1,true,glyphs)&&glyph_text(f,glyphs)=="#01...."&&glyphs.back().x+32.f==640.f&&inside_hud(f,glyphs),"complete prefix and three dots can end exactly at viewport edge");
    cd.title.options.x=397;cd.title.changed();const auto edge_count=glyphs.size();
    check(!cd_title_glyphs(cd,&pack,4,1,true,glyphs)&&glyphs.size()==edge_count,"one pixel less than the prefix and ellipsis needs refuses without clipped dots");
    // Variable advances, bearings, spacing and kerning use the recovered draw
    // path, rather than assuming cell count times scale is the text width.
    f.metrics.assign(96,{-7,17});f.metrics['.'-32]={0,4};f.spacing_x=1.f;
    f.kerning.assign(96*96,0);f.kerning[std::size_t('A'-32)*96+std::size_t('.'-32)]=3;
    f.kerning[std::size_t('.'-32)*96+std::size_t('.'-32)]=1;
    cd.title.options=cd_title_options(true,2,0.3f,0.5f,375,450);cd.title.changed();glyphs.clear();
    check(cd_title_glyphs(cd,&pack,5,1,true,glyphs)&&glyphs.size()>=3&&glyph_text(f,glyphs).compare(glyphs.size()-3,3,"...")==0&&inside_hud(f,glyphs),"proportional metrics bearings spacing and cut-boundary kerning remain bounded");
    cd.title.options=cd_title_options(true,2,2.f,0.5f,600,450);cd.title.changed();
    const auto kept=glyphs.size();
    check(!cd_title_glyphs(cd,&pack,6,1,true,glyphs)&&glyphs.size()==kept&&cd.title.remaining==119,"insufficient space for prefix and ellipsis refuses atomically while timer ages");
    f.metrics.assign(96,{7,17});f.kerning.clear();cd.title.options=cd_title_options(true,2,1.f,0.5f,0,450);cd.title.changed();
    check(!cd_title_glyphs(cd,&pack,7,1,true,glyphs)&&glyphs.size()==kept,"left-bearing clipping cannot produce a partial prefix");
    std::uint32_t frame=8;
    for(const int width:{8,32,128})for(const float scale:{0.05f,0.3f,1.f,2.f})for(const int x:{0,375,600,639}){
        f.width=std::int16_t(width);f.spacing_x=0;f.metrics.assign(96,{0,std::int8_t(width-1)});
        cd.title.options=cd_title_options(true,2,scale,0.5f,x,450);cd.title.changed();glyphs.clear();
        const bool drawn=cd_title_glyphs(cd,&pack,frame++,1,true,glyphs);
        check(cd.title.remaining==119&&((drawn&&!glyphs.empty()&&inside_hud(f,glyphs))||(!drawn&&glyphs.empty())),"cell/scale/position boundaries submit only complete horizontally bounded glyphs");
    }
}
void readable_spacing(){
    FrontendFont font;font.width=30;font.height=30;font.first=32;font.token=3;
    font.metrics.assign(96,{0,22});font.metrics['.'-32]={11,10};
    font.kerning.assign(96*96,0);font.kerning[std::size_t('.'-32)*96+std::size_t('.'-32)]=1;
    FrontendTextStyle style;style.scale_x=0.3f;style.scale_y=0.5f;
    const FrontendTextCursor origin{375,375,450};
    auto native=origin,readable=origin;
    frontend_text_advance_42c610(font,style,native,' ',0);cd_title_advance(font,style,readable,' ',0);
    check(native.x==377&&readable.x==380,"default word gap survives truncation as half a scaled cell rounded up");
    native=readable=origin;
    frontend_text_advance_42c610(font,style,native,'.','.');cd_title_advance(font,style,readable,'.','.');
    check(native.x==377&&readable.x==379,"period advance includes scaled ink width plus a visible gap");
    native=readable=origin;
    frontend_text_advance_42c610(font,style,native,'A','.');cd_title_advance(font,style,readable,'A','.');
    check(readable.x==383&&native.x==381,"letter before period gets the same explicit gap used by fitting");
    std::vector<FrontendGlyph> dots,words,letters;
    check(cd_title_layout(font,style,origin,"...",dots)&&dots.size()==3&&dots[1].x-dots[0].x==4.f&&dots[2].x-dots[1].x==4.f,"three distinct period origins use the shared title layout");
    check(cd_title_layout(font,style,origin,"A B",words)&&cd_title_layout(font,style,origin,"AB",letters)&&words.size()==2&&letters.size()==2&&words[1].x-letters[1].x==5.f,"word space remains distinct in submitted native glyph positions");
    std::vector<FrontendGlyph> native_draws;
    check(frontend_text_draw(font,style,origin,"...",native_draws)&&native_draws.size()==3&&native_draws[1].x-native_draws[0].x==2.f,"global frontend font spacing remains unchanged");
    std::vector<FrontendGlyph> fitted;
    check(cd_title_draw_bounded(font,style,origin,0,std::string(512,'A'),fitted)&&fitted.size()>=3&&inside_hud(font,fitted)&&fitted.back().x-fitted[fitted.size()-2].x==4.f&&fitted[fitted.size()-2].x-fitted[fitted.size()-3].x==4.f,"width fit and final ellipsis submission share the same readable spacing");
}
}
int main(){timing();transaction();labels_and_options();font_path();viewport_ellipsis();readable_spacing();std::printf("CD title overlay: %u checks PASS (synthetic font/timer/ownership only)\n",checks);}
