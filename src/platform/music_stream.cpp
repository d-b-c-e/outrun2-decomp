#include "platform/music_stream.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#if defined(OR2_HAVE_VORBIS)
#include <vorbis/vorbisfile.h>
#endif
#if defined(OUTRUN_WITH_FFMPEG)
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>
}
#endif

namespace outrun::platform {
namespace {
// PC table 771AD8 (names as the EXE spells them; the retail SD tree is
// case-insensitive).
constexpr const char* Tracks771ad8[MusicTrackCount]{
    "01_Splash_wave.ogg","02_Magical_Sound_Shower.ogg","03_Passing_Breeze.ogg","04_Risky_Ride.ogg","05_Shiny_World.ogg",
    "06_Night_Flight.ogg","07_Life_was_a_Bore.ogg","08_Radiation.ogg","09_Night_Bird.ogg","10_Splash_wave_1986.ogg",
    "11_Magical_Sound_Shower_1986.ogg","12_Passing_Breeze_1986.ogg","13_Shake_the_Street_1989.ogg","14_Rush_a_Difficulty_1989.ogg",
    "15_Who_are_You_1989.ogg","16_Keep_Your_Heart_1989.ogg","17_Splash_Wave_EuroMix.ogg","18_Magical_Sound_Shower_EuroMix.ogg",
    "19_Passing_Breeze_EuroMix.ogg","20_Risky_Ride_Guitar_Mix.ogg","21_Shake_the_Street_ARRANGED.ogg","22_Who_are_you_ARRANGED.ogg",
    "23_Rush_a_difficulty_ARRANGED.ogg","24_Keep_your_Heart_ ARRANGED.ogg","25_Shiny_World_prototype.ogg","26_Night_Flight_prototype.ogg",
    "27_Life_was_a_Bore_Instrumental.ogg","28_Night_Flight_Instrumental.ogg","LAST_WAVE.ogg","SEGA441.ogg","BEACH_WAVE.ogg",
    "TITLE_PATTERN1.ogg","TITLE_01.ogg","SEL_01_Splash_Wave.ogg","SEL_02_Magical_Sound_Shower.ogg","SEL_03_Passing_Breeze.ogg",
    "SEL_04_Risky_Ride.ogg","SEL_05_Shiny_World.ogg","SEL_06_Night_Flight.ogg","SEL_07_Life_was_a_Bore.ogg","SEL_08_Radiation.ogg",
    "SEL_09_Night_Bird.ogg","SEL_10_Splash_Wave_1986.ogg","SEL_11_Magical_Sound_Shower_1986.ogg","SEL_12_Passing_Breeze_1986.ogg",
    "SEL_13_Shake_the_Street_1989.ogg","SEL_14_Rush_a_Difficulty.ogg","SEL_15_Who_are_You_1989.ogg","SEL_16_Keep_Your_Heart_1989.ogg",
    "SEL_17_Splash_Wave_EuroMix.ogg","SEL_18_Magical_Sound_Shower_EuroMix.ogg","SEL_19_Passing_Breeze_EuroMix.ogg",
    "SEL_20_Risky_Ride_Guitar.ogg","SEL_21_Shake_the_Street_ARRANGED.ogg","SEL_22_Who_are_you_ARRANGED.ogg",
    "SEL_23_Rush_a_Difficulty_ARRANGED.ogg","SEL_24_Keep_your_Heart_ARRANGED.ogg","SEL_25_Shiny_World_Prototype.ogg",
    "SEL_26_Night_Flight_Prototype.ogg","SEL_27_Life_was_a_Bore_Instrumental.ogg","SEL_28_Night_Flight_Instrumental.ogg",
    "OR2ED1.ogg","OR2ED2.ogg","OR2ED3A.ogg","OR2ED3B.ogg","OR2ED4.ogg","OR2ED5.ogg"};
bool fail(std::string& e,const char* m){e=m;return false;}
constexpr std::size_t MaxMusicBytes=64u*1024u*1024u;
std::atomic<unsigned> replacement_policy{0};
std::atomic<bool> flac_loop_policy{false};
bool magic(const std::vector<std::uint8_t>& b,std::size_t at,const char* s,std::size_t n){
    return at<=b.size()&&n<=b.size()-at&&std::memcmp(b.data()+at,s,n)==0;
}
std::uint32_t little32(const std::vector<std::uint8_t>& b,std::size_t at){
    return std::uint32_t(b[at])|(std::uint32_t(b[at+1])<<8)|(std::uint32_t(b[at+2])<<16)|(std::uint32_t(b[at+3])<<24);
}
bool wav_chunks(const std::vector<std::uint8_t>& b,std::string& error){
    const auto length=std::size_t(little32(b,4));
    if(length<4||length>b.size()-8)return fail(error,"music: truncated RIFF container");
    const auto end=length+8;std::size_t at=12;bool format=false,data=false;
    for(unsigned chunk=0;at<end&&chunk<1024;++chunk){
        if(end-at<8)return fail(error,"music: truncated WAV chunk header");
        const auto size=std::size_t(little32(b,at+4));
        const bool is_format=magic(b,at,"fmt ",4),is_data=magic(b,at,"data",4);at+=8;
        if(size>end-at)return fail(error,"music: truncated WAV chunk");
        if(is_format){if(size<16)return fail(error,"music: short WAV format");format=true;}
        if(is_data){if(!size)return fail(error,"music: empty WAV PCM");data=true;}
        at+=size;
        if(size&1u){if(at==end)return fail(error,"music: missing WAV chunk padding");++at;}
    }
    return (at==end&&format&&data)||fail(error,"music: missing WAV format/data or too many chunks");
}
int loop_key(const std::uint8_t* p,std::size_t n,std::size_t& prefix){
    int index=0;
    for(const char* key:{"LOOPSTART=","LOOPLENGTH=","LOOPEND="}){
        ++index;const std::size_t len=std::strlen(key);if(n<len)continue;
        std::size_t i=0;for(;i<len;++i){const auto c=p[i];if((c>='a'&&c<='z'?c-32:c)!=key[i])break;}
        if(i==len){prefix=len;return index;}
    }
    return 0;
}
bool loop_decimal(const std::uint8_t* p,std::size_t n,std::uint64_t& value){
    if(!n||n>20)return false;
    value=0;
    for(std::size_t i=0;i<n;++i){
        if(p[i]<'0'||p[i]>'9')return false;
        const auto digit=std::uint64_t(p[i]-'0');
        if(value>(std::numeric_limits<std::uint64_t>::max()-digit)/10u)return false;
        value=value*10u+digit;
    }
    return true;
}
// Native FLAC metadata is length delimited. Do not scan compressed frames for
// strings (false positives), or let unbounded metadata reach the demuxer.
bool flac_metadata(const std::vector<std::uint8_t>& b,bool allow,MusicFlacLoopPoints& points,std::string& error){
    std::size_t at=4;bool last=false;unsigned comment_blocks=0;bool trailing_comments=false;
    bool seen[3]{};std::uint64_t values[3]{};
    for(unsigned block=0;!last&&block<128;++block){
        if(at>b.size()||b.size()-at<4)return fail(error,"music: truncated FLAC metadata");
        last=(b[at]&128)!=0;const unsigned type=b[at]&127;
        const std::size_t n=(std::size_t(b[at+1])<<16)|(std::size_t(b[at+2])<<8)|b[at+3];at+=4;
        if(n>b.size()-at||at+n>1024u*1024u)return fail(error,"music: FLAC metadata exceeds bound or is truncated");
        if(block==0&&(type!=0||n!=34))return fail(error,"music: missing FLAC STREAMINFO");
        if(block==0){
            points.total_frames=std::uint64_t(b[at+13]&15)<<32;
            for(unsigned i=0;i<4;++i)points.total_frames|=std::uint64_t(b[at+14+i])<<(24u-i*8u);
        }
        if(type==4){
            ++comment_blocks;
            std::size_t p=at;const auto end=at+n;
            auto u32=[&](std::uint32_t& v){
                if(end-p<4)return false;
                v=std::uint32_t(b[p])|(std::uint32_t(b[p+1])<<8)|(std::uint32_t(b[p+2])<<16)|(std::uint32_t(b[p+3])<<24);p+=4;return true;
            };
            std::uint32_t len=0,count=0;
            if(!u32(len)||len>end-p)return fail(error,"music: invalid FLAC comments");
            p+=len;
            if(!u32(count)||count>4096)return fail(error,"music: invalid FLAC comment count");
            for(std::uint32_t i=0;i<count;++i){
                if(!u32(len)||len>end-p)return fail(error,"music: truncated FLAC comment");
                std::size_t prefix=0;const int key=loop_key(b.data()+p,len,prefix);
                if(key){
                    if(!allow)return fail(error,"music: custom FLAC loop points disabled (parity pending unless explicitly enabled)");
                    std::uint64_t value=0;
                    // Validate even a value later replaced by a duplicate or LENGTH.
                    if(!loop_decimal(b.data()+p+prefix,len-prefix,value))return fail(error,"music: malformed or overflowing FLAC loop value");
                    seen[key-1]=true;values[key-1]=value;
                }
                p+=len;
            }
            trailing_comments=trailing_comments||p!=end;
        }
        at+=n;
    }
    if(!last)return fail(error,"music: too many FLAC metadata blocks");
    if(seen[0]||seen[1]||seen[2]){
        if(comment_blocks!=1||trailing_comments)return fail(error,"music: custom FLAC loops require one exact comment block");
        if(!seen[0]||(!seen[1]&&!seen[2]))return fail(error,"music: incomplete FLAC loop points");
        points.start=values[0];points.end_exclusive=values[2];
        if(seen[1]){
            if(values[1]<2)return fail(error,"music: FLAC LOOPLENGTH must be at least 2 (Tweaks exclusive-end compatibility)");
            const auto delta=values[1]-1u;
            if(points.start>std::numeric_limits<std::uint64_t>::max()-delta)return fail(error,"music: FLAC loop end overflow");
            points.end_exclusive=points.start+delta;points.length_compatibility=true;
        }
        if(!points.total_frames||points.start>=points.end_exclusive||points.end_exclusive>points.total_frames)
            return fail(error,"music: FLAC loop points outside known source frames");
        points.enabled=true;
    }
    return true;
}
#if defined(OUTRUN_WITH_FFMPEG)
bool pcm_codec(AVCodecID id){
    return id==AV_CODEC_ID_PCM_U8||id==AV_CODEC_ID_PCM_S16LE||id==AV_CODEC_ID_PCM_S24LE||
        id==AV_CODEC_ID_PCM_S32LE||id==AV_CODEC_ID_PCM_F32LE;
}
bool ff_fail(std::string& e,const char* what,int code){
    char why[AV_ERROR_MAX_STRING_SIZE]{};av_strerror(code,why,sizeof why);
    e=std::string("music: ")+what+": "+why;return false;
}
#endif
}
const char* music_track_name_771ad8(unsigned track){return track<MusicTrackCount?Tracks771ad8[track]:nullptr;}
void set_music_replacement_policy(MusicReplacementPolicy p){replacement_policy.store((p.allow_wav?1u:0u)|(p.allow_flac?2u:0u));}
MusicReplacementPolicy music_replacement_policy(){const auto p=replacement_policy.load();return {(p&1)!=0,(p&2)!=0};}
void set_music_flac_loop_points_enabled(bool enabled){flac_loop_policy.store(enabled);}
bool music_flac_loop_points_enabled(){return flac_loop_policy.load();}
bool parse_music_flac_loop_points(const std::vector<std::uint8_t>& b,bool allow,MusicFlacLoopPoints& points,std::string& error){
    error.clear();points={};
    if(b.size()>MaxMusicBytes||!magic(b,0,"fLaC",4))return fail(error,"music: invalid native FLAC input");
    return flac_metadata(b,allow,points,error);
}
MusicFileFormat select_music_file(MusicReplacementPolicy p,bool wav,bool flac){
    return p.allow_flac&&flac?MusicFileFormat::Flac:p.allow_wav&&wav?MusicFileFormat::Wav:MusicFileFormat::Vorbis;
}
std::string music_replacement_name(const std::string& original,MusicFileFormat f){
    if(f==MusicFileFormat::Vorbis)return original;
    const auto dot=original.find_last_of('.');return original.substr(0,dot)+(f==MusicFileFormat::Flac?".flac":".wav");
}
bool validate_music_file(MusicFileFormat f,const std::vector<std::uint8_t>& b,std::string& error){
    error.clear();
    if(b.empty()||b.size()>MaxMusicBytes)return fail(error,"music: empty input or input exceeds 64 MiB");
    if(f==MusicFileFormat::Vorbis)return magic(b,0,"OggS",4)||fail(error,"music: selected Ogg has wrong signature");
    if(f==MusicFileFormat::Wav)return ((magic(b,0,"RIFF",4)&&magic(b,8,"WAVE",4))||fail(error,"music: selected WAV has wrong signature"))&&wav_chunks(b,error);
    MusicFlacLoopPoints points;
    return parse_music_flac_loop_points(b,music_flac_loop_points_enabled(),points,error);
}
bool music_replacement_decoder_available(MusicFileFormat f){
    if(f==MusicFileFormat::Vorbis)return MusicStream::decoder_available();
#if defined(OUTRUN_WITH_FFMPEG)
    if(!av_find_input_format(f==MusicFileFormat::Flac?"flac":"wav"))return false;
    if(f==MusicFileFormat::Flac)return avcodec_find_decoder(AV_CODEC_ID_FLAC)!=nullptr;
    for(const auto id:{AV_CODEC_ID_PCM_U8,AV_CODEC_ID_PCM_S16LE,AV_CODEC_ID_PCM_S24LE,AV_CODEC_ID_PCM_S32LE,AV_CODEC_ID_PCM_F32LE})
        if(!avcodec_find_decoder(id))return false;
    return true;
#else
    return false;
#endif
}

struct MusicStream::Impl {
    std::vector<std::uint8_t> file;
    std::size_t read_at{};
    bool open{},loop{},ended{};
    std::uint32_t rate{},channels{};
    std::vector<std::int16_t> pending;   // decoded source frames (stereo) not yet consumed
    std::size_t base{};                  // index of pending[0] (frames)
    double position{};                   // source frame position of the next output frame
    bool replacement{};
#if defined(OUTRUN_WITH_FFMPEG)
    AVFormatContext* format{};
    AVIOContext* io{};
    AVCodecContext* codec{};
    AVPacket* packet{};
    AVFrame* frame{};
    SwrContext* convert{};
    int stream{-1};
    AVSampleFormat input_format{AV_SAMPLE_FMT_NONE};
    bool draining{},finished{};
    std::uint64_t decoded_since_seek{};
    std::uint64_t expected_samples{};
    // Exact source phase in units of 1/48000 source frames. Repeated double
    // addition (e.g. 8000/48000) drifts below EOF and can emit an extra frame.
    std::size_t replacement_position{};
    std::uint32_t replacement_fraction{};
    MusicFlacLoopPoints loop_points;
    bool custom_loop{},custom_wrap_pending{},custom_seek_pending{},custom_seek_seen{};
    std::uint64_t custom_next_sample{},custom_seek_discarded{};
    static int read_memory(void* user,std::uint8_t* out,int n){
        auto& s=*static_cast<Impl*>(user);
        if(n<=0)return AVERROR(EINVAL);
        const auto take=std::min(std::size_t(n),s.file.size()-s.read_at);
        if(!take)return AVERROR_EOF;
        std::memcpy(out,s.file.data()+s.read_at,take);s.read_at+=take;return int(take);
    }
    static std::int64_t seek_memory(void* user,std::int64_t offset,int whence){
        auto& s=*static_cast<Impl*>(user);if(whence==AVSEEK_SIZE)return std::int64_t(s.file.size());
        whence&=~AVSEEK_FORCE;
        std::int64_t base=0;
        if(whence==SEEK_CUR)base=std::int64_t(s.read_at);
        else if(whence==SEEK_END)base=std::int64_t(s.file.size());
        else if(whence!=SEEK_SET)return AVERROR(EINVAL);
        if(offset < -base || offset>std::int64_t(s.file.size())-base)return AVERROR(EINVAL);
        s.read_at=std::size_t(base+offset);return std::int64_t(s.read_at);
    }
    bool open_replacement(MusicFileFormat selected,std::string& error){
        if(!music_replacement_decoder_available(selected))return fail(error,"music: WAV/FLAC decoder unavailable in this build");
        auto* buffer=static_cast<unsigned char*>(av_malloc(32768));
        if(!buffer)return fail(error,"music: AVIO allocation failed");
        io=avio_alloc_context(buffer,32768,0,this,&Impl::read_memory,nullptr,&Impl::seek_memory);
        if(!io){av_free(buffer);return fail(error,"music: AVIO context allocation failed");}
        format=avformat_alloc_context();if(!format)return fail(error,"music: format allocation failed");
        format->pb=io;format->flags|=AVFMT_FLAG_CUSTOM_IO;
        format->probesize=1024*1024;format->max_analyze_duration=AV_TIME_BASE;format->max_streams=1;
        int rc=avformat_open_input(&format,nullptr,av_find_input_format(selected==MusicFileFormat::Flac?"flac":"wav"),nullptr);
        if(rc<0)return ff_fail(error,"open replacement",rc);
        rc=avformat_find_stream_info(format,nullptr);if(rc<0)return ff_fail(error,"replacement stream info",rc);
        if(format->nb_streams!=1||format->streams[0]->codecpar->codec_type!=AVMEDIA_TYPE_AUDIO)
            return fail(error,"music: replacement must contain exactly one audio stream");
        stream=0;const auto* parameters=format->streams[0]->codecpar;
        if((selected==MusicFileFormat::Flac&&parameters->codec_id!=AV_CODEC_ID_FLAC)||
           (selected==MusicFileFormat::Wav&&!pcm_codec(parameters->codec_id)))
            return fail(error,"music: selected container has unsupported codec");
        const auto* decoder=avcodec_find_decoder(parameters->codec_id);
        if(!decoder)return fail(error,"music: selected replacement codec unavailable");
        codec=avcodec_alloc_context3(decoder);if(!codec)return fail(error,"music: codec allocation failed");
        rc=avcodec_parameters_to_context(codec,parameters);if(rc<0)return ff_fail(error,"codec parameters",rc);
        codec->thread_count=1;codec->err_recognition=AV_EF_CRCCHECK|AV_EF_EXPLODE;
        rc=avcodec_open2(codec,decoder,nullptr);if(rc<0)return ff_fail(error,"open replacement codec",rc);
        if(codec->ch_layout.nb_channels<1||codec->ch_layout.nb_channels>2||codec->sample_rate<8000||codec->sample_rate>192000)
            return fail(error,"music: unsupported replacement layout");
        rate=std::uint32_t(codec->sample_rate);channels=std::uint32_t(codec->ch_layout.nb_channels);
        const auto* audio=format->streams[stream];
        if(audio->duration>0&&audio->duration!=AV_NOPTS_VALUE){
            const auto samples=av_rescale_q(audio->duration,audio->time_base,AVRational{1,int(rate)});
            if(samples>0)expected_samples=std::uint64_t(samples);
        }
        if(loop&&loop_points.enabled){
            if(selected!=MusicFileFormat::Flac||audio->time_base.num!=1||audio->time_base.den!=int(rate)||
               (expected_samples&&expected_samples!=loop_points.total_frames))
                return fail(error,"music: custom FLAC loops require exact source-sample timestamps/count");
            custom_loop=true;
            std::printf("music: FLAC loop start=%llu end-exclusive=%llu (%s; caller loop enabled)\n",
                        static_cast<unsigned long long>(loop_points.start),static_cast<unsigned long long>(loop_points.end_exclusive),
                        loop_points.length_compatibility?"Tweaks LENGTH-1 compatibility":"explicit END");
        }
        packet=av_packet_alloc();frame=av_frame_alloc();
        if(!packet||!frame)return fail(error,"music: decode allocation failed");
        replacement=true;return true;
    }
    bool seek_custom_loop(std::string& error){
        const int rc=av_seek_frame(format,stream,std::int64_t(loop_points.start),AVSEEK_FLAG_BACKWARD);
        if(rc<0)return ff_fail(error,"custom FLAC loop seek",rc);
        avcodec_flush_buffers(codec);swr_free(&convert);input_format=AV_SAMPLE_FMT_NONE;
        av_packet_unref(packet);av_frame_unref(frame);
        draining=false;custom_wrap_pending=false;custom_seek_pending=true;custom_seek_seen=false;
        custom_seek_discarded=0;return true;
    }
    // Returns one bounded source chunk, or finished=true. No audio device here.
    bool refill_replacement(std::uint64_t& loops,std::string& error){
        for(unsigned work=0;work<128;++work){
            if(custom_loop&&custom_wrap_pending){
                if(!seek_custom_loop(error))return false;
                ++loops;continue;
            }
            const int got=avcodec_receive_frame(codec,frame);
            if(got==0){
                if(frame->nb_samples<=0||frame->nb_samples>65536||frame->sample_rate!=int(rate)||
                   av_channel_layout_compare(&frame->ch_layout,&codec->ch_layout)!=0)
                    return fail(error,"music: replacement frame bounds/layout changed");
                const auto frame_samples=std::size_t(frame->nb_samples);
                std::size_t offset=0,take=frame_samples;
                if(custom_loop){
                    const auto timestamp=frame->best_effort_timestamp!=AV_NOPTS_VALUE?frame->best_effort_timestamp:frame->pts;
                    if(timestamp<0||timestamp==AV_NOPTS_VALUE||std::uint64_t(timestamp)>loop_points.total_frames||
                       frame_samples>loop_points.total_frames-std::uint64_t(timestamp))
                        return fail(error,"music: missing/out-of-range FLAC frame timestamp");
                    const auto first=std::uint64_t(timestamp),end=first+frame_samples;
                    if((!custom_seek_pending||custom_seek_seen)&&first!=custom_next_sample)
                        return fail(error,"music: discontinuous FLAC sample timestamps");
                    custom_next_sample=end;custom_seek_seen=true;
                    if(custom_seek_pending){
                        if(first>loop_points.start)return fail(error,"music: FLAC seek skipped loop start");
                        if(end<=loop_points.start){
                            custom_seek_discarded+=frame_samples;av_frame_unref(frame);
                            if(custom_seek_discarded>1048576u)return fail(error,"music: FLAC seek discard exceeds bound");
                            continue;
                        }
                        offset=std::size_t(loop_points.start-first);custom_seek_discarded+=offset;
                        if(custom_seek_discarded>1048576u)return fail(error,"music: FLAC seek discard exceeds bound");
                        custom_seek_pending=false;
                    }
                    if(first+offset>=loop_points.end_exclusive)return fail(error,"music: FLAC frame missed loop end");
                    const auto selected_end=std::min(end,loop_points.end_exclusive);
                    take=std::size_t(selected_end-first)-offset;
                    custom_wrap_pending=selected_end==loop_points.end_exclusive;
                }
                const auto sample_format=static_cast<AVSampleFormat>(frame->format);
                if(!convert){
                    input_format=sample_format;
                    int rc=swr_alloc_set_opts2(&convert,&codec->ch_layout,AV_SAMPLE_FMT_S16,int(rate),
                                              &codec->ch_layout,sample_format,int(rate),0,nullptr);
                    if(rc<0)return ff_fail(error,"replacement PCM converter",rc);
                    rc=swr_init(convert);if(rc<0)return ff_fail(error,"replacement PCM converter init",rc);
                }else if(input_format!=sample_format)return fail(error,"music: replacement sample format changed");
                const int capacity=swr_get_out_samples(convert,frame->nb_samples);
                if(capacity<=0||capacity>65568)return fail(error,"music: replacement PCM chunk exceeds bound");
                std::vector<std::int16_t> pcm(std::size_t(capacity)*channels);
                std::uint8_t* output=reinterpret_cast<std::uint8_t*>(pcm.data());
                const int count=swr_convert(convert,&output,capacity,const_cast<const std::uint8_t**>(frame->extended_data),frame->nb_samples);
                av_frame_unref(frame);
                if(count<0)return ff_fail(error,"replacement PCM conversion",count);
                if(count==0)continue;
                if(custom_loop&&std::size_t(count)!=frame_samples)return fail(error,"music: custom FLAC PCM conversion changed sample count");
                if(!custom_loop)take=std::size_t(count);
                if(pending.size()+take*2u>262144u)return fail(error,"music: replacement pending PCM exceeds bound");
                if(channels==2)pending.insert(pending.end(),pcm.begin()+std::ptrdiff_t(offset*2u),pcm.begin()+std::ptrdiff_t((offset+take)*2u));
                else for(std::size_t i=offset;i<offset+take;++i){pending.push_back(pcm[i]);pending.push_back(pcm[i]);}
                decoded_since_seek+=std::uint64_t(count);
                if(!custom_loop&&expected_samples&&decoded_since_seek>expected_samples)return fail(error,"music: decoded replacement exceeds declared samples");
                return true;
            }
            if(got==AVERROR_EOF){
                if(custom_loop)return fail(error,"music: FLAC ended before custom loop boundary");
                if(!decoded_since_seek||(expected_samples&&decoded_since_seek!=expected_samples))
                    return fail(error,"music: empty or truncated replacement PCM");
                if(!loop){finished=true;return true;}
                const auto start=format->streams[stream]->start_time;
                const int rc=av_seek_frame(format,stream,start==AV_NOPTS_VALUE?0:start,AVSEEK_FLAG_BACKWARD);
                if(rc<0)return ff_fail(error,"replacement loop seek",rc);
                avcodec_flush_buffers(codec);swr_free(&convert);input_format=AV_SAMPLE_FMT_NONE;
                draining=false;decoded_since_seek=0;++loops;continue;
            }
            if(got!=AVERROR(EAGAIN))return ff_fail(error,"replacement decode",got);
            if(draining)return fail(error,"music: replacement decoder stalled while draining");
            const int read=av_read_frame(format,packet);
            if(read==AVERROR_EOF){
                const int rc=avcodec_send_packet(codec,nullptr);draining=true;
                if(rc<0&&rc!=AVERROR_EOF)return ff_fail(error,"replacement drain",rc);
                continue;
            }
            if(read<0)return ff_fail(error,"replacement packet read",read);
            if(packet->stream_index!=stream){av_packet_unref(packet);continue;}
            if(packet->flags&AV_PKT_FLAG_CORRUPT){av_packet_unref(packet);return fail(error,"music: corrupt replacement packet");}
            if(packet->size<0||packet->size>1024*1024){av_packet_unref(packet);return fail(error,"music: replacement packet exceeds bound");}
            const int rc=avcodec_send_packet(codec,packet);av_packet_unref(packet);
            if(rc<0)return ff_fail(error,"replacement packet decode",rc);
        }
        return fail(error,"music: replacement decode work exceeds bound");
    }
#endif
#if defined(OR2_HAVE_VORBIS)
    OggVorbis_File vf{};
    static std::size_t read(void* p,std::size_t size,std::size_t n,void* user){
        auto& s=*static_cast<Impl*>(user);const std::size_t want=size*n,left=s.file.size()-s.read_at,take=std::min(want,left);
        std::memcpy(p,s.file.data()+s.read_at,take);s.read_at+=take;return size?take/size:0;}
    static int seek(void* user,ogg_int64_t offset,int whence){
        auto& s=*static_cast<Impl*>(user);std::int64_t to=offset;
        if(whence==SEEK_CUR)to+=std::int64_t(s.read_at);else if(whence==SEEK_END)to+=std::int64_t(s.file.size());
        if(to<0||to>std::int64_t(s.file.size()))return -1;s.read_at=std::size_t(to);return 0;}
    static long tell(void* user){return long(static_cast<Impl*>(user)->read_at);}
#endif
    void close(){
#if defined(OR2_HAVE_VORBIS)
        if(open&&!replacement)ov_clear(&vf);
#endif
#if defined(OUTRUN_WITH_FFMPEG)
        swr_free(&convert);av_frame_free(&frame);av_packet_free(&packet);avcodec_free_context(&codec);
        avformat_close_input(&format);
        // avformat does not own custom AVIO; it may have replaced its buffer.
        if(io){av_freep(&io->buffer);avio_context_free(&io);}
        stream=-1;input_format=AV_SAMPLE_FMT_NONE;draining=false;finished=false;decoded_since_seek=0;expected_samples=0;
        replacement_position=0;replacement_fraction=0;
        loop_points={};custom_loop=false;custom_wrap_pending=false;custom_seek_pending=false;custom_seek_seen=false;
        custom_next_sample=0;custom_seek_discarded=0;
#endif
        open=false;replacement=false;ended=false;pending.clear();base=0;position=0;file.clear();read_at=0;
    }
};
MusicStream::MusicStream():impl_(new Impl){}
MusicStream::~MusicStream(){impl_->close();}
bool MusicStream::decoder_available(){
#if defined(OR2_HAVE_VORBIS)
    return true;
#else
    return false;
#endif
}
bool MusicStream::open(std::vector<std::uint8_t> file,bool loop,std::string& error){
    auto& s=*impl_;s.close();
    error.clear();
    const bool flac=magic(file,0,"fLaC",4),wav=magic(file,0,"RIFF",4);
    if(flac||wav){
        const auto selected=flac?MusicFileFormat::Flac:MusicFileFormat::Wav;
        if(!validate_music_file(selected,file,error))return false;
#if defined(OUTRUN_WITH_FFMPEG)
        if(flac&&!parse_music_flac_loop_points(file,music_flac_loop_points_enabled(),s.loop_points,error))return false;
        s.file=std::move(file);s.loop=loop;
        if(!s.open_replacement(selected,error)){s.close();return false;}
        s.open=true;stats.rate=s.rate;stats.channels=s.channels;++stats.opens;return true;
#else
        return fail(error,"music: WAV/FLAC decoder unavailable in this build");
#endif
    }
#if defined(OR2_HAVE_VORBIS)
    s.file=std::move(file);s.read_at=0;
    const ov_callbacks cb{&Impl::read,&Impl::seek,nullptr,&Impl::tell};
    if(ov_open_callbacks(&s,&s.vf,nullptr,0,cb)!=0){s.file.clear();return fail(error,"music: not an Ogg Vorbis stream");}
    const vorbis_info* info=ov_info(&s.vf,-1);
    if(!info||info->channels<1||info->channels>2||info->rate<8000||info->rate>192000){ov_clear(&s.vf);s.file.clear();return fail(error,"music: unsupported Vorbis layout");}
    s.open=true;s.loop=loop;s.rate=std::uint32_t(info->rate);s.channels=std::uint32_t(info->channels);
    stats.rate=s.rate;stats.channels=s.channels;++stats.opens;
    return true;
#else
    (void)file;(void)loop;
    return fail(error,"music: no Vorbis decoder in this build");
#endif
}
void MusicStream::stop(){if(impl_->open)++stats.stops;impl_->close();}
bool MusicStream::playing()const{return impl_->open&&!impl_->ended;}
bool MusicStream::refill(std::string& error){
#if defined(OR2_HAVE_VORBIS)
    auto& s=*impl_;
    char raw[8192];int section=0;
    for(int attempt=0;attempt<4;++attempt){
        const long got=ov_read(&s.vf,raw,sizeof raw,0,2,1,&section);
        if(got>0){
            const std::size_t samples=std::size_t(got)/2u;const auto* p=reinterpret_cast<const std::int16_t*>(raw);
            if(s.channels==2)s.pending.insert(s.pending.end(),p,p+samples);
            else for(std::size_t i=0;i<samples;++i){s.pending.push_back(p[i]);s.pending.push_back(p[i]);}
            return true;
        }
        if(got==0){
            if(!s.loop){s.ended=true;++stats.ends;return true;}
            if(ov_raw_seek(&s.vf,0)!=0)return fail(error,"music: loop seek failed");
            ++stats.loops;continue;
        }
        if(got==OV_HOLE)continue;           // interruption in the data: skip
        return fail(error,"music: Vorbis decode error");
    }
    return fail(error,"music: no data after a loop seek");
#else
    return fail(error,"music: no Vorbis decoder in this build");
#endif
}
bool MusicStream::mix_add(double* acc,std::size_t frames,double gain,std::string& error){
    auto& s=*impl_;
#if defined(OUTRUN_WITH_FFMPEG)
    if(s.open&&s.replacement){
        if(frames>96000u||(!acc&&frames)){s.close();return fail(error,"music: replacement mix request exceeds bound");}
        for(std::size_t f=0;f<frames;++f){
            const auto index=s.replacement_position;
            while(!s.finished&&(index+2u-s.base)*2u>s.pending.size()){
                const auto consumed=std::min(index-s.base,s.pending.size()/2u);
                if(consumed){s.pending.erase(s.pending.begin(),s.pending.begin()+std::ptrdiff_t(consumed*2u));s.base+=consumed;}
                if(!s.refill_replacement(stats.loops,error)){s.close();return false;}
            }
            const auto have=s.pending.size()/2u;
            if(index-s.base>=have){++stats.ends;s.close();break;}
            const auto i0=index-s.base,i1=std::min(i0+1u,have-1u);
            const double fraction=double(s.replacement_fraction)/48000.0;
            for(std::size_t c=0;c<2;++c)
                acc[f*2u+c]+=(s.pending[i0*2u+c]+(s.pending[i1*2u+c]-s.pending[i0*2u+c])*fraction)*gain;
            const auto next=s.replacement_fraction+s.rate;
            s.replacement_position+=next/48000u;s.replacement_fraction=next%48000u;++stats.frames;
        }
        return true;
    }
#endif
    if(!s.open||s.ended)return true;
    const double step=double(s.rate)/48000.0;
    for(std::size_t f=0;f<frames;++f){
        const std::size_t index=std::size_t(s.position);
        // frames index and index+1 (relative to base) must be decoded
        while(!s.ended&&(index+2u-s.base)*2u>s.pending.size())if(!refill(error))return false;
        const std::size_t have=s.pending.size()/2u;
        if(index-s.base>=have)break;                                   // stream finished
        const std::size_t i0=index-s.base,i1=std::min(i0+1u,have-1u);
        const double fraction=s.position-double(index);
        const double l=s.pending[i0*2]+(s.pending[i1*2]-s.pending[i0*2])*fraction;
        const double r=s.pending[i0*2+1]+(s.pending[i1*2+1]-s.pending[i0*2+1])*fraction;
        acc[f*2]+=l*gain;acc[f*2+1]+=r*gain;
        s.position+=step;++stats.frames;
    }
    // drop the consumed source frames
    const std::size_t consumed=std::size_t(s.position)-s.base;
    if(consumed>4096u&&consumed*2u<=s.pending.size()){
        s.pending.erase(s.pending.begin(),s.pending.begin()+std::ptrdiff_t(consumed*2u));s.base+=consumed;
    }
    if(s.ended&&std::size_t(s.position)-s.base>=s.pending.size()/2u)s.close();
    return true;
}
}
