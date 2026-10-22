// Owned disposable Sound files, real production lookup and MenuAudio; no device.
#include "runtime/game_host.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <windows.h>
namespace fs=std::filesystem;
namespace pf=outrun::platform;
namespace rt=outrun::runtime;
static unsigned checks;
static void check(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
static std::vector<std::uint8_t> read(const fs::path& p){std::ifstream f(p,std::ios::binary);check(bool(f),"fixture missing");return {std::istreambuf_iterator<char>(f),{}};}
static void write(const fs::path& p,const std::vector<std::uint8_t>& b){std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));check(bool(f),"fixture write failed");}
int main(int argc,char** argv){
    try{
        check(argc==3,"requires owned fixture and disposable output roots");
        check(pf::music_replacement_decoder_available(pf::MusicFileFormat::Flac),"FLAC decoder unavailable");
        check(pf::music_replacement_decoder_available(pf::MusicFileFormat::Wav),"WAV decoder unavailable");
        check(pf::MusicStream::decoder_available(),"stock Vorbis decoder unavailable");
        const fs::path fixtures=argv[1],base=argv[2];
        fs::create_directories(base);
        const auto root=base/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        check(fs::create_directory(root),"disposable root already exists");
        struct Cleanup {fs::path root;~Cleanup(){std::error_code ec;fs::remove_all(root,ec);pf::set_music_replacement_policy({});}} cleanup{root};
        fs::create_directory(root/"Sound");
        const auto flac=read(fixtures/"mono-48000-s16.flac"),wav=read(fixtures/"mono-48000-s16.wav"),ogg=read(fixtures/"stock.ogg");
        const auto fp=root/"Sound"/"bEaCh_WaVe.FlAc",wp=root/"Sound"/"Beach_wave.WaV";
        write(root/"Sound"/"Beach_wave.OgG",ogg);write(wp,wav);write(fp,flac);
        pf::RetailAssetStore store;store.root=root.string();
        auto host=std::make_unique<rt::GameHost>();host->retail=&store;
        auto& audio=host->menu_audio;std::string error;std::vector<std::uint8_t> bytes;
        auto lookup=[&](pf::MusicReplacementPolicy policy){pf::set_music_replacement_policy(policy);return rt::read_music_file(*host,"BEACH_WAVE.ogg",bytes,error);};
        check(lookup({})&&bytes==ogg,"disabled replacements preserve stock bytes");
        check(lookup({true,false})&&bytes==wav,"enabled WAV wins over Ogg");
        check(lookup({false,true})&&bytes==flac,"mixed-case same-stem FLAC wins");
        check(lookup({true,true})&&bytes==flac,"FLAC wins over WAV and Ogg");
        fs::remove(fp);check(lookup({true,true})&&bytes==wav,"absent FLAC retains WAV");
        fs::remove(wp);check(lookup({true,true})&&bytes==ogg,"absent replacements preserve stock");
        write(wp,wav);write(fp,{'b','a','d'});
        check(!lookup({true,true})&&!error.empty(),"corrupt selected FLAC refuses without WAV/Ogg fallback");
        write(fp,flac);fs::resize_file(fp,64u*1024u*1024u+1u);
        check(!lookup({true,true})&&!error.empty(),"oversized selected FLAC refuses without fallback");
        write(fp,flac);
        HANDLE held=CreateFileW(fp.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(held!=INVALID_HANDLE_VALUE,"exclusive unreadable fixture");
        const bool unreadable=lookup({true,true});CloseHandle(held);
        check(!unreadable&&!error.empty(),"unreadable selected FLAC refuses without fallback");
        std::vector<std::uint8_t> bank;
        auto put=[&](unsigned v){for(unsigned i=0;i<4;++i)bank.push_back(std::uint8_t(v>>(8*i)));};
        const unsigned ids[]{0,1,64},sizes[]{47512,29016,47460};
        for(unsigned i=0;i<3;++i){put(ids[i]);put(sizes[i]);for(unsigned j=0;j<sizes[i]/2;++j){bank.push_back(0xe8);bank.push_back(3);}}
        put(~0u);put(~0u);check(audio.load(bank,error),"owned MENU bank");
        std::vector<std::int16_t> stock;
        check(lookup({})&&audio.play_music(std::move(bytes),false,error)&&audio.mix(100,stock,error),"disabled replacements play stock through MenuAudio");
        check(std::any_of(stock.begin(),stock.end(),[](auto v){return v!=0;}),"stock decoding produces PCM");audio.stop_music();
        auto play=[&](){audio.stop_music();return lookup({true,true})&&audio.play_music(std::move(bytes),true,error);};
        check(play(),"lookup-to-MenuAudio FLAC open");
        std::vector<std::int16_t> pcm,first;
        check(audio.mix(800,first,error),"whole-file looping mix");
        for(unsigned f=0;f<800;++f){const auto expected=((int((f%257)*37%127)-63)*256);check(first[f*2]==expected&&first[f*2+1]==expected,"exact whole-file FLAC PCM loop");}
        check(audio.music().playing()&&audio.music().stats.loops>=3,"whole-file looping remains active");
        audio.stop_music();check(!audio.music().playing(),"replacement stop");
        check(audio.mix(1,pcm,error)&&pcm[0]==0&&pcm[1]==0,"stopped replacement is silent");
        check(play()&&audio.mix(1,pcm,error)&&pcm[0]==first[0]&&pcm[1]==first[1],"reopen resets replacement cursor");
        audio.stop_music();check(audio.command(1,error)&&audio.mix(1,pcm,error),"effect-only reference");const auto effect=pcm[0];
        audio.stop_effects();check(play()&&audio.command(1,error)&&audio.mix(1,pcm,error),"replacement plus effect mix");
        check(pcm[0]==first[0]+effect&&pcm[1]==pcm[0],"replacement adds to shared effect mixer");
        audio.stop_music();check(audio.active_voices()==1,"replacement stop preserves effect");audio.stop_effects();
        std::vector<std::uint8_t> broken{'f','L','a','C',128,0,0,34};broken.resize(42);write(fp,broken);
        check(!play()&&!audio.music().playing(),"selected decoder failure closes prior stream without fallback");
        write(fp,flac);check(play(),"replacement reopens after selected failure");audio.stop_music();
        check(!pf::music_flac_loop_points_enabled(),"loop-tag policy remains default off");
        std::printf("music_lookup: %u checks PASS; owned lookup/MenuAudio only; no device\n",checks);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"music_lookup: FAIL after %u checks: %s\n",checks,e.what());return 1;}
}
