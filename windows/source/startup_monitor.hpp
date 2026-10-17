#pragma once
// MonitorDevicePath is an opaque CCD target identity, never an enumeration index.
// Pure selection/anchor rules; the Windows adapter only reads the active topology.
#include "triple_screens.hpp"
#include <algorithm>
#include <optional>
#include <string>
#include <vector>
namespace outrun::desktop {
struct StartupMonitor {
    ScreenRect bounds{},work{};
    std::vector<std::string> paths; // multiple active targets on a cloned source are ambiguous
    std::string label;
};
inline bool monitor_path_equal(const std::string& a,const std::string& b){
    if(a.size()!=b.size())return false;
    for(std::size_t n=0;n<a.size();++n){
        const auto fold=[](unsigned char c){return c>='A'&&c<='Z'?c+('a'-'A'):c;};
        if(fold(static_cast<unsigned char>(a[n]))!=fold(static_cast<unsigned char>(b[n])))return false;
    }
    return true;
}
struct StartupMonitorSelection { std::optional<std::size_t> index;std::string note; };
inline StartupMonitorSelection select_startup_monitor(const std::vector<StartupMonitor>& monitors,
                                                     const std::string& saved,bool enabled=true){
    if(!enabled)return {{},saved.empty()?"":"saved gameplay display ignored in a test session"};
    if(saved.empty())return {};
    std::optional<std::size_t> found;
    for(std::size_t n=0;n<monitors.size();++n){
        const auto& m=monitors[n];
        for(const auto& path:m.paths)if(monitor_path_equal(path,saved)){
            if(found||m.paths.size()!=1u)return {{},"saved gameplay display is ambiguous; default placement applies"};
            if(m.bounds.right<=m.bounds.left||m.bounds.bottom<=m.bounds.top||
               m.work.right<=m.work.left||m.work.bottom<=m.work.top)
                return {{},"saved gameplay display has no usable rectangle; default placement applies"};
            found=n;
        }
    }
    if(!found)return {{},"saved gameplay display is unavailable; default placement applies"};
    return {found,"selected gameplay display: "+monitors[*found].label};
}
inline bool startup_monitor_span(const std::vector<StartupMonitor>& monitors,std::size_t center,
                                 ScreenRect& span,std::string* reason=nullptr){
    if(center>=monitors.size())return false;
    std::vector<ScreenRect> rects;rects.reserve(monitors.size());
    for(std::size_t n=0;n<monitors.size();++n){auto r=monitors[n].bounds;r.primary=n==center;rects.push_back(r);}
    return triple_span_from_monitors(rects,span,reason); // unchanged shared geometry, local anchor only
}
inline ScreenRect center_startup_window(const ScreenRect& work,long width,long height){
    width=std::min(width,work.right-work.left);height=std::min(height,work.bottom-work.top);
    const auto x=work.left+(work.right-work.left-width)/2,y=work.top+(work.bottom-work.top-height)/2;
    return {x,y,x+width,y+height};
}
struct StartupMonitorState {
    std::vector<StartupMonitor> monitors;
    StartupMonitorSelection selection;
    const StartupMonitor* selected()const{return selection.index?&monitors[*selection.index]:nullptr;}
};
// Once before startup resolution/window creation; no monitor changes or polling.
void configure_startup_monitor(const std::string& saved,bool enabled);
const StartupMonitorState& startup_monitor_state();
unsigned selected_monitor_render_height(bool borderless);
}
