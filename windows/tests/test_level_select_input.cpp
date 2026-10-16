// The actual desktop publisher with a synthetic normalized keyboard and backend.
// No window, OS key polling, device, renderer or installed game is used.
#include "level_select_input.hpp"
#include "desktop_keyboard.hpp"
#include "native_window.hpp"
#include "platform/runtime_input.hpp"
#include <cstdio>
#include <cstdlib>
namespace {
unsigned checks{},backend_calls{};bool panel_open{};
void check(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"level input FAIL: %s\n",what);std::exit(1);}}
void backend(outrun::platform::NativeInputState& out,const outrun::platform::NativeRuntimeContext*){
    ++backend_calls;out.connected=true;out.pc_device.buttons_04=0x100008u;
    out.frontend.feature_mask=0x4567u;out.raw_buttons_held=0x9876u;
    out.steering=-45;out.horn=true;
}
}
namespace outrun::xbox_runtime {
InputFilter& input_filter(){static InputFilter filter=nullptr;return filter;}
}
namespace outrun::desktop { bool desktop_panel_open(){return panel_open;} }
int main(){
    using namespace outrun;
    using namespace outrun::desktop;
    for(const bool enabled:{false,true})for(const bool test_or_playback:{false,true}){
        xbox_runtime::input_filter()=backend;desktop_keys_clear();panel_open=false;
        level_select_input_configure(enabled,test_or_playback);level_select_wrap_input_filter();
        const auto filter=xbox_runtime::input_filter();
        check((filter!=backend)==(enabled&&!test_or_playback),"only opted-in ordinary/recording processes wrap the backend");
        level_select_wrap_input_filter();check(xbox_runtime::input_filter()==filter,"wrapping twice never creates a recursive chain");
        const auto sample=[&](){platform::NativeInputState out{};const auto before=backend_calls;filter(out,nullptr);
            check(backend_calls==before+1u,"backend runs exactly once");
            check(out.connected&&out.pc_device.buttons_04==0x100008u&&out.frontend.feature_mask==0x4567u&&
                  out.raw_buttons_held==0x9876u&&out.steering==-45&&out.horn,"original normalized signals stay intact");return out;};
        check(!sample().level_select_shift,"no key starts released");
        desktop_keys()[0x2a]=0x80;
        check(sample().level_select_shift==(enabled&&!test_or_playback),"left Shift reaches only an admitted live publisher");
        desktop_keys()[0x2a]=0;desktop_keys()[0x36]=0x80;
        check(sample().level_select_shift==(enabled&&!test_or_playback),"right Shift is equivalent");
        panel_open=true;check(!sample().level_select_shift,"F6 owner blocks the modifier");panel_open=false;
        desktop_keys_clear();check(!sample().level_select_shift,"focus-loss clear releases Shift immediately");
        desktop_keys()[0x2a]=1;desktop_keys()[0x36]=1;
        check(!sample().level_select_shift,"low transition bits are not a held key");
        desktop_keys()[0x1d]=0x80;check(!sample().level_select_shift,"Control is not the Shift alias");
    }
    xbox_runtime::input_filter()=nullptr;desktop_keys_clear();
    std::printf("Level input: %u checks PASS (synthetic keyboard/backend only)\n",checks);
}
