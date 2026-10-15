#include "platform/frontend_categories.hpp"
#include "platform/pc_screen.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
using namespace outrun::platform;
using outrun::driving::Bytes;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
    FrontendCategoryState c;FrontendSprites pool;FrontendUiResources ui{pool};
    CHECK(pool.bind_timing(0x44,std::vector<FrontendSpriteTiming>(224,{240,60})));
    std::array<std::uint8_t,0xe00> root{};Bytes r(root.data(),root.size());
    PcLicense profile;frontend_license_reset_4471a0(profile,1);
    LicenseProgressTables tables;tables.categories_ready=true;tables.category_counts.fill(5);
    FrontendInputSnapshot input;unsigned repeat{},result{};std::vector<unsigned> effects;
    ui.effect_user=&effects;ui.effect_4249f0=[](void* p,unsigned id){static_cast<std::vector<unsigned>*>(p)->push_back(id);return true;};
    FrontendCategoryServices s{ui,r,profile,tables,input,repeat};
    CHECK(frontend_categories_construct_4e8130(c,repeat));
    c.selection_84b7f4=6;c.selection_84b7f8=7;c.query_83639c=8;
    CHECK(frontend_categories_init_4e8760(c,s));CHECK(c.selection_84b7f4==0&&c.selection_84b7f8==0&&c.query_83639c==0);
    const auto tick=[&](unsigned mask){input.feature_mask=mask;CHECK(frontend_categories_control_4e8780(c,s,result));};
    tick(0x2000);tick(0);CHECK(c.selected_84b7f0==1);
    tick(4);CHECK(result==0&&effects.back()==3);tick(0);
    for(unsigned group=0;group<7;++group){bool open{};CHECK(frontend_category_unlocked_4e8410(profile,tables,group,open));CHECK(open==(group==0||group==4));}
    tick(8);CHECK(result==2);tick(0);
    for(unsigned grade=0;grade<8;++grade){
        for(unsigned i=0;i<200;++i)profile[0x1dd+i]=std::uint8_t(grade);
        tables.championship_present.fill(true);for(auto& bank:tables.championship_types)for(unsigned i=0;i<15;++i)bank[i]=i;
        for(unsigned i=0;i<132;++i)profile[0x36e + i]=std::uint8_t(grade|(grade<<4));
        for(unsigned group=0;group<7;++group){int got{};CHECK(frontend_group_grade_4e82a0(profile,tables,group,got)&&got==int(grade));
            c.selected_84b7f0=group;r.put32(0x20c,0xffffffff);tick(4);
            const bool open=group==0||group==4||(grade>=4&&grade!=7);
            CHECK(result==unsigned(open));
            if(open){CHECK(Bytes(c.object.data(),c.object.size()).u32(4)==(group<4?0x3b:0x3c));CHECK(r.u32(0x20c)==(0xfffff8ff|(group<<8)));}
            tick(0);tick(8);CHECK(result==2&&r.u32(0x20c)==0xffe008ff);tick(0);
        }
    }
    {   // EnableHollyCourse2: group 7 grades the fourth lane and unlocks per mode; 0 keeps the PC's seven groups.
        for(unsigned lane=0;lane<4;++lane)for(unsigned i=0;i<15;++i)tables.championship_types[lane][i]=lane*15+i;
        tables.championship_present.fill(true);
        const auto lane_grade=[&](unsigned lane,unsigned g){for(unsigned i=0;i<15;++i){const unsigned type=lane*15+i;
            profile[0x36e+type*2]=profile[0x36f+type*2]=std::uint8_t(g|(g<<4));}};
        const auto unlocked=[&](unsigned group){bool open{};CHECK(frontend_category_unlocked_4e8410(profile,tables,group,open));return open;};
        lane_grade(0,7);lane_grade(1,4);lane_grade(2,3);lane_grade(3,2);             // groups 4..7: none, A, B, C
        for(int mode=1;mode<=3;++mode){g_pc_enable_holly_course2=mode;int got{};
            CHECK(frontend_group_grade_4e82a0(profile,tables,7,got)&&got==2);
            CHECK(unlocked(6)&&unlocked(7)==(mode==1));}   // mode 1 opens as group 6 (group 5's A); 2 and 3 need Holly 1's A
        lane_grade(2,4);
        for(int mode=1;mode<=3;++mode){g_pc_enable_holly_course2=mode;CHECK(unlocked(7));}
        tables.championship_present[3]=false;                                      // no valid MIX 2 lane
        for(int mode=1;mode<=3;++mode){g_pc_enable_holly_course2=mode;int got{};
            CHECK(frontend_group_grade_4e82a0(profile,tables,7,got)&&got==7&&!unlocked(7));}
        tables.championship_present[3]=true;g_pc_enable_holly_course2=0;
        {int got{};CHECK(!frontend_group_grade_4e82a0(profile,tables,7,got));}CHECK(!unlocked(7));
        g_pc_enable_holly_course2=3;
    }
    {   // EnableHollyCourse2's racer menu (Tweaks' patches): the eighth tile and the bottom frame at +674, 5CE400's edits,
        // the texts past the seven-entry tables and group 7's mode bits. Synthetic texts: string i reads "#" and i in hex;
        // a 1x1 font spells each glyph's character in its cell (left = low nibble, top = high nibble).
        constexpr unsigned Texts=0x398;
        std::vector<std::uint8_t> raw(8+4*(Texts+1),0);
        const auto put=[&](std::size_t at,std::uint32_t v){for(unsigned k=0;k<4;++k)raw[at+k]=std::uint8_t(v>>(8*k));};
        put(0,0x74657874);
        for(unsigned i=0;i<Texts;++i){char id[8];std::snprintf(id,sizeof id,"#%X",i);put(8+4*i,unsigned(raw.size()));
            for(const char* p=id;*p;++p){raw.push_back(std::uint8_t(*p));raw.push_back(0);}raw.push_back(0);raw.push_back(0);}
        put(4,unsigned(raw.size()));
        FrontendTextTable text;CHECK(text.parse(raw.data(),raw.size()));
        FrontendFontPack fonts;fonts.fonts[9].width=1;fonts.fonts[9].height=1;s.fonts=&fonts;s.text=&text;
        const auto object=[&]{return Bytes(c.object.data(),c.object.size());};
        const auto sprite=[&](unsigned off){return pool.get(object().u32(off+8));};
        const auto shown=[&]{std::string t;for(const auto& g:c.glyphs)t.push_back(char(g.top*16+g.left));return t;};
        const auto press=[&](unsigned mask){tick(mask);tick(0);};
        const auto at=[&](unsigned from,unsigned mask){c.selected_84b7f0=from;press(mask);return c.selected_84b7f0;};
        const auto lane_grade=[&](unsigned lane,unsigned g){for(unsigned i=0;i<15;++i){const unsigned type=lane*15+i;
            profile[0x36e+type*2]=profile[0x36f+type*2]=std::uint8_t(g|(g<<4));}};
        constexpr unsigned Up=0x400,Down=0x800,Left=0x1000,Right=0x2000;
        lane_grade(0,7);lane_grade(1,4);lane_grade(3,2);                            // groups 4..7: none, A, (Holly 1), C
        // Mode 3: the PC's 5CE400 and seven tiles until group 7 first unlocks; then the edits follow its state.
        g_pc_enable_holly_course2=3;lane_grade(2,3);c.holly2_nav_patched=c.holly2_nav_unlocked=false;
        c.selected_84b7f0=0;CHECK(frontend_categories_display_4e8c60(c,s));
        CHECK(object().u32(0x494+8)==~0u&&!c.holly2_nav_patched);                 // no eighth tile while locked
        CHECK(sprite(0x674)&&sprite(0x674)->token==0x4400b4&&sprite(0x674)->first==0.f);
        CHECK(at(0,Left)==0&&at(3,Down)==3&&at(3,Right)==3&&at(6,Right)==6&&at(4,Left)==4);
        lane_grade(2,4);c.selected_84b7f0=0;CHECK(frontend_categories_display_4e8c60(c,s));   // Holly 1 reaches A
        CHECK(c.holly2_nav_patched&&c.holly2_nav_unlocked&&sprite(0x494)->token==0x4400b7&&sprite(0x494)->first==15.f);
        CHECK(at(3,Down)==7&&at(6,Right)==7&&at(4,Left)==7&&at(7,Right)==4&&at(0,Left)==3&&at(3,Right)==0);
        lane_grade(2,3);c.selected_84b7f0=7;CHECK(frontend_categories_display_4e8c60(c,s));   // locked again (license switch)
        CHECK(c.holly2_nav_patched&&!c.holly2_nav_unlocked);
        CHECK(c.selected_84b7f0==6&&sprite(0x5d4)->first==180.f&&sprite(0x5d4)->last==209.f);   // hidden 7 -> Holly 1, its list
        CHECK(at(3,Down)==3&&at(6,Right)==4&&at(4,Left)==6&&at(0,Left)==3&&at(3,Right)==0);
        // Mode 1: always eight tiles; group 7 opens with Holly 1 (group 5's A).
        g_pc_enable_holly_course2=1;
        CHECK(at(0,Left)==3&&at(3,Down)==7&&at(7,Right)==4&&at(4,Left)==7&&at(7,Left)==6&&at(6,Right)==7&&at(7,Up)==3);
        c.selected_84b7f0=7;CHECK(frontend_categories_display_4e8c60(c,s));
        CHECK(sprite(0x494)->first==15.f&&sprite(0x534)->token==0x4400cb&&sprite(0x534)->first==4.f&&sprite(0x674)->first==7.f);
        CHECK(shown()=="#38FC");                                                    // Holly 1's normal text, rank C
        lane_grade(3,6);CHECK(frontend_categories_display_4e8c60(c,s)&&shown()=="#394AAA");   // 0x394 when complete
        r.put32(0x20c,0x12345678);tick(4);
        CHECK(result==1&&effects.back()==64&&object().u32(4)==0x3c&&r.u32(0x20c)==((0x12345678u&~0x700u)|0x700u));tick(0);
        tick(8);CHECK(result==2);tick(0);
        lane_grade(1,3);CHECK(frontend_categories_display_4e8c60(c,s));             // group 7 locked in mode 1
        CHECK(sprite(0x494)->first==14.f&&sprite(0x534)->first==5.f&&sprite(0x674)->first==12.f&&shown()=="#396");
        tick(4);CHECK(result==0&&effects.back()==3);tick(0);
        CHECK(frontend_categories_suspend_4e8a30(c,s));for(unsigned layer=0;layer<21;++layer)CHECK(pool.used(layer)==0);
        // Mode 0: the PC's seven groups, the bottom frame at +494, no group 7.
        g_pc_enable_holly_course2=0;c.selected_84b7f0=0;
        CHECK(frontend_categories_construct_4e8130(c,repeat)&&frontend_categories_init_4e8760(c,s)&&frontend_categories_display_4e8c60(c,s));
        CHECK(sprite(0x494)->token==0x4400b4&&sprite(0x3f4)->token==0x4400b7);   // the suspend below proves +674 unused
        CHECK(at(0,Left)==0&&at(3,Down)==3&&at(6,Right)==6&&at(4,Left)==4);
        c.selected_84b7f0=7;CHECK(!frontend_categories_control_4e8780(c,s,result)&&s.missing==0x4e8780);
        c.selected_84b7f0=0;CHECK(frontend_categories_suspend_4e8a30(c,s));for(unsigned layer=0;layer<21;++layer)CHECK(pool.used(layer)==0);
        g_pc_enable_holly_course2=3;lane_grade(1,4);s.fonts=nullptr;s.text=nullptr;
        CHECK(frontend_categories_construct_4e8130(c,repeat)&&frontend_categories_init_4e8760(c,s));
    }
    CHECK(frontend_categories_suspend_4e8a30(c,s));for(unsigned layer=0;layer<21;++layer)CHECK(pool.used(layer)==0);
    c.selected_84b7f0=0;CHECK(frontend_categories_construct_4e8130(c,repeat));CHECK(frontend_categories_init_4e8760(c,s));
    ui.effect_4249f0=nullptr;input.feature_mask=0x2000;
    CHECK(!frontend_categories_control_4e8780(c,s,result)&&s.missing==0x4249f0);
    std::puts("category unlocks, selected mode bits, navigation, rejection sound and resource lifecycle pass");
}
