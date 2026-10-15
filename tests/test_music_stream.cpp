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
std::vector<std::uint8_t> add_flac_comments(const std::vector<std::uint8_t>& file,const std::vector<std::vector<std::string>>& blocks){
    check(file.size()>=4&&std::memcmp(file.data(),"fLaC",4)==0,"test requires native owned FLAC");
    std::vector<std::uint8_t> out{'f','L','a','C'};std::size_t at=4;bool last=false;
    while(!last){
        check(at<=file.size()&&file.size()-at>=4,"owned FLAC metadata header");
        last=(file[at]&128)!=0;const auto type=file[at]&127;
        const std::size_t size=(std::size_t(file[at+1])<<16)|(std::size_t(file[at+2])<<8)|file[at+3];at+=4;
        check(size<=file.size()-at,"owned FLAC metadata size");
        if(type!=4){out.push_back(std::uint8_t(type));out.insert(out.end(),file.begin()+std::ptrdiff_t(at-3),file.begin()+std::ptrdiff_t(at+size));}
        at+=size;
    }
    for(std::size_t block=0;block<blocks.size();++block){
        std::vector<std::uint8_t> payload;le(payload,0,4);le(payload,std::uint32_t(blocks[block].size()),4);
        for(const auto& comment:blocks[block]){le(payload,std::uint32_t(comment.size()),4);payload.insert(payload.end(),comment.begin(),comment.end());}
        check(payload.size()<16777216u,"test metadata fits FLAC block");
        out.push_back(std::uint8_t(block+1u==blocks.size()?132:4));
        out.push_back(std::uint8_t(payload.size()>>16));out.push_back(std::uint8_t(payload.size()>>8));out.push_back(std::uint8_t(payload.size()));
        out.insert(out.end(),payload.begin(),payload.end());
    }
    out.insert(out.end(),file.begin()+std::ptrdiff_t(at),file.end());return out;
}
std::vector<std::uint8_t> metadata_flac(std::uint64_t frames){
    std::vector<std::uint8_t> out{'f','L','a','C',128,0,0,34};out.resize(42);
    out[21]=std::uint8_t((frames>>32)&15);
    for(unsigned i=0;i<4;++i)out[22+i]=std::uint8_t(frames>>(24u-i*8u));return out;
}
void custom_loop_metadata_checks(){
    check(!music_flac_loop_points_enabled(),"custom loop default off");
    MusicFlacLoopPoints points;std::string error;const auto base=metadata_flac(257);
    const auto valid=add_flac_comments(base,{{"LOOPSTART=3","LOOPEND=34"}});
    check(!parse_music_flac_loop_points(valid,false,points,error)&&error.find("disabled")!=std::string::npos,"custom metadata requires explicit opt-in");
    auto expect=[&](std::vector<std::string> tags,std::uint64_t start,std::uint64_t end,bool length){
        check(parse_music_flac_loop_points(add_flac_comments(base,{tags}),true,points,error),error);
        check(points.enabled&&points.start==start&&points.end_exclusive==end&&points.length_compatibility==length&&points.total_frames==257,"exact custom metadata semantics");
    };
    expect({"loopstart=3","LoopEnd=34"},3,34,false);
    expect({"LOOPSTART=3","LOOPLENGTH=31"},3,33,true);
    expect({"LOOPEND=99","LOOPLENGTH=31","LOOPSTART=3"},3,33,true);
    expect({"LOOPLENGTH=31","LOOPSTART=3","LOOPEND=99"},3,33,true);
    expect({"LOOPSTART=1","loopstart=3","LOOPLENGTH=22","looplength=31","LOOPEND=34","loopend=99"},3,33,true);
    expect({"LOOPSTART=3","LOOPEND=10","LOOPEND=34"},3,34,false);
    expect({"LOOPSTART=0","LOOPLENGTH=2"},0,1,true);
    expect({"LOOPSTART=256","LOOPLENGTH=2"},256,257,true);
    expect({"LOOPSTART=0","LOOPEND=257"},0,257,false);
    auto refuse=[&](std::vector<std::string> tags){
        check(!parse_music_flac_loop_points(add_flac_comments(base,{tags}),true,points,error),"invalid custom metadata must refuse");
        check(!points.enabled&&!error.empty(),"invalid points never enabled");
    };
    for(const auto& value:std::vector<std::string>{"","-1","+3"," 3","3 ","3x","18446744073709551616","000000000000000000003",std::string("3\0x",3)})
        refuse({"LOOPSTART="+value,"LOOPEND=34"});
    refuse({"LOOPSTART=x","LOOPSTART=3","LOOPEND=34"});
    refuse({"LOOPSTART=3","LOOPLENGTH=x","LOOPLENGTH=31"});
    refuse({"LOOPSTART=3","LOOPEND=x","LOOPLENGTH=31"});
    refuse({"LOOPSTART=18446744073709551615","LOOPLENGTH=2"});
    refuse({"LOOPSTART=18446744073709551615","LOOPLENGTH=0"});
    refuse({"LOOPSTART=3","LOOPLENGTH=0"});refuse({"LOOPSTART=3","LOOPLENGTH=1"});
    refuse({"LOOPSTART=3"});refuse({"LOOPLENGTH=31"});refuse({"LOOPEND=34"});
    refuse({"LOOPLENGTH=31","LOOPEND=34"});
    refuse({"LOOPSTART=3","LOOPEND=3"});refuse({"LOOPSTART=4","LOOPEND=3"});refuse({"LOOPSTART=0","LOOPEND=258"});
    check(!parse_music_flac_loop_points(add_flac_comments(metadata_flac(0),{{"LOOPSTART=0","LOOPEND=1"}}),true,points,error),"unknown source sample count refuses custom points");
    check(!parse_music_flac_loop_points(add_flac_comments(base,{{"LOOPSTART=3"},{"LOOPEND=34"}}),true,points,error),"split custom metadata blocks refuse");
    check(!parse_music_flac_loop_points(add_flac_comments(base,{{"ARTIST=owned"},{"LOOPSTART=3","LOOPEND=34"}}),true,points,error),"custom loops require one comment block");
    check(parse_music_flac_loop_points(add_flac_comments(base,{{"LOOPSTARTX=3"}}),true,points,error)&&!points.enabled,"unrecognized tag is not approximated");
    set_music_flac_loop_points_enabled(true);set_music_replacement_policy({true,false});
    check(music_flac_loop_points_enabled()&&music_replacement_policy().allow_wav,"loop opt-in independent of format policy");
    set_music_flac_loop_points_enabled(false);set_music_replacement_policy({});
}
std::vector<std::uint8_t> wav(unsigned rate,unsigned channels,unsigned bits,bool floating=false,std::size_t frames=SourceFrames){
    std::vector<std::uint8_t> out;const auto bytes=std::uint32_t(frames*channels*(bits/8u));
    text(out,"RIFF");le(out,36u+bytes+(bytes&1u),4);text(out,"WAVE");text(out,"fmt ");le(out,16,4);
    le(out,floating?3u:1u,2);le(out,channels,2);le(out,rate,4);le(out,rate*channels*(bits/8u),4);
    le(out,channels*(bits/8u),2);le(out,bits,2);text(out,"data");le(out,bytes,4);
    for(std::size_t f=0;f<frames;++f)for(unsigned c=0;c<channels;++c){
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
struct NumericResult {std::vector<double> pcm;std::uint64_t loops{};};
NumericResult decode_numeric(std::vector<std::uint8_t> bytes,unsigned rate,unsigned channels,const char* format,unsigned bits,
                            std::size_t source_frames=SourceFrames,std::array<std::size_t,4> chunks={{1,3,17,61}},
                            bool loop=false,std::size_t requested=0){
    MusicStream stream;std::string error;
    std::printf("case format=%s rate=%u channels=%u bits=%u sourceFrames=%zu loop=%u\n",format,rate,channels,bits,source_frames,unsigned(loop));
    check(stream.open(std::move(bytes),loop,error),error);
    check(stream.stats.rate==rate&&stream.stats.channels==channels,"source layout stats");
    // Closed-form sample/time oracle, independent of the decoder's incremental
    // phase state. Expected counts remain exact; no epsilon permits extra EOF.
    const auto expected=loop?requested:std::size_t((std::uint64_t(source_frames)*48000u+rate-1u)/rate);
    std::size_t output=0;NumericResult result;
    for(std::size_t call=0;stream.playing()&&(!loop||output<requested)&&call<expected+2u;++call){
        const std::size_t count=loop?std::min(chunks[call%4u],requested-output):chunks[call%4u];
        std::vector<double> acc(count*2u,7.0);const auto before=stream.stats.frames;
        check(stream.mix_add(acc.data(),count,0.5,error),error);
        const auto rendered=std::size_t(stream.stats.frames-before);
        for(std::size_t f=0;f<count;++f){
            if(f<rendered){
                const auto timestamp=std::uint64_t(output)*rate;
                const auto absolute_index=std::size_t(timestamp/48000u);
                check(loop||absolute_index<source_frames,"too many decoded frames");
                const auto index=absolute_index%source_frames;
                const auto next=loop?(index+1u)%source_frames:std::min(index+1u,source_frames-1u);
                const auto fraction=double(timestamp%48000u)/48000.0;
                for(unsigned c=0;c<2;++c){const unsigned source_channel=channels==1?0:c;
                    const double numeric=7.0+(sample(index,source_channel)+(sample(next,source_channel)-sample(index,source_channel))*fraction)*0.5;
                    check(std::abs(acc[f*2u+c]-numeric)<0.0001,"numeric PCM/gain/channel/resample mismatch");
                    result.pcm.push_back(acc[f*2u+c]);
                }
                ++output;
            }else check(acc[f*2u]==7.0&&acc[f*2u+1u]==7.0,"EOF must not change unused mix frames");
        }
    }
    std::printf("case count format=%s rate=%u channels=%u bits=%u output=%zu expected=%zu sourceFrames=%zu loop=%u loops=%llu\n",
                format,rate,channels,bits,output,expected,source_frames,unsigned(loop),static_cast<unsigned long long>(stream.stats.loops));
    std::fflush(stdout);
    check(output==expected,"source frame count mismatch (see actual case/count context)");
    if(loop){
        check(stream.playing()&&stream.stats.ends==0,"whole-file loop remains active");
        const auto last_index=(std::uint64_t(output-1u)*rate)/48000u;
        check(stream.stats.loops==(last_index+1u)/source_frames,"exact source loop count including interpolation lookahead");
        result.loops=stream.stats.loops;stream.stop();check(!stream.playing(),"loop stop");
    }else{
        check(!stream.playing(),"bounded EOF and buffered-tail drain");
        check(stream.stats.ends==1,"exact one end");
    }
    return result;
}
NumericResult render_custom_loop(const std::vector<std::uint8_t>& bytes,unsigned rate,unsigned channels,
                                std::uint64_t start,std::uint64_t end,std::size_t outputs,std::array<std::size_t,4> chunks){
    MusicStream stream;std::string error;NumericResult result;
    const std::string case_name="custom rate="+std::to_string(rate)+" channels="+std::to_string(channels)+
        " start="+std::to_string(start)+" endExclusive="+std::to_string(end);
    const bool opened=stream.open(bytes,true,error);
    check(opened,case_name+": "+error);
    check(stream.stats.rate==rate&&stream.stats.channels==channels,"custom decoded layout");
    auto physical=[&](std::uint64_t logical){return logical<end?logical:start+(logical-end)%(end-start);};
    std::size_t output=0;
    for(std::size_t call=0;output<outputs&&call<outputs+1u;++call){
        const auto count=std::min(chunks[call%4u],outputs-output);std::vector<double> pcm(count*2u);
        const auto before=stream.stats.frames;
        const bool mixed=stream.mix_add(pcm.data(),count,0.5,error);
        check(mixed,case_name+": "+error);
        check(stream.stats.frames-before==count,"custom loop emitted exact requested frames");
        for(std::size_t f=0;f<count;++f,++output){
            const auto timestamp=std::uint64_t(output)*rate,index=timestamp/48000u;
            const auto a=physical(index),b=physical(index+1u);const double fraction=double(timestamp%48000u)/48000.0;
            for(unsigned c=0;c<2;++c){const auto channel=channels==1?0u:c;
                const double expected=(sample(std::size_t(a),channel)+(sample(std::size_t(b),channel)-sample(std::size_t(a),channel))*fraction)*0.5;
                check(std::abs(pcm[f*2u+c]-expected)<0.0001,"custom intro/end-exclusive/wrap/interpolation sample mismatch");
                result.pcm.push_back(pcm[f*2u+c]);
            }
        }
    }
    const auto lookahead=(std::uint64_t(outputs-1u)*rate)/48000u+1u;
    const auto expected_loops=lookahead<end?0u:1u+(lookahead-end)/(end-start);
    std::printf("custom count rate=%u channels=%u start=%llu endExclusive=%llu output=%zu loops=%llu expectedLoops=%llu\n",
                rate,channels,static_cast<unsigned long long>(start),static_cast<unsigned long long>(end),output,
                static_cast<unsigned long long>(stream.stats.loops),static_cast<unsigned long long>(expected_loops));
    check(output==outputs&&stream.playing()&&stream.stats.ends==0,"custom loop remains active");
    check(stream.stats.loops==expected_loops,"custom exact loop count including lookahead");
    result.loops=stream.stats.loops;stream.stop();check(!stream.playing(),"custom stop");return result;
}
void custom_loop_decode_checks(const std::string& retained,const std::string& focused){
    check(!retained.empty()&&!focused.empty(),"owned retained/focused fixture directories required");
    check(music_replacement_decoder_available(MusicFileFormat::Flac),"actual FLAC decoder required");
    set_music_flac_loop_points_enabled(true);
    const auto small=load(retained+"/mono-48000-s16.flac");
    auto pair=[&](const std::vector<std::uint8_t>& bytes,unsigned rate,unsigned channels,std::uint64_t start,std::uint64_t end,
                  std::size_t outputs,std::array<std::size_t,4> chunks){
        const auto a=render_custom_loop(bytes,rate,channels,start,end,outputs,chunks);
        const auto b=render_custom_loop(bytes,rate,channels,start,end,outputs,{{509,1,3,31}});
        check(a.pcm==b.pcm&&a.loops==b.loops,"custom PCM/loop count independent of chunks");
    };
    pair(add_flac_comments(small,{{"LOOPSTART=3","LOOPEND=34"}}),48000,1,3,34,1003,{{33,1,1,31}});
    pair(add_flac_comments(small,{{"LOOPSTART=1","loopstart=3","LOOPLENGTH=31","LOOPEND=99"}}),48000,1,3,33,1003,{{32,1,1,31}});
    pair(add_flac_comments(small,{{"LOOPSTART=10","LOOPLENGTH=2"}}),48000,1,10,11,111,{{10,1,1,31}});
    pair(add_flac_comments(small,{{"LOOPSTART=113","LOOPEND=257"}}),48000,1,113,257,1003,{{256,1,1,31}});
    pair(add_flac_comments(small,{{"LOOPSTART=0","LOOPEND=257"}}),48000,1,0,257,1003,{{257,1,3,31}});
    // Interior final sample in a one-frame file must seek its containing frame0.
    pair(add_flac_comments(small,{{"LOOPSTART=256","LOOPEND=257"}}),48000,1,256,257,503,{{256,1,3,31}});
    const auto fractional=add_flac_comments(load(retained+"/mono-8000-u8.flac"),{{"LOOPSTART=3","LOOPLENGTH=31"}});
    pair(fractional,8000,1,3,33,1503,{{197,1,1,31}});
    // loop=false deliberately retains the entire file, including samples after END.
    decode_numeric(add_flac_comments(small,{{"LOOPSTART=3","LOOPLENGTH=31"}}),48000,1,"tagged-nonloop",16);
    const auto long_flac=load(focused+"/stereo-44100-block1024.flac");
    MusicFlacLoopPoints points;std::string error;
    check(parse_music_flac_loop_points(long_flac,true,points,error)&&points.total_frames==20017,"owned multiblock source length");
    check(long_flac.size()>12&&((unsigned(long_flac[10])<<8)|long_flac[11])==1024,"owned FLAC fixed block size 1024");
    pair(add_flac_comments(long_flac,{{"LOOPSTART=1137","LOOPEND=13123"}}),44100,2,1137,13123,43003,{{14282,1,3,31}});
    pair(add_flac_comments(long_flac,{{"LOOPSTART=1137","LOOPEND=20017"}}),44100,2,1137,20017,65003,{{21786,1,3,31}});
    // LOOPSTART inside the final short frame exercises a nonzero frame anchor.
    pair(add_flac_comments(long_flac,{{"LOOPSTART=19900","LOOPEND=20017"}}),44100,2,19900,20017,23003,{{21786,1,3,31}});
    decode_numeric(add_flac_comments(long_flac,{{"LOOPSTART=1137","LOOPEND=13123"}}),44100,2,"tagged-multiblock-nonloop",24,20017,{{4096,31,1,3}});
    MusicStream stream;
    check(stream.open(add_flac_comments(small,{{"LOOPSTART=3","LOOPEND=34"}}),true,error),"custom lifecycle open");
    std::vector<double> active(140);check(stream.mix_add(active.data(),70,1.0,error)&&stream.stats.loops>0,"custom lifecycle traversed a wrap");
    const auto previous_loops=stream.stats.loops;
    check(!stream.open(add_flac_comments(small,{{"LOOPSTART=3","LOOPLENGTH=1"}}),true,error)&&!stream.playing(),"invalid custom reopen closes previous owner");
    check(stream.open(small,false,error),"untagged reopen clears custom state");
    std::vector<double> pcm(600);check(stream.mix_add(pcm.data(),300,1.0,error)&&!stream.playing(),"untagged nonloop still ends");
    check(stream.stats.loops==previous_loops,"custom state cannot leak into untagged reopen");
    set_music_flac_loop_points_enabled(false);
    check(!stream.open(add_flac_comments(small,{{"LOOPSTART=3","LOOPEND=34"}}),true,error)&&error.find("disabled")!=std::string::npos,"disabled policy still refuses tags");
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
        for(const auto bits:{8u,16u,24u,32u})decode_numeric(wav(rate,channels,bits),rate,channels,"WAV-PCM",bits);
    decode_numeric(wav(48000,1,32,true),48000,1,"WAV-FLOAT",32);decode_numeric(wav(24000,2,32,true),24000,2,"WAV-FLOAT",32);
    check(!dir.empty(),"FFmpeg fixture requires generated FLAC directory");
    struct Case {const char* file;unsigned rate,channels,bits;};
    for(const auto& c:std::array<Case,5>{{{"mono-48000-s16.flac",48000,1,16},{"stereo-24000-s24.flac",24000,2,24},
        {"stereo-96000-s32.flac",96000,2,32},{"mono-8000-u8.flac",8000,1,16},{"stereo-192000-f32.flac",192000,2,24}}})
        decode_numeric(load(dir+"/"+c.file),c.rate,c.channels,"FLAC",c.bits);
    // Long, nonintegral output/source ratios with independent chunk schedules.
    // These exercise actual WAV decoding, exact tail length and identical PCM.
    for(const auto rate:{11025u,44100u,47999u}){
        constexpr std::size_t frames=480017;
        const auto bytes=wav(rate,2,24,false,frames);
        const auto a=decode_numeric(bytes,rate,2,"WAV-long",24,frames,{{4096,997,17,1}});
        const auto b=decode_numeric(bytes,rate,2,"WAV-long",24,frames,{{31,1,3,509}});
        check(a.pcm==b.pcm,"chunk-independent long fractional PCM/tail count");
    }
    for(const auto rate:{11025u,44100u,47999u}){
        constexpr std::size_t frames=1003;
        const auto outputs=std::size_t((std::uint64_t(frames)*3u*48000u+rate-1u)/rate)+73u;
        const auto bytes=wav(rate,1,16,false,frames);
        const auto a=decode_numeric(bytes,rate,1,"WAV-loop",16,frames,{{1,3,17,61}},true,outputs);
        const auto b=decode_numeric(bytes,rate,1,"WAV-loop",16,frames,{{509,31,1,3}},true,outputs);
        check(a.pcm==b.pcm&&a.loops==b.loops,"chunk-independent fractional loop PCM/count");
    }
    // Existing generated FLAC also traverses fractional-rate loop boundaries.
    const auto flac_loop=load(dir+"/mono-8000-u8.flac");
    const auto fa=decode_numeric(flac_loop,8000,1,"FLAC-loop",16,SourceFrames,{{1,3,17,61}},true,5003);
    const auto fb=decode_numeric(flac_loop,8000,1,"FLAC-loop",16,SourceFrames,{{509,31,1,3}},true,5003);
    check(fa.pcm==fb.pcm&&fa.loops==fb.loops,"chunk-independent FLAC fractional loop PCM/count");
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
    try{
        if(argc>1&&std::string(argv[1])=="--flac-loops-only"){
            custom_loop_metadata_checks();
#if defined(OUTRUN_WITH_FFMPEG)
            check(argc==4,"--flac-loops-only requires retained and focused owned fixture directories");
            custom_loop_decode_checks(argv[2],argv[3]);
#else
            throw std::runtime_error("custom loop numeric fixture requires optional FFmpeg decoder");
#endif
        }else{policy_and_validation();replacement_checks(argc>1?argv[1]:"");}
        std::printf("music_stream: %u checks PASS; owned numeric PCM only; no audible acceptance\n",checks);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"music_stream: FAIL after %u checks: %s\n",checks,e.what());return 1;}
}
