// The real host binding and sound driver with synthetic silent PCM and fake
// presentation/output. No game data, audio device, window or worker thread.
#include "runtime/game_host.hpp"
#include "enhancements/frame_rate.hpp"
#include "enhancements/general_presentation.hpp"
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace rt=outrun::runtime;
namespace pf=outrun::platform;
namespace en=outrun::enhancements;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"presentation sound line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)

struct FakePresenter final:rt::Presenter {
    unsigned draws{};
    bool set_start_loading_scene(std::uint32_t)override{return true;}
    bool set_start_loading_visible(bool)override{return true;}
    bool set_frontend_visible(bool)override{return true;}
    bool set_frontend_glyphs(const std::vector<pf::FrontendGlyph>&)override{return true;}
    bool set_frontend_images(const std::vector<pf::FrontendListImage>&)override{return true;}
    bool set_frontend_icons(const std::vector<pf::FrontendWindowIcon>&)override{return true;}
    bool set_frontend_token(std::uint32_t)override{return true;}
    bool set_frontend_overlay(std::uint32_t,float)override{return true;}
    bool set_frontend_game_backdrop(bool)override{return true;}
    bool set_movie_frame(const std::uint8_t*,std::uint32_t,std::uint32_t)override{return true;}
    bool set_pc_scene(std::function<void(pf::PcD3D9Device&)>)override{return true;}
    bool draw()override{++draws;return true;}
    bool frontend_animation_complete()const override{return false;}
    void scene_to_frame()override{}
    void cycle_pc_diagnostic()override{}
    void disable_pc_specialised_shaders()override{}
};
struct FakeAudio final:rt::AudioOutput {
    unsigned submissions{};
    std::uint64_t frames{};
    bool open(std::string&)override{return true;}
    bool submit(const std::vector<std::int16_t>& pcm,std::string&)override{
        ++submissions;frames+=pcm.size()/2u;return true;
    }
    void close()override{}
    std::uint64_t submitted_frames()const override{return frames;}
    std::size_t queued_frames()const override{return 0;}
};
struct Fixture {
    std::unique_ptr<pf::NativeRuntimeContext> runtime{std::make_unique<pf::NativeRuntimeContext>()};
    rt::GameHost host;
    rt::RuntimeBinding binding{};
    FakePresenter presenter;
    FakeAudio audio;
    Fixture(){
        CHECK(rt::bind_runtime(host,*runtime,binding));
        host.presenter=&presenter;host.audio_output=&audio;host.audio_open=true;
        // Same three-entry MENU format as the existing audio fixture, with
        // silence generated in memory rather than any retail sample or file.
        std::vector<std::uint8_t> bank;
        const auto put=[&](std::uint32_t value){for(unsigned i=0;i<4u;++i)bank.push_back(std::uint8_t(value>>(8u*i)));};
        const unsigned ids[]{0u,1u,64u},sizes[]{47512u,29016u,47460u};
        for(unsigned i=0;i<3u;++i){put(ids[i]);put(sizes[i]);bank.resize(bank.size()+sizes[i],0u);}
        put(~0u);put(~0u);
        std::string error;CHECK(host.menu_audio.load(bank,error));
        runtime->pc_sound.ready=true;
    }
    void call(std::uint32_t pc){
        const auto& callback=binding.services.loop.frame;
        (void)callback.call(callback.user,pc,nullptr,0u);
    }
};

static void cadence(bool experimental,std::uint32_t legacy_rate,const char* limit,bool sync_override=false){
    en::set_display_rate(legacy_rate);
    CHECK(en::general_presentation().configure(experimental,limit));
    Fixture f;
    unsigned ticks=0,renders=0;
    for(unsigned count:{0u,1u,3u,0u,4u,2u,6u,0u}){
        rt::audio_main_acquire(); // the normal input-to-render main-thread ownership
        for(unsigned n=0;n<count;++n){
            f.call(0x453bb0u); // the existing tick's preceding service
            f.call(0x42f330u);++ticks;
            // PcSound performs its ICS/fade update every fourth service call.
            // Check before rendering too, to distinguish a deferred/collapsed tick.
            CHECK(f.runtime->pc_sound.stats.ticks==((experimental||sync_override)?ticks:renders)/4u);
        }
        f.call(0x449050u);++renders;
        CHECK(f.runtime->pc_sound.stats.ticks==((experimental||sync_override)?ticks:renders)/4u);
        CHECK(f.host.game_frames==renders);
        CHECK(f.audio.submissions==renders); // PCM pumping remains per render
        CHECK(f.presenter.draws==renders);
        CHECK(!f.host.render_failed);
    }
}

int main(int argc,char** argv){
    if(argc==2&&std::string_view(argv[1])=="--vsync-controls"){
        for(const auto sync:{"0","1","2"}){
            CHECK(en::presentation_controls().configure("0",sync));
            cadence(false,60u,"0",true);
        }
        CHECK(en::presentation_controls().configure("1","2",false));
        cadence(false,60u,"0"); // exclusions restore the original compatibility path
        CHECK(en::general_presentation().configure(false,"0"));en::set_display_rate(60u);
        std::printf("Presentation controls sound: %u checks PASS (synthetic host; no device/assets)\n",checks);return 0;
    }
    CHECK(!rt::audio_thread().run);
    CHECK(!rt::audio_thread().failed);
    cadence(false,60u,"0");
    cadence(false,120u,"0"); // existing 120 Hz behavior is deliberately retained
    cadence(true,60u,"144");
    cadence(true,120u,"0"); // uncapped presentation uses the same fixed-tick owner
    {
        Fixture f;
        f.host.presenter=nullptr;
        for(unsigned n=0;n<4u;++n)f.call(0x42f330u);
        CHECK(f.runtime->pc_sound.stats.ticks==1u); // no presenter dependency
        f.call(0x449050u);
        CHECK(f.runtime->pc_sound.stats.ticks==1u&&f.audio.submissions==0u);
        f.runtime->pc_sound.ready=false;
        for(unsigned n=0;n<4u;++n)f.call(0x42f330u);
        CHECK(f.runtime->pc_sound.stats.ticks==1u);
        f.host.runtime=nullptr;f.call(0x42f330u); // absent runtime remains a no-op
    }
    CHECK(en::general_presentation().configure(false,"0"));
    en::set_display_rate(60u);
    std::printf("Presentation sound: %u checks PASS (synthetic host callbacks; no device/assets)\n",checks);
}
