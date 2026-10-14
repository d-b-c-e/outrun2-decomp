#include "ffb_shadow_live.hpp"
#include "ffb_shadow.hpp"
#include "desktop_panel.hpp"
#include "platform/native_runtime.hpp"
#include "native_window.hpp"
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <memory>

namespace outrun::desktop {
namespace {
struct Live {
    HANDLE file{INVALID_HANDLE_VALUE};
    std::unique_ptr<ffb::shadow::Core> core;
    xbox_runtime::InputFilter inner{};
    bool panel_open{},failure_logged{},ordinary_admitted{},selected{};
    ffb::producer2::Strengths strengths{};
} live;
void observe(void*,const platform::NativeCarProducerObservation& o);
void register_observer(){
    xbox_runtime::car_producer_observer()={nullptr,observe,&live.ordinary_admitted};
}
bool write(const std::string& bytes){
    if(live.file==INVALID_HANDLE_VALUE||bytes.size()>MAXDWORD)return false;
    DWORD n=0;
    return WriteFile(live.file,bytes.data(),DWORD(bytes.size()),&n,nullptr)&&n==bytes.size();
}
void close_trace(){
    if(live.file==INVALID_HANDLE_VALUE)return;
    (void)FlushFileBuffers(live.file); // once at detach/shutdown, never on the game tick
    CloseHandle(live.file);live.file=INVALID_HANDLE_VALUE;
}
void note_failure(){
    if(live.failure_logged)return;
    live.failure_logged=true;
    live.ordinary_admitted=false;
    close_trace();
    std::printf("ffb shadow: optional trace detached after observer or file failure\n");std::fflush(stdout);
}
void observe(void*,const platform::NativeCarProducerObservation& o){
    if(!live.core||!live.core->active())return;
    ffb::shadow::Gate g{};
    g.mode=o.mode;g.generation=o.car_generation;g.continuity=o.continuity;g.attempt=o.attempt;
    g.frame_counter=o.frame_counter;g.update_index=o.update_index;
    g.car_active=o.car_active;g.panel_open=live.panel_open;g.recovery_pending=o.recovery_pending;
    ffb::shadow::Sample sample{};sample.gate=g;sample.producer_fresh=o.producer_fresh;
    sample.car=o.car;sample.car_bytes=o.car_bytes;
    live.core->observe(sample);
    if(live.core->trace_failed()||!live.core->active())note_failure();
}
void filter(platform::NativeInputState& out,const platform::NativeRuntimeContext* rt){
    live.panel_open=desktop_panel_open();
    if(live.core&&live.core->active()){
        ffb::shadow::Gate g{};
        if(rt){
            const auto& car=rt->event_function36.car_select;
            g.mode=rt->mode_state.current;g.paused=rt->event_state.pause_depth>0u;
            g.generation=car.race_inits;g.continuity=car.producer_continuity;g.attempt=car.producer_attempt;
            g.car_active=car.producer_active;g.recovery_pending=car.producer_recovery_pending;
            g.frame_counter=rt->frame_state.frame_counter_95af0c;
            g.update_index=rt->frame_state.update_index_8a8cdc;
        }
        g.panel_open=live.panel_open;
        live.core->before_frame(g);
        if(live.core->trace_failed()||!live.core->active())note_failure();
    }
    if(live.inner)live.inner(out,rt);
}
}
bool ffb_shadow_select(const ffb::player::Selection& selection,ffb::shadow::Core::RequestSink sink){
    if(selection.model!=ffb::player::Model::Producer2||!sink||live.core)return false;
    const auto& s=selection.strengths;
    if(s.steering_percent<0||s.steering_percent>100||s.shift_percent<0||s.shift_percent>100)return false;
    try{live.core=std::make_unique<ffb::shadow::Core>(nullptr,s,std::move(sink));}
    catch(...){return false;}
    live.selected=true;live.strengths=s;register_observer();
    return true;
}
void ffb_shadow_invalidate_selected() noexcept {if(live.selected&&live.core)live.core->invalidate();}
bool ffb_shadow_configure(const std::wstring& path,bool ordinary_allowed){
    live.ordinary_admitted=false;
    if(path.empty())return false;
    if(!ordinary_allowed){std::printf("ffb shadow: refused during recording\n");return false;}
    if(!std::filesystem::path(path).is_absolute()){
        std::printf("ffb shadow: refused non-absolute trace path\n");return false;
    }
    live.file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(live.file==INVALID_HANDLE_VALUE){std::printf("ffb shadow: trace create-new failed (%lu)\n",GetLastError());return false;}
    if(!write(ffb::shadow::Core::header(live.strengths,live.selected))){
        std::printf("ffb shadow: trace header write failed\n");close_trace();return false;
    }
    if(live.core)live.core->set_writer(write);
    else try{live.core=std::make_unique<ffb::shadow::Core>(write);}
    catch(...){close_trace();return false;}
    live.ordinary_admitted=true;
    if(!live.selected)register_observer();
    std::printf("ffb shadow: producer@2 software trace armed (%s)\n",
        live.selected?"selected request shares observer":"no wheel output");std::fflush(stdout);
    return true;
}
void ffb_shadow_wrap_input_filter(){
    if(!live.core)return;
    auto& current=xbox_runtime::input_filter();
    if(current==filter)return;
    live.inner=current;current=filter;
}
void ffb_shadow_shutdown(){
    live.ordinary_admitted=false;
    xbox_runtime::car_producer_observer()={};
    if(live.core)live.core->stop();live.core.reset();
    close_trace();
    live.selected=false;live.strengths={};live.failure_logged=false;
}
}
