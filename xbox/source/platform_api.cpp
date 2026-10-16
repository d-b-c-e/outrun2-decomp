#include "platform_api.hpp"
#include "native_window.hpp"
#include "platform/retail_asset_store.hpp"
#include "platform/ui_prompt_family.hpp"
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
namespace {
namespace wgi=winrt::Windows::Gaming::Input;
bool running=true;
std::string retail_root;
// -1..1 (y up) to the DualSense byte (0..255, 128 centre, y down).
std::uint8_t axis(double v,bool invert){v=std::clamp(invert?-v:v,-1.0,1.0);return std::uint8_t(std::clamp<long>(std::lround(128.0+v*(v<0?128.0:127.0)),0,255));}
// A pad's USB identity (RawGameController); known false when it cannot be read.
outrun::xbox_runtime::GamepadIdentity lookup(const wgi::Gamepad& pad){
    outrun::xbox_runtime::GamepadIdentity id{false,0,0};
    try{if(const auto raw=wgi::RawGameController::FromGameController(pad))id={true,raw.HardwareVendorId(),raw.HardwareProductId()};}
    catch(...){}
    return id;
}
// Cached per pad: a known identity for good; an unreadable one is read again after 2 s (sana 44396: a transient failure
// must not leave an unrelated pad out for the whole process; until it reads, the pad stays unread while anything is
// owned). The cache holds the pads it has seen, so a pad's address is never reused for another while cached.
outrun::xbox_runtime::GamepadIdentity identity(const wgi::Gamepad& pad){
    struct Entry { wgi::Gamepad pad; outrun::xbox_runtime::GamepadIdentity id; std::chrono::steady_clock::time_point retry; };
    static std::vector<Entry> cache;
    const auto now=std::chrono::steady_clock::now();
    for(auto& c:cache)if(c.pad==pad){
        if(!c.id.known&&now>=c.retry){c.id=lookup(pad);c.retry=now+std::chrono::seconds(2);}
        return c.id;
    }
    if(cache.size()>=16)cache.clear();
    cache.push_back(Entry{pad,lookup(pad),now+std::chrono::seconds(2)});
    return cache.back().id;
}
// The desktop's choice (native_window.hpp gamepad_choice), logged whenever the pads or the choice change.
int choose_pad(const winrt::Windows::Foundation::Collections::IVectorView<wgi::Gamepad>& pads){
    const auto choice=outrun::xbox_runtime::gamepad_choice();
    if(!choice)return 0;
    std::vector<outrun::xbox_runtime::GamepadIdentity> ids;
    for(const auto& pad:pads)ids.push_back(identity(pad));
    const int pick=choice(ids.data(),unsigned(ids.size()));
    static std::string last;
    std::string line="pad: "+std::to_string(ids.size())+" WGI Gamepad(s);";
    for(std::size_t i=0;i<ids.size();++i){
        char id[32];
        if(ids[i].known)std::snprintf(id,sizeof id,"VID %04X PID %04X",unsigned(ids[i].vid),unsigned(ids[i].pid));
        else std::snprintf(id,sizeof id,"identity unreadable");
        line+=" ["+std::to_string(i)+"] "+id+(int(i)==pick?" read by the game's pad path":pick<0||int(i)<pick?" left out (the input profile reads it, or it cannot be identified)":" not read");
    }
    if(line!=last){std::printf("%s\n",line.c_str());std::fflush(stdout);last=line;}
    return pick<int(ids.size())?pick:-1;
}
// The first connected gamepad (or the desktop's choice), in the shared DualSense layout (src/input/dual_sense.hpp).
void read_pad(outrun::ps5::PadSample& sample){
    using namespace outrun::ps5;
    sample=PadSample{};
    // Presentation only. No pad, refused choice or unreadable/unsupported ID
    // stays generic. Never infer a family from the shared DualSense layout.
    outrun::platform::texture_replacement_set_pad_family(outrun::platform::UiPromptFamily::PC);
    const auto pads=wgi::Gamepad::Gamepads();
    if(pads.Size()==0)return;
    const int pick=choose_pad(pads);
    if(pick<0)return;
    const auto selected=pads.GetAt(std::uint32_t(pick));
    const auto r=selected.GetCurrentReading();
    const auto id=identity(selected); // existing cached WGI identity; no second enumeration/reader
    outrun::platform::texture_replacement_set_pad_family(outrun::platform::ui_prompt_family_for_identity(id.known,id.vid,id.pid));
    sample.connected=true;
    sample.left_x=axis(r.LeftThumbstickX,false);sample.left_y=axis(r.LeftThumbstickY,true);
    sample.right_x=axis(r.RightThumbstickX,false);sample.right_y=axis(r.RightThumbstickY,true);
    sample.l2=std::uint8_t(std::lround(std::clamp(r.LeftTrigger,0.0,1.0)*255));sample.r2=std::uint8_t(std::lround(std::clamp(r.RightTrigger,0.0,1.0)*255));
    using B=wgi::GamepadButtons;
    const struct {B raw;std::uint32_t logical;} mapping[]{
        {B::A,Cross},{B::B,Circle},{B::X,Square},{B::Y,Triangle},{B::Menu,Options},{B::View,Share},
        {B::LeftShoulder,L1},{B::RightShoulder,R1},{B::DPadLeft,Left},{B::DPadRight,Right},{B::DPadUp,Up},{B::DPadDown,Down},
        {B::LeftThumbstick,StickL},{B::RightThumbstick,StickR}};
    for(auto m:mapping)if((r.Buttons&m.raw)==m.raw)sample.buttons|=m.logical;
    if(sample.l2>127)sample.buttons|=L2;if(sample.r2>127)sample.buttons|=R2;
}
// Two spellings of one folder: separators, case and trailing separators ignored (Windows paths).
bool same_folder(std::string a,std::string b){
    auto norm=[](std::string& s){for(auto& c:s){if(c=='/')c='\\';else if(c>='A'&&c<='Z')c=char(c-'A'+'a');}while(s.size()>3&&s.back()=='\\')s.pop_back();};
    norm(a);norm(b);return !a.empty()&&a==b;
}
bool writable(const std::string& folder){
    const auto probe=folder+"/.or2-write-test";
    std::FILE* f=std::fopen(probe.c_str(),"wb");if(!f)return false;std::fclose(f);std::remove(probe.c_str());return true;
}
}
bool application_initialize(std::string&){running=true;return true;}
void application_shutdown(){running=false;}
bool application_running(){return running;}
void platform_sleep_ns(s64 ns){if(ns>0)std::this_thread::sleep_for(std::chrono::nanoseconds(ns));else std::this_thread::yield();}
// The game folder is the home: the original data is read there and the
// cache, saves, options and logs are written there, as the PC executable
// does in its installation folder. PC build: the executable's folder (it is
// copied into the Steam game folder). Xbox: OutRun2006 at the root of a USB
// drive, then the application's LocalState\OutRun2006.
std::string application_home(int argc,char** argv){
    for(int i=1;i<argc;++i){const std::string a=argv[i]?argv[i]:"";if(a.rfind("--data=",0)==0)retail_root=a.substr(7);}
    if(retail_root.empty()){
        std::vector<std::string> candidates{outrun::xbox_runtime::launch_folder()};
        for(char drive='D';drive<='Z';++drive)candidates.push_back(std::string(1,drive)+":/OutRun2006");
        if(auto local=outrun::xbox_runtime::local_state_folder();!local.empty())candidates.push_back(local+"/OutRun2006");
        for(const auto& path:candidates)if(!path.empty()&&outrun::platform::retail_asset_store_is_game_root(path)){retail_root=path;break;}
    }
    if(retail_root.empty())throw std::runtime_error("OutRun 2006 game folder not found (USB drive OutRun2006, LocalState\\OutRun2006 or next to the executable)");
    // A latched (test) desktop process: the game folder is data only. Its write probe and SaveGame folder were made in
    // the run folder by the entry point's fence, which also seeded the owner's saves there.
    if(const auto& run=outrun::xbox_runtime::latched_home();!run.empty()){
        // The seed and the receipt name the fenced folder; a different data root (--data=, a drive search) is refused.
        if(!same_folder(retail_root,outrun::xbox_runtime::latched_game_root()))throw std::runtime_error("a test run needs the data folder to be the fenced game folder: data "+retail_root+", fenced "+outrun::xbox_runtime::latched_game_root());
        return run;
    }
    if(!writable(retail_root))throw std::runtime_error("the game folder is read-only: "+retail_root);
    std::error_code ec;
    std::filesystem::create_directories(retail_root+"/SaveGame",ec);
    if(ec)throw std::runtime_error("cannot create SaveGame: "+ec.message());
    return retail_root;
}
std::string application_retail_root(){return retail_root;}
// stdout goes to runtime.log in the game folder (window_*.cpp); no text console.
void consoleInit(void*){}void consoleUpdate(void*){std::fflush(stdout);}void consoleExit(void*){std::fflush(stdout);}
void padConfigureInput(int,u64){}void padInitializeDefault(PadState* p){*p=PadState{};}
void padUpdate(PadState* p){
    if(!outrun::xbox_runtime::pump_window_events())running=false;
    read_pad(p->sample);
    const auto held=p->sample.connected?p->sample.buttons:0u;p->down=held&~p->previous;p->up=p->previous&~held;p->previous=held;
}
bool padIsConnected(const PadState* p){return p->sample.connected;}
u64 padGetButtons(const PadState* p){return p->sample.buttons;}u64 padGetButtonsDown(const PadState* p){return p->down;}u64 padGetButtonsUp(const PadState* p){return p->up;}
HidAnalogStickState padGetStickPos(const PadState* p,unsigned stick){
    const auto x=stick?p->sample.right_x:p->sample.left_x,y=stick?p->sample.right_y:p->sample.left_y;
    return {(int(x)-128)*256,-(int(y)-128)*256};
}
