// CPU-only: real pinned ImGui widgets, no window/backend/OS input/device.
#include "panel_navigation_imgui.hpp"
#include <cstdio>
#include <cstdlib>
using namespace outrun::desktop;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"navigation:%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
    constexpr unsigned confirm=16,right=8,down=2;
    PanelNavigation relay;
    relay.update(confirm,true);auto b=relay.drain();CHECK(b.active&&b.reset&&b.held==0&&b.count==0);
    relay.update(0,true);relay.update(confirm,true);relay.update(0,true);b=relay.drain();
    CHECK(b.count==2&&b.states[0]==confirm&&b.states[1]==0);
    for(unsigned reason=0;reason<4;++reason){ // close, focus, binding edit, disabled session share this gate
        relay.update(confirm,true);relay.update(confirm,false);b=relay.drain();CHECK(!b.active&&b.reset&&b.count==0);
        relay.update(confirm,true);CHECK(relay.drain().held==0);relay.update(0,true);relay.update(right,true);
        CHECK(relay.drain().held==right);relay.update(0,true);relay.drain();
    }
    for(unsigned i=0;i<65;++i)relay.update(i%2?0:confirm,true);
    b=relay.drain();CHECK(b.reset&&b.held==0&&b.count==0);
    relay.update(0,true);relay.update(confirm,true);CHECK(relay.drain().held==confirm);
    relay.block();relay.drain();
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;io.DisplaySize={800,600};io.DeltaTime=1.f/60;
    unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    CHECK(pixels&&width>0&&height>0);
    int activations=0;float slider=.5f;ImGuiID button_id=0,slider_id=0;
    const auto frame=[&]{
        panel_navigation_imgui(relay.drain());ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({500,400});ImGui::Begin("Fixture",nullptr,ImGuiWindowFlags_NoSavedSettings);
        button_id=ImGui::GetID("Save");if(ImGui::Button("Save"))++activations;ImGui::SetItemDefaultFocus();
        slider_id=ImGui::GetID("FOV");ImGui::SliderFloat("FOV",&slider,0.f,1.f);ImGui::End();ImGui::Render();
    };
    relay.update(0,true);frame();frame();frame();
    relay.update(1,true);frame();relay.update(0,true);frame();frame(); // show normal gamepad navigation highlight
    CHECK(GImGui->NavId==button_id&&GImGui->NavCursorVisible);
    relay.update(confirm,true);frame();relay.update(0,true);frame();frame();CHECK(activations==1);
    relay.update(confirm,true);for(unsigned i=0;i<45;++i)frame();CHECK(activations==2); // ordinary Button has no held repeat
    relay.update(0,true);frame();
    // Two complete taps between render frames survive the relay and ImGui's trickle queue.
    relay.update(confirm,true);relay.update(0,true);relay.update(confirm,true);relay.update(0,true);
    for(unsigned i=0;i<8;++i)frame();CHECK(activations==4);
    // Real navigation moves to and edits the real slider.
    relay.update(down,true);frame();relay.update(0,true);frame();frame();CHECK(GImGui->NavId==slider_id);
    relay.update(confirm,true);frame();relay.update(0,true);frame();relay.update(right,true);for(unsigned i=0;i<3;++i)frame();
    CHECK(slider>.5f);relay.update(0,true);frame();
    // Pending gamepad activation is cancelled; unrelated queued keyboard and mouse survive.
    io.AddKeyEvent(ImGuiKey_A,true);io.AddMouseButtonEvent(0,true);io.AddKeyEvent(ImGuiKey_GamepadFaceDown,true);
    relay.block();panel_navigation_imgui(relay.drain());
    bool keyboard=false,mouse=false,gamepad=false;
    for(const auto& e:GImGui->InputEventsQueue){
        keyboard=keyboard||(e.Type==ImGuiInputEventType_Key&&e.Key.Key==ImGuiKey_A);
        mouse=mouse||(e.Type==ImGuiInputEventType_MouseButton&&e.MouseButton.Button==0);
        gamepad=gamepad||(e.Type==ImGuiInputEventType_Key&&e.Key.Key==ImGuiKey_GamepadFaceDown);
    }
    CHECK(keyboard&&mouse&&!gamepad);CHECK(!ImGui::GetKeyData(ImGuiKey_GamepadFaceDown)->Down);
    const int before=activations;relay.update(confirm,true);for(unsigned i=0;i<3;++i)frame();CHECK(activations==before);
    CHECK(ImGui::GetKeyData(ImGuiKey_A)->Down&&io.MouseDown[0]);
    ImGui::DestroyContext();std::printf("panel navigation: %u checks PASS\n",checks);
}
