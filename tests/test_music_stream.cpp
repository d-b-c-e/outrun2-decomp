// Generated owned numeric PCM only. No game, device, backend or audible output.
#include "platform/music_stream.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#if defined(OUTRUN_WITH_FFMPEG)
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
#endif
using namespace outrun::platform;
namespace {
unsigned checks{};
void check(bool ok,const std::string& message){++checks;if(!ok)throw std::runtime_error(message);}
constexpr std::size_t SourceFrames=257;
int sample(std::size_t frame,unsigned channel){return (int((frame*37u+channel*101u)%127u)-63)*256;}
void le(std::vector<std::uint8_t>& out,std::uint32_t value,unsigned count){
    for(unsigned i=0;i<count;++i)out.push_back(std::uint8_t(value>>(8u*i)));
}
void text(std::vector<std::uint8_t>& out,const char* value){out.insert(out.end(),value,value+4);}
std::vector<std::uint8_t> wav(unsigned rate,unsigned channels,unsigned bits,bool floating=false){
    std::vector<std::uint8_t> out;const auto bytes=std::uint32_t(SourceFrames*channels*(bits/8u));
    text(out,"RIFF");le(out,36u+bytes+(bytes&1u),4);text(out,"WAVE");text(out,"fmt ");le(out,16,4);
    le(out,floating?3u:1u,2);le(out,channels,2);le(out,rate,4);le(out,rate*channels*(bits/8u),4);
    le(out,channels*(bits/8u),2);le(out,bits,2);text(out,"data");le(out,bytes,4);
    for(std::size_t f=0;f<SourceFrames;++f)for(unsigned c=0;c<channels;++c){
        const int value=sample(f,c);
        if(floating){const float v=float(value)/32768.0f;std::uint32_t encoded{};static_assert(sizeof v==sizeof encoded);std::memcpy(&encoded,&v,sizeof v);le(out,encoded,4);}
        else if(bits==8)le(out,std::uint32_t(value/256+128),1);
        else le(out,std::uint32_t(std::int64_t(value)*(std::int64_t(1)<<(bits-16u))),bits/8u);
    }
    if(bytes&1u)out.push_back(0);
    return out;
}
#if defined(OUTRUN_WITH_FFMPEG)
std::vector<std::uint8_t> load(const std::string& path){
    std::ifstream in(path,std::ios::binary);check(bool(in),"missing generated owned fixture");
    return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
void decode_numeric(std::vector<std::uint8_t> bytes,unsigned rate,unsigned channels){
    MusicStream stream;std::string error;
    check(stream.open(std::move(bytes),false,error),error);
    check(stream.stats.rate==rate&&stream.stats.channels==channels,"source layout stats");
    const double step=double(rate)/48000.0;double position=0;std::size_t output=0;
    for(unsigned call=0;stream.playing()&&call<4096;++call){
        const std::size_t count=std::array<std::size_t,4>{{1,3,17,61}}[call%4u];
        std::vector<double> acc(count*2u,7.0);const auto before=stream.stats.frames;
        check(stream.mix_add(acc.data(),count,0.5,error),error);
        const auto rendered=std::size_t(stream.stats.frames-before);
        for(std::size_t f=0;f<count;++f){
            if(f<rendered){
                check(position<double(SourceFrames),"too many decoded frames");
                const auto index=std::size_t(position),next=std::min(index+1u,SourceFrames-1u);
                const auto fraction=position-double(index);
                for(unsigned c=0;c<2;++c){const unsigned source_channel=channels==1?0:c;
                    const double expected=7.0+(sample(index,source_channel)+(sample(next,source_channel)-sample(index,source_channel))*fraction)*0.5;
                    check(std::abs(acc[f*2u+c]-expected)<0.0001,"numeric PCM/gain/channel/resample mismatch");
                }
                position+=step;++output;
            }else check(acc[f*2u]==7.0&&acc[f*2u+1u]==7.0,"EOF must not change unused mix frames");
        }
    }
    check(!stream.playing(),"bounded EOF and buffered-tail drain");
    check(output==std::size_t(std::ceil(double(SourceFrames)/step)),"source tail was lost");
    check(stream.stats.ends==1,"exact one end");
}
#endif
void policy_and_validation(){
    check(!music_replacement_policy().allow_wav&&!music_replacement_policy().allow_flac,"defaults off");
    for(unsigned mask=0;mask<16;++mask){
        const MusicReplacementPolicy p{(mask&1)!=0,(mask&2)!=0};
        const bool wav_exists=(mask&4)!=0,flac_exists=(mask&8)!=0;
        const auto expected=p.allow_flac&&flac_exists?MusicFileFormat::Flac:p.allow_wav&&wav_exists?MusicFileFormat::Wav:MusicFileFormat::Vorbis;
        check(select_music_file(p,wav_exists,flac_exists)==expected,"flag/existence precedence");
        set_music_replacement_policy(p);const auto got=music_replacement_policy();
        check(got.allow_wav==p.allow_wav&&got.allow_flac==p.allow_flac,"startup policy roundtrip");
    }
    set_music_replacement_policy({});
    check(music_replacement_name("MiXeD.Name.ogg",MusicFileFormat::Flac)=="MiXeD.Name.flac","same stem");
    check(music_replacement_name("Track",MusicFileFormat::Wav)=="Track.wav","stem without extension");
    check(music_replacement_name("Original.ogg",MusicFileFormat::Vorbis)=="Original.ogg","original name untouched");
    std::string error;const auto good=wav(48000,1,16);
    check(validate_music_file(MusicFileFormat::Wav,good,error),"WAV signature");
    auto short_wave=good;short_wave.pop_back();
    check(!validate_music_file(MusicFileFormat::Wav,short_wave,error),"declared RIFF length must be present");
    check(!validate_music_file(MusicFileFormat::Flac,good,error),"selected wrong format refuses");
    check(select_music_file({true,true},true,true)==MusicFileFormat::Flac,"bad existing FLAC still wins; no decoder fallback");
    check(!validate_music_file(MusicFileFormat::Flac,{},error),"empty refusal");
    check(!validate_music_file(MusicFileFormat::Wav,std::vector<std::uint8_t>(64u*1024u*1024u+1u),error),"input size bound");
    std::vector<std::uint8_t> flac{'f','L','a','C',128,0,0,34};flac.resize(42);
    check(validate_music_file(MusicFileFormat::Flac,flac,error),"length-delimited STREAMINFO validation");
    flac.pop_back();check(!validate_music_file(MusicFileFormat::Flac,flac,error),"truncated metadata refusal");
    for(const std::string comment:{"loopstart=3","LOOPLENGTH=31","LoopEnd=34"}){
        std::vector<std::uint8_t> tagged{'f','L','a','C',0,0,0,34};tagged.resize(42);
        const auto size=std::uint32_t(12+comment.size());
        tagged.push_back(132);tagged.push_back(0);tagged.push_back(0);tagged.push_back(std::uint8_t(size));
        le(tagged,0,4);le(tagged,1,4);le(tagged,std::uint32_t(comment.size()),4);
        tagged.insert(tagged.end(),comment.begin(),comment.end());
        check(!validate_music_file(MusicFileFormat::Flac,tagged,error)&&error.find("parity pending")!=std::string::npos,"case-insensitive custom loop refusal");
    }
}
void replacement_checks(const std::string& dir){
    std::string error;MusicStream stream;
#if defined(OUTRUN_WITH_FFMPEG)
    check(av_find_input_format("bink")&&avcodec_find_decoder(AV_CODEC_ID_BINKVIDEO)&&
          avcodec_find_decoder(AV_CODEC_ID_BINKAUDIO_RDFT)&&avcodec_find_decoder(AV_CODEC_ID_BINKAUDIO_DCT),"actual Bink components retained");
    check(music_replacement_decoder_available(MusicFileFormat::Wav),"actual required WAV demuxer/PCM decoders unavailable");
    check(music_replacement_decoder_available(MusicFileFormat::Flac),"actual FLAC demuxer/decoder unavailable");
    for(const auto rate:{8000u,24000u,48000u,96000u,192000u})for(const auto channels:{1u,2u})
        for(const auto bits:{8u,16u,24u,32u})decode_numeric(wav(rate,channels,bits),rate,channels);
    decode_numeric(wav(48000,1,32,true),48000,1);decode_numeric(wav(24000,2,32,true),24000,2);
    check(!dir.empty(),"FFmpeg fixture requires generated FLAC directory");
    struct Case {const char* file;unsigned rate,channels;};
    for(const auto& c:std::array<Case,5>{{{"mono-48000-s16.flac",48000,1},{"stereo-24000-s24.flac",24000,2},
        {"stereo-96000-s32.flac",96000,2},{"mono-8000-u8.flac",8000,1},{"stereo-192000-f32.flac",192000,2}}})
        decode_numeric(load(dir+"/"+c.file),c.rate,c.channels);
    auto tagged=load(dir+"/tagged-loop.flac");
    check(!stream.open(std::move(tagged),true,error)&&error.find("parity pending")!=std::string::npos,"custom loop tags explicitly pending");
    for(const auto& bytes:std::array<std::vector<std::uint8_t>,2>{{wav(48000,1,16),load(dir+"/mono-48000-s16.flac")}}){
        check(stream.open(bytes,true,error),"loop open");
        std::vector<double> out((SourceFrames*3u+31u)*2u);
        check(stream.mix_add(out.data(),out.size()/2u,1.0,error),error);
        for(std::size_t i=0;i<out.size()/2u;++i)check(out[i*2u]==sample(i%SourceFrames,0)&&out[i*2u+1u]==out[i*2u],"whole-file loop/gain");
        check(stream.stats.loops>=3&&stream.playing(),"whole-file looping remains active");
        const auto stops=stream.stats.stops;stream.stop();check(!stream.playing()&&stream.stats.stops==stops+1,"explicit stop");
    }
    for(const auto layout:std::array<std::array<unsigned,2>,3>{{{{7999,1}},{{192001,2}},{{48000,3}}}})
        check(!stream.open(wav(layout[0],layout[1],16),false,error),"unsupported rate/channel refusal");
    for(const auto& original:std::array<std::vector<std::uint8_t>,2>{{wav(48000,1,16),load(dir+"/mono-48000-s16.flac")}}){
        auto truncated=original;truncated.resize(truncated.size()-17u);
        bool ok=stream.open(std::move(truncated),false,error);
        std::array<double,8192> out{};if(ok)ok=stream.mix_add(out.data(),out.size()/2u,1.0,error);
        check(!ok,"truncated replacement must refuse open or decode");stream.stop();
    }
    for(unsigned i=0;i<32;++i){
        check(stream.open(wav(48000,2,16),false,error),"reopen owned memory");
        check(!stream.open({'R','I','F','F'},false,error)&&!stream.playing(),"failed new open closes previous stream");
    }
    check(stream.open(wav(48000,1,16),false,error),"mix bound fixture");
    check(!stream.mix_add(nullptr,96001,1.0,error)&&!stream.playing(),"mix request bound");
#else
    (void)dir;
    check(!music_replacement_decoder_available(MusicFileFormat::Wav),"WAV decoder unavailable build");
    check(!music_replacement_decoder_available(MusicFileFormat::Flac),"FLAC decoder unavailable build");
    check(!stream.open(wav(48000,1,16),false,error)&&error.find("unavailable")!=std::string::npos,"explicit unavailable error");
#endif
    check(!stream.open({},false,error)&&!stream.playing(),"empty/failed reopen stopped");
}
}
int main(int argc,char** argv){
    try{policy_and_validation();replacement_checks(argc>1?argv[1]:"");
        std::printf("music_stream: %u checks PASS; owned numeric PCM only; no audible acceptance\n",checks);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"music_stream: FAIL after %u checks: %s\n",checks,e.what());return 1;}
}
