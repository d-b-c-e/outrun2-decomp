#pragma once
#include "panel_navigation.hpp"
#include "imgui.h"
#include "imgui_internal.h"
namespace outrun::desktop {
inline void panel_navigation_imgui(const PanelNavigation::Batch& batch){
    constexpr ImGuiKey keys[]{ImGuiKey_GamepadDpadUp,ImGuiKey_GamepadDpadDown,ImGuiKey_GamepadDpadLeft,
        ImGuiKey_GamepadDpadRight,ImGuiKey_GamepadFaceDown,ImGuiKey_GamepadFaceRight};
    auto& io=ImGui::GetIO();auto& g=*ImGui::GetCurrentContext();
    if(batch.reset){
        // Pinned ImGui 1.91.6: cancel only our six keys, including trickled events.
        // ClearInputKeys/ClearEventsQueue would also discard the owner's keyboard/mouse.
        for(int i=g.InputEventsQueue.Size-1;i>=0;--i){
            const auto& event=g.InputEventsQueue[i];bool owned=false;
            if(event.Type==ImGuiInputEventType_Key)for(auto key:keys)owned=owned||event.Key.Key==key;
            if(owned)g.InputEventsQueue.erase(g.InputEventsQueue.Data+i);
        }
        for(auto key:keys){auto* data=ImGui::GetKeyData(key);data->Down=false;data->DownDuration=data->DownDurationPrev=-1.f;data->AnalogValue=0.f;}
        if(g.ActiveIdSource==ImGuiInputSource_Gamepad)ImGui::ClearActiveID();
    }
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableGamepad;
    if(batch.active)io.BackendFlags|=ImGuiBackendFlags_HasGamepad;
    else io.BackendFlags&=~ImGuiBackendFlags_HasGamepad;
    const auto submit=[&](unsigned held){for(unsigned i=0;i<6;++i)io.AddKeyEvent(keys[i],(held&(1u<<i))!=0);};
    if(batch.active){for(unsigned i=0;i<batch.count;++i)submit(batch.states[i]);if(!batch.count)submit(batch.held);}
}
}
