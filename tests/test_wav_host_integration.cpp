// Owned files and the production host callbacks/mixer; no startup, thread or device.
#include "runtime/game_host.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>

namespace rt=outrun::runtime;
namespace pf=outrun::platform;
namespace fs=std::filesystem;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"WAV host line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct FakeAudio final:rt::AudioOutput {
    std::vector<std::int16_t> samples;
    bool open(std::string&)override{return true;}
    bool submit(const std::vector<std::int16_t>& pcm,std::string&)override{samples.insert(samples.end(),pcm.begin(),pcm.end());return true;}
    void close()override{}
    std::uint64_t submitted_frames()const override{return samples.size()/2;}
    std::size_t queued_frames()const override{return 0;}
};
int main(int argc,char** argv){
    CHECK(argc==2&&*argv[1]);
    const fs::path inputs=argv[1],root=inputs/"host-run",sound=root/"Sound";
    fs::create_directories(sound);
    // This fixed directory contains only this fixture's copies, never a retail tree.
    const auto original=sound/pf::music_track_name_771ad8(0);
    const auto wav=sound/"01_sPLASH_WaVE.WaV",flac=sound/"01_SPLASH_wave.FlAc";
    const auto copy=[&](const char* source,const fs::path& destination){fs::copy_file(inputs/source,destination,fs::copy_options::overwrite_existing);};
    const auto corrupt=[&](const fs::path& path){std::ofstream file(path,std::ios::binary);file<<"corrupt selected replacement";CHECK(bool(file));};
    fs::remove(wav);fs::remove(flac);copy("original.ogg",original);
    auto context=std::make_unique<pf::NativeRuntimeContext>();
    rt::GameHost host;rt::RuntimeBinding binding{};FakeAudio output;
    pf::RetailAssetStore retail;retail.root=root.string();
    CHECK(rt::bind_runtime(host,*context,binding));host.retail=&retail;host.audio_output=&output;
    std::vector<std::uint8_t> bank;
    const auto put=[&](unsigned v){for(unsigned i=0;i<4;++i)bank.push_back(std::uint8_t(v>>(8*i)));};
    const unsigned ids[]{0,1,64},sizes[]{47512,29016,47460};
    for(unsigned i=0;i<3;++i){put(ids[i]);put(sizes[i]);for(unsigned j=0;j<sizes[i]/2;++j){bank.push_back(0xe8);bank.push_back(3);}}
    put(~0u);put(~0u);std::string error;
    CHECK(host.menu_audio.load(bank,error));
    const auto play=[&](bool loop){return rt::menu_music_play(&host,0,0,loop?1u:0u);};
    const auto mix=[&](std::size_t frames){std::vector<std::int16_t> pcm;CHECK(host.menu_audio.mix(frames,pcm,error));return pcm;};
    const auto nonzero=[](const auto& pcm){return std::any_of(pcm.begin(),pcm.end(),[](auto v){return v!=0;});};
    std::printf("decoders: Vorbis=%u WAV=%u FLAC=%u\n",unsigned(pf::MusicStream::decoder_available()),
        unsigned(pf::music_replacement_decoder_available(pf::MusicFileFormat::Wav)),unsigned(pf::music_replacement_decoder_available(pf::MusicFileFormat::Flac)));
    CHECK(pf::MusicStream::decoder_available());
    CHECK(!pf::music_replacement_policy().allow_wav&&!pf::music_replacement_policy().allow_flac);
    corrupt(wav);corrupt(flac);
    CHECK(play(false));const auto ogg=mix(32);CHECK(nonzero(ogg));
    CHECK(rt::menu_music_stop(&host,0)&&!host.menu_audio.music().playing());
    pf::set_music_replacement_policy({true,false});fs::remove(wav);
    CHECK(play(false));CHECK(mix(32)==ogg); // no match retains the original decoder
    copy("replacement.wav",wav);
    const bool wav_available=pf::music_replacement_decoder_available(pf::MusicFileFormat::Wav);
    CHECK(play(false)==wav_available);
    if(wav_available){
        const auto baseline=mix(32);CHECK(nonzero(baseline));
        CHECK(play(false));mix(1024);CHECK(!host.menu_audio.music().playing());
        CHECK(play(true));const auto loops=host.menu_audio.music().stats.loops;mix(1024);
        CHECK(host.menu_audio.music().playing()&&host.menu_audio.music().stats.loops>loops);
        CHECK(play(true));float level=.5f;unsigned bits;std::memcpy(&bits,&level,4);
        CHECK(rt::menu_volume(&host,0x42fc90,bits));const auto quiet=mix(32);
        CHECK(nonzero(quiet)&&std::abs(quiet[0])<std::abs(baseline[0]));
        CHECK(rt::menu_volume(&host,0x42fc90,0));const auto frames=host.menu_audio.music().stats.frames;
        CHECK(!nonzero(mix(32))&&host.menu_audio.music().stats.frames==frames);
        level=std::pow(10.f,.2f);std::memcpy(&bits,&level,4);CHECK(rt::menu_volume(&host,0x42fc90,bits));
        CHECK(host.menu_audio.command(0x801,error));const auto voices=host.menu_audio.active_voices();
        CHECK(host.menu_audio.queue_movie({123,-123,456,-456},error));
        const auto movie=mix(1);CHECK(movie[0]>123&&movie[1]>-123&&host.menu_audio.active_voices()==voices);
        CHECK(rt::menu_music_stop(&host,0));const auto retained=mix(1);CHECK(retained[0]>456&&retained[1]>-456);
        host.menu_audio.stop_effects();CHECK(!nonzero(mix(1)));
    }else CHECK(!host.menu_audio.music().playing()&&host.music_error.find("unavailable")!=std::string::npos);
    corrupt(wav);CHECK(!play(false)&&!host.menu_audio.music().playing());
    copy("replacement.wav",wav);fs::resize_file(wav,64u*1024u*1024u+1u);
    CHECK(!play(false)&&!host.menu_audio.music().playing());
    CHECK(host.music_error.find("limit")!=std::string::npos||host.music_error.find("large")!=std::string::npos);
    // A valid FLAC wins over the still-oversized WAV. A corrupt winner refuses even with valid WAV/Ogg.
    copy("replacement.flac",flac);pf::set_music_replacement_policy({true,true});
    const bool flac_available=pf::music_replacement_decoder_available(pf::MusicFileFormat::Flac);
    CHECK(play(true)==flac_available);if(flac_available)CHECK(nonzero(mix(32)));
    copy("replacement.wav",wav);corrupt(flac);CHECK(!play(false)&&!host.menu_audio.music().playing());
    pf::set_music_replacement_policy({false,false});CHECK(play(true));
    host.audio_open=true;host.audio_clock_started=true;
    host.audio_clock=std::chrono::steady_clock::now()-std::chrono::microseconds((host.menu_audio.stats.mixed_frames+128)*1000/48);
    CHECK(rt::pump_audio(host)&&nonzero(output.samples));
    CHECK(rt::menu_music_stop(&host,0)&&!host.menu_audio.music().playing());
    CHECK(!host.render_failed&&!rt::audio_thread().run);
    fs::remove(wav);fs::remove(flac);fs::remove(original);
    std::printf("WAV host integration: %u checks PASS; offline only, fake output\n",checks);
}
