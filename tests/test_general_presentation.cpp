#include "enhancements/general_presentation.hpp"
#include "enhancements/frame_rate.hpp"
#include "enhancements/gpu_backend.hpp"
#include "platform/native_runtime.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <memory>
#include <filesystem>
#include <fstream>
#include <sstream>
namespace en=outrun::enhancements;
namespace pf=outrun::platform;
static unsigned checks;
static void check(bool value,const char* why){++checks;if(!value){std::fprintf(stderr,"presentation: %s\n",why);std::exit(1);}}
static void policy(){
    en::GeneralPresentation p;
    for(const auto text:{"0","60","90","119","144","165","180","240","360","1000"}){
        check(p.configure(true,text)&&p.enabled,"accepted ordinary cap");
        check(p.configure(true,text,false)&&!p.enabled,"test/record/playback gate");
    }
    for(const auto text:{"","-1","1","59","1001","1e2","144fps","nan","inf","4294967296"})
        check(!p.configure(true,text)&&!p.enabled,"invalid cap refuses experimental mode");
    check(p.configure(false,"bad")&&!p.enabled,"off ignores an unused cap");
    for(const unsigned rate:{60u,90u,119u,144u,165u,180u,240u,360u,1000u}){
        en::PresentationDeadline deadline;std::uint64_t now=1000000000ull;
        check(deadline.wait_ns(now,rate)==0,"first present has no artificial wait");
        for(unsigned frame=0;frame<rate;++frame)now+=deadline.wait_ns(now,rate);
        check(now==2000000000ull,"fractional nanosecond remainder retains one-second average");
        now+=1000000000ull;check(deadline.wait_ns(now,rate)==0,"late present rebases without catch-up burst");
        check(deadline.wait_ns(now,0)==0,"uncapped clears deadline");
        check(deadline.wait_ns(now,rate)==0,"cap resumes fresh after uncapped/loading reset");
    }
    en::VideoSettings start;start.frame_rate=120;en::GpuVideoBackend normal("",start);
    en::general_presentation().configure(false,"0");normal.set_frame_rates({60,120});
    check(normal.current().frame_rate==120&&en::display_rate()==120,"off preserves saved 120 mode");
    check(!en::GpuVideoBackend::parse_option("framerate","144",start),"legacy options contract unchanged");
    en::general_presentation().configure(true,"0");start.frame_rate=0;en::GpuVideoBackend unlocked("",start);
    unlocked.set_frame_rates({0});check(unlocked.current().frame_rate==0&&en::display_frames(),"zero selects interpolation instead of forcing 60");
    check(unlocked.frame_rates().size()==1,"experimental override removes misleading live frame-rate row");
    const auto file=std::filesystem::temp_directory_path()/"or2_general_presentation_fixture.ini";
    check(!std::filesystem::exists(file)&&!std::filesystem::exists(file.string()+".tmp"),"fresh isolated options fixture");
    {std::ofstream out(file,std::ios::binary);out<<"; original comment\nframerate=120\ncustom=keep\n";check(bool(out),"write synthetic options fixture");}
    start.frame_rate=144;en::GpuVideoBackend saved(file.string(),start);saved.set_frame_rates({144});
    saved.apply(saved.current());
    const auto contents=[&]{std::ifstream in(file,std::ios::binary);std::ostringstream out;out<<in.rdbuf();return out.str();};
    const auto experimental=contents();
    check(experimental.find("framerate=120")!=std::string::npos&&experimental.find("custom=keep")!=std::string::npos,
          "experimental video edits preserve the ordinary saved rate and unrelated key");
    en::general_presentation().configure(false,"0");en::set_display_rate(60);
    en::GpuVideoBackend ordinary(file.string(),start);ordinary.set_frame_rates({60,120});ordinary.apply(ordinary.current());
    check(contents().find("framerate=60")!=std::string::npos,"ordinary backend retains its original rate writer");
    check(std::filesystem::remove(file),"remove only the owned synthetic fixture");
}
static void timeline(pf::NativeRuntimeContext& context,unsigned rendered_rate,unsigned cap,std::uint32_t mode){
    struct Fixture {
        pf::NativeRuntimeContext* c{};
        std::uint64_t now=60000000,frequency{};
        unsigned frames{},frame_limit{},input{},sound{},control{},draw{},zero{},multiple{};
    } f;f.c=&context;
    f.frequency=std::uint64_t(rendered_rate)*60000u;f.frame_limit=rendered_rate+1u;f.c->mode_state.current=mode;
    en::general_presentation().configure(true,std::to_string(cap));en::set_display_rate(cap);
    pf::NativeRuntimePlatform platform{};platform.user=&f;
    platform.ticks=[](void* u)->std::uint64_t{return static_cast<Fixture*>(u)->now;};
    platform.frequency=[](void* u)->std::uint64_t{return static_cast<Fixture*>(u)->frequency;};
    platform.sleep_ns=[](void*,std::uint64_t){check(false,"synthetic timeline never sleeps");};
    platform.main_loop=[](void* u){auto& s=*static_cast<Fixture*>(u);++s.frames;
        s.now=60000000ull+std::uint64_t(s.frames)*60000u;return s.frames<s.frame_limit;};
    pf::NativeRuntimeLoopServices services{};services.platform=&platform;
    services.setup={&f,[](void*,std::uint32_t,const std::uint32_t*,std::size_t)->std::uint32_t{return 0;}};
    services.cleanup={&f,[](void*,std::uint32_t,std::uint32_t,std::uint32_t){}};
    services.frame={&f,[](void* u,std::uint32_t pc,const std::uint32_t*,std::size_t)->std::uint32_t{
        auto& s=*static_cast<Fixture*>(u);
        if(pc==0x455c20u)return 1;
        if(pc==0x453bb0u)++s.input;
        if(pc==0x42f330u)++s.sound;
        if(pc==0x43fa20u)++s.control;
        if(pc==0x449050u){++s.draw;const auto ticks=s.c->frame_state.updates_95af48;
            if(!ticks)++s.zero;if(ticks>1)++s.multiple;
            check(en::display_blend()>=0.f&&en::display_blend()<=1.f,"interpolation remains bounded");
            // Model a little rendering work so the original 1/60000-second
            // platform guard does not need a real sleep. The next frame's
            // absolute synthetic deadline still owns the exact render rate.
            s.now+=s.frequency/60000u;
        }
        return 0;
    }};
    check(pf::run_native_frame_loop(*f.c,services)==0,"production loop closes normally");
    check(f.input==60&&f.sound==60&&f.control==60,"one second has 60 input/sound/control calls at every render rate");
    check(f.draw==rendered_rate+1u&&f.zero>0,"display-only warmup/extra frames do not advance simulation");
    if(rendered_rate<60u)check(f.multiple>0,"slow rendering runs multiple simulation updates");
    en::general_presentation().configure(false,"0");en::set_display_rate(60);
}
int main(){
    policy();
    // The real loop's process-wide network module retains views into its first
    // context. Keep one context alive for all timelines, as the host does;
    // destroying it between cases leaves those real service views dangling.
    auto context=std::make_unique<pf::NativeRuntimeContext>();
    for(const auto rate:{30u,60u,90u,120u,144u,165u,180u,240u,360u,1000u})
        for(const auto mode:{16u,32u})timeline(*context,rate,rate<60u?144u:rate,mode);
    timeline(*context,360,0,16);
    std::printf("General presentation: %u checks PASS (synthetic clocks/services only)\n",checks);
}
