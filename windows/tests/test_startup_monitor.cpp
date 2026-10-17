#include "startup_monitor.hpp"
#include <cstdio>
#include <cstdlib>
using namespace outrun::desktop;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"startup monitor line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static bool same(ScreenRect a,ScreenRect b){return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;}
int main(){
    // Primary dashboard above an independently selected racing row, shuffled enumeration and negative coordinates.
    std::vector<StartupMonitor> monitors{
        {{0,-1080,1920,0,true},{0,-1080,1920,-40,true},{"path-dashboard"},"Dashboard"},
        {{2560,0,5120,1440,false},{2560,0,5120,1400,false},{"path-right"},"Right"},
        {{-2560,0,0,1440,false},{-2560,0,0,1400,false},{"path-left"},"Left"},
        {{0,0,2560,1440,false},{0,0,2560,1400,false},{"path-center"},"Center"}};
    CHECK(!select_startup_monitor(monitors,"").index);
    CHECK(select_startup_monitor(monitors,"").note.empty());
    CHECK(!select_startup_monitor(monitors,"path-center",false).index);
    CHECK(!select_startup_monitor(monitors,"path-center",false).note.empty());
    CHECK(!select_startup_monitor({},"path-center").index);
    CHECK(!select_startup_monitor(monitors,"3").index);
    CHECK(!select_startup_monitor(monitors,"Center").index);
    CHECK(!select_startup_monitor(monitors,"\\\\.\\DISPLAY3").index);
    CHECK(!select_startup_monitor(monitors,"path-center ").index);
    auto selection=select_startup_monitor(monitors,"PATH-CENTER");
    CHECK(selection.index&&*selection.index==3u);
    CHECK(selection.note.find("Center")!=std::string::npos);
    const auto saved_rect=monitors[*selection.index].bounds;
    std::reverse(monitors.begin(),monitors.end());
    selection=select_startup_monitor(monitors,"path-center");
    CHECK(selection.index&&*selection.index==0u);
    CHECK(same(monitors[*selection.index].bounds,saved_rect));
    ScreenRect span{};std::string reason;
    CHECK(startup_monitor_span(monitors,*selection.index,span,&reason));
    CHECK(same(span,{-2560,0,5120,1440,false}));
    CHECK(monitors.back().bounds.primary&&!monitors.front().bounds.primary); // original OS-primary flags intact
    CHECK(!startup_monitor_span(monitors,monitors.size(),span));
    CHECK(!startup_monitor_span(monitors,3u,span)); // dashboard is not a three-display row
    auto duplicate=monitors;duplicate.push_back(monitors.front());
    CHECK(!select_startup_monitor(duplicate,"path-center").index);
    auto clone=monitors;clone.front().paths.push_back("second-physical-target");
    CHECK(!select_startup_monitor(clone,"path-center").index);
    CHECK(!select_startup_monitor(clone,"second-physical-target").index);
    auto unavailable=monitors;unavailable.front().paths.clear();
    CHECK(!select_startup_monitor(unavailable,"path-center").index);
    auto unusable=monitors;unusable.front().work.right=unusable.front().work.left;
    CHECK(!select_startup_monitor(unusable,"path-center").index);
    unusable=monitors;unusable.front().bounds.bottom=unusable.front().bounds.top;
    CHECK(!select_startup_monitor(unusable,"path-center").index);
    auto no_right=monitors;no_right.erase(no_right.begin()+2);
    CHECK(!startup_monitor_span(no_right,0u,span,&reason));
    auto in_band=monitors;in_band.push_back({{5120,0,6400,1440,false},{5120,0,6400,1400,false},{"extra"},"Extra"});
    CHECK(!startup_monitor_span(in_band,0u,span,&reason));
    CHECK(same(center_startup_window({-2560,0,0,1400,false},1300,760),{-1930,320,-630,1080,false}));
    CHECK(same(center_startup_window({-800,-600,0,0,false},1300,760),{-800,-600,0,0,false}));
    CHECK(same(center_startup_window({100,50,1100,850,false},1000,800),{100,50,1100,850,false}));
    CHECK(monitor_path_equal("\\\\?\\DISPLAY#ABC#123","\\\\?\\display#abc#123"));
    CHECK(!monitor_path_equal("path-a","path-aa"));
    CHECK(!monitor_path_equal("path-a","path-b"));
    std::printf("Startup monitor: %u checks PASS (synthetic identities/rectangles only)\n",checks);
}
