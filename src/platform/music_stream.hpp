#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace outrun::platform {
// PC 401000 / 42FBA0 streamed BGM: ".\Sound\%s" of the track table 771AD8
// (Ogg Vorbis), played on the single stream object 95B24C (a new request
// closes the previous stream; 401030 / 401050 close it). The volume word
// 95B240 is .bss 0 (0 dB, unattenuated) and a stream is only started while it
// is above -10000. Ogg decoding is libvorbisfile; optional replacements use
// static FFmpeg. The device side is the shared
// 48 kHz stereo mix of MenuAudio (linear resampling from the file rate).
const char* music_track_name_771ad8(unsigned track);   // nullptr outside 0..0x42
constexpr unsigned MusicTrackCount=0x43u;

// Experimental replacement policy, set once by the desktop startup settings.
// Qualification defaults are off (Tweaks defaults both on). No file is rewritten.
struct MusicReplacementPolicy { bool allow_wav{},allow_flac{}; };
enum class MusicFileFormat { Vorbis,Wav,Flac };
void set_music_replacement_policy(MusicReplacementPolicy policy);
MusicReplacementPolicy music_replacement_policy();
MusicFileFormat select_music_file(MusicReplacementPolicy policy,bool wav_exists,bool flac_exists);
std::string music_replacement_name(const std::string& original,MusicFileFormat format);
// Selected replacement failures must not retry another extension. Checks magic,
// the 64 MiB input bound and configured, bounded FLAC loop comments before decode.
bool validate_music_file(MusicFileFormat format,const std::vector<std::uint8_t>& bytes,std::string& error);
bool music_replacement_decoder_available(MusicFileFormat format);
// Separate opt-in config; does not change MusicReplacementPolicy/Stats/object ABI.
void set_music_flac_loop_points_enabled(bool enabled);
bool music_flac_loop_points_enabled();
struct MusicFlacLoopPoints {
    bool enabled{},length_compatibility{};
    std::uint64_t total_frames{},start{},end_exclusive{};
};
// Strict bounded native-FLAC comments. Last duplicate wins only when every value
// is valid. LENGTH overrides END and deliberately retains Tweaks' exclusive
// end=start+length-1 quirk; LENGTH<2 and invalid/unknown ranges refuse.
bool parse_music_flac_loop_points(const std::vector<std::uint8_t>& bytes,bool allow,
                                 MusicFlacLoopPoints& points,std::string& error);

class MusicStream {
public:
    MusicStream();
    ~MusicStream();
    MusicStream(const MusicStream&)=delete;
    MusicStream& operator=(const MusicStream&)=delete;
    // Takes the whole file (the retail Sound folder); false with the reason
    // when the file is invalid or its decoder was not built. WAV/FLAC use an
    // optional static FFmpeg backend; Ogg keeps the existing Vorbis route.
    // Opted-in valid FLAC points apply only to loop=true: intro [0,end), then
    // [start,end). loop=false retains full-file playback after tag validation.
    bool open(std::vector<std::uint8_t> file,bool loop,std::string& error);
    void stop();
    bool playing()const;
    // Adds `frames` 48 kHz stereo frames (scaled by gain) to acc (interleaved).
    bool mix_add(double* acc,std::size_t frames,double gain,std::string& error);
    struct Stats { std::uint64_t opens{},stops{},frames{},loops{},ends{}; std::uint32_t rate{},channels{}; } stats;
    static bool decoder_available();
private:
    bool refill(std::string& error);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
