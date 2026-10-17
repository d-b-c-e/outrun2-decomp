#include "startup_monitor.hpp"
#include <windows.h>
#include <cstdio>
namespace outrun::desktop {
namespace {
StartupMonitorState state;
std::string utf8(const wchar_t* value){
    const int size=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);
    if(size<=1)return {};
    std::string text(std::size_t(size),'\0');
    if(!WideCharToMultiByte(CP_UTF8,0,value,-1,text.data(),size,nullptr,nullptr))return {};
    text.pop_back();return text;
}
struct Scan { std::vector<StartupMonitor> monitors;std::vector<std::wstring> sources;bool failed{}; };
BOOL CALLBACK add(HMONITOR monitor,HDC,LPRECT,LPARAM arg){
    auto& scan=*reinterpret_cast<Scan*>(arg);MONITORINFOEXW info{};info.cbSize=sizeof info;
    if(!GetMonitorInfoW(monitor,&info)){scan.failed=true;return FALSE;}
    const auto rect=[](RECT r,bool primary){return ScreenRect{r.left,r.top,r.right,r.bottom,primary};};
    const bool primary=(info.dwFlags&MONITORINFOF_PRIMARY)!=0;
    scan.monitors.push_back({rect(info.rcMonitor,primary),rect(info.rcWork,primary),{},utf8(info.szDevice)});
    scan.sources.emplace_back(info.szDevice);return TRUE;
}
bool identities(Scan& scan){
    // Bounded retries if the topology changes between buffer sizing and query.
    for(unsigned attempt=0;attempt<3u;++attempt){
        UINT32 path_count=0,mode_count=0;
        if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&path_count,&mode_count)!=ERROR_SUCCESS)return false;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
        const LONG result=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&path_count,paths.data(),&mode_count,modes.data(),nullptr);
        if(result==ERROR_INSUFFICIENT_BUFFER)continue;
        if(result!=ERROR_SUCCESS)return false;
        for(UINT32 n=0;n<path_count;++n){
            const auto& path=paths[n];
            if(path.sourceInfo.modeInfoIdx>=mode_count)return false;
            const auto& mode=modes[path.sourceInfo.modeInfoIdx];
            if(mode.infoType!=DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)return false;
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof source,path.sourceInfo.adapterId,path.sourceInfo.id};
            DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
            target.header={DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,sizeof target,path.targetInfo.adapterId,path.targetInfo.id};
            if(DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS||
               DisplayConfigGetDeviceInfo(&target.header)!=ERROR_SUCCESS||!target.monitorDevicePath[0])return false;
            unsigned matches=0;
            for(std::size_t m=0;m<scan.sources.size();++m)if(_wcsicmp(scan.sources[m].c_str(),source.viewGdiDeviceName)==0){
                ++matches;auto& monitor=scan.monitors[m];monitor.paths.push_back(utf8(target.monitorDevicePath));
                const auto& rect=monitor.bounds;const auto& dimensions=mode.sourceMode;
                if(dimensions.position.x!=rect.left||dimensions.position.y!=rect.top||
                   dimensions.width!=unsigned(rect.right-rect.left)||dimensions.height!=unsigned(rect.bottom-rect.top))return false;
                if(target.monitorFriendlyDeviceName[0])monitor.label=utf8(target.monitorFriendlyDeviceName);
            }
            if(matches!=1u)return false; // stale enumeration: no partial identity assignment
        }
        return true;
    }
    return false;
}
}
void configure_startup_monitor(const std::string& saved,bool enabled){
    state={};
    if(enabled){
        Scan scan;
        if(EnumDisplayMonitors(nullptr,nullptr,add,reinterpret_cast<LPARAM>(&scan))&&!scan.failed){
            if(!identities(scan))for(auto& m:scan.monitors)m.paths.clear();
            for(auto& m:scan.monitors){
                const auto& r=m.bounds;
                m.label+=" ("+std::to_string(r.right-r.left)+"x"+std::to_string(r.bottom-r.top)+" at "+
                    std::to_string(r.left)+", "+std::to_string(r.top)+(r.primary?", primary)":")");
            }
            state.monitors=std::move(scan.monitors);
        }
    }
    state.selection=select_startup_monitor(state.monitors,saved,enabled);
    if(!state.selection.note.empty())std::printf("window: %s\n",state.selection.note.c_str());
}
const StartupMonitorState& startup_monitor_state(){return state;}
unsigned selected_monitor_render_height(bool borderless){
    const auto* monitor=state.selected();if(!monitor)return 0u;
    if(borderless)return unsigned(monitor->bounds.bottom-monitor->bounds.top);
    RECT rect{0,0,1280,720};AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
    const long frame_width=rect.right-rect.left-1280,frame_height=rect.bottom-rect.top-720;
    const long work_height=monitor->work.bottom-monitor->work.top;
    return monitor->work.right-monitor->work.left>frame_width&&work_height>frame_height?
        unsigned(std::min(720L,work_height-frame_height)):720u;
}
}
