#pragma once
namespace outrun::desktop {
struct WheelShortcutEdges { bool menu{},mute{}; };
class WheelShortcuts {
    unsigned previous_{};
public:
    WheelShortcutEdges step(bool menu,bool mute,bool focused){
        const unsigned held=(menu?1u:0u)|(mute?2u:0u);
        const unsigned down=held&~previous_;
        previous_=held; // A press begun outside the game must release before it can act.
        return {focused&&(down&1u)!=0,focused&&(down&2u)!=0};
    }
};
}
