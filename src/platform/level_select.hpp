#pragma once
#include "driving/pc_driving.hpp"
#include <array>
#include <cstdint>

namespace outrun::platform {
// Tweaks' 445756 menu key and the native 4E7800 screen control. No renderer,
// window input polling or generated-code patch is needed.
inline constexpr std::uint32_t level_select_initial_key(bool enabled){return enabled?0x1bu:0u;}
// The desktop's normalized DIK snapshot, not an asynchronous/global key poll.
// Either Shift must actually be held; a low transition bit is not a press.
inline bool level_select_shift_sample(bool enabled,bool blocked,const std::array<std::uint8_t,256>& keys){
    return enabled&&!blocked&&((keys[0x2au]|keys[0x36u])&0x80u)!=0u;
}
struct LevelSelectServices {
    void* user{};
    std::uint32_t (*action)(void*){};              // inherited slot +14(1)
    std::uint32_t (*switch_now)(void*,std::uint32_t){}; // 4536C0(mask)
    bool (*special_course)(void*,unsigned,unsigned){}; // table 4E7920 / 4E7950 entry == 0
    void (*commit)(void*,std::int32_t,bool){};     // course globals, owner 440DF0
};
inline std::uint32_t level_select_update_4e7800(driving::Bytes object,bool enabled,
                                               const LevelSelectServices& s,bool shift_held=false){
    // Validate storage and services before a widget or input callback can run.
    object.check(0x130e4u,1u);
    if(!s.action||!s.switch_now||!s.special_course||!s.commit)
        throw std::invalid_argument("level select: incomplete services");
    const auto row=object.i32(0x34u);
    // PC interleaves the first 60 rows into two columns, then six special rows.
    const auto course=row<60?(row%2)*30+row/2:row;
    const auto action=s.action(s.user);
    // The added LS alias is local to this screen, like Tweaks' 4E7838 hook.
    const auto held=s.switch_now(s.user,enabled?0x110u:0x10u);
    const auto table_index=std::uint32_t(course)-10u;
    const bool in_table=table_index<=19u;
    const bool alternate=(held||(enabled&&shift_held))&&in_table&&s.special_course(s.user,0u,table_index);
    object.put8(0x130e4u,std::uint8_t(alternate));
    switch(action){
    case 0u:{
        const bool secondary=in_table&&s.special_course(s.user,1u,table_index)&&alternate;
        s.commit(s.user,course,secondary);
        object.put32(4u,0x1cu);return 1u;
    }
    case 1u:return 2u;
    case 2u:{const auto current=object.i32(0x34u);object.put32(0x34u,current>0?std::uint32_t(current)-1u:65u);break;}
    case 4u:{const auto current=object.i32(0x34u);object.put32(0x34u,current<65?std::uint32_t(current)+1u:0u);break;}
    default:break;
    }
    return 0u;
}
}
