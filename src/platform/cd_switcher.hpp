#pragma once
// Optional desktop CD catalog. No game/decoder/input-record layouts change.
// All state and callbacks run on the game thread; the audio callback takes its
// existing audio lock. Off/unbound: both recovered race call sites fall through.
#include "platform/music_stream.hpp"
#include "platform/cd_title.hpp"
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace outrun::platform {
struct CdTrack { std::string path,title; };
struct CdCatalog { std::vector<CdTrack> tracks; std::string error; };
inline std::string_view cd_trim(std::string_view v){
    while(!v.empty()&&std::isspace(static_cast<unsigned char>(v.front())))v.remove_prefix(1);
    while(!v.empty()&&std::isspace(static_cast<unsigned char>(v.back())))v.remove_suffix(1);
    return v;
}
inline CdCatalog parse_cd_tracks(const std::vector<std::string>& lines){
    CdCatalog out;std::size_t bytes=0;
    if(lines.size()>4096u){out.error="more than 4096 CDTracks lines";return out;}
    for(const auto& raw:lines){
        if(raw.size()>1024u*1024u-bytes){out.tracks.clear();out.error="CDTracks exceeds 1 MiB";return out;}
        bytes+=raw.size();const auto line=cd_trim(raw);
        if(line.empty()||line.front()=='#'||line.front()==';')continue;
        const auto equal=line.find('=');
        if(equal==std::string_view::npos){out.tracks.clear();out.error="CDTracks line has no =";return out;}
        const auto path=cd_trim(line.substr(0,equal)),title=cd_trim(line.substr(equal+1));
        if(path.empty()||path.size()>4095u||title.size()>512u||out.tracks.size()==256u||
           path.find('\0')!=std::string_view::npos||title.find('\0')!=std::string_view::npos){
            out.tracks.clear();out.error="CDTracks path/title/count outside bounds";return out;}
        // Do not sort/deduplicate, lowercase a path, or use a title as a filename.
        out.tracks.push_back({std::string(path),std::string(title)});
    }
    return out;
}
// Low bits use runtime::PadButton's platform-neutral layout (host_platform.hpp).
// These extra bits belong only to the switcher's derived sample, never a host,
// game input or recording record. Tweaks game.hpp supplies the alias names.
enum CdStickBits : std::uint64_t {
    CdLsUp=1ull<<16,CdLsDown=1ull<<17,CdLsLeft=1ull<<18,CdLsRight=1ull<<19,
    CdRsUp=1ull<<20,CdRsDown=1ull<<21,CdRsLeft=1ull<<22,CdRsRight=1ull<<23
};
inline bool parse_cd_combo(std::string_view text,std::uint64_t& mask){
    mask=0;if(text.size()>128u)return false;
    struct Named { const char* name;unsigned bit; };
    static constexpr Named names[]={{"a",0},{"b",1},{"x",2},{"y",3},{"ls",4},{"rs",5},
        {"lb",6},{"rb",7},{"lt",8},{"rt",9},{"start",10},{"back",11},
        {"left",12},{"up",13},{"right",14},{"down",15},
        {"ls-up",16},{"ls-down",17},{"ls-left",18},{"ls-right",19},
        {"rs-up",20},{"rs-down",21},{"rs-left",22},{"rs-right",23}};
    while(true){
        const auto plus=text.find('+');const auto token=cd_trim(text.substr(0,plus));
        std::string key(token);for(auto& ch:key)ch=char(std::tolower(static_cast<unsigned char>(ch)));
        bool found=false;for(const auto& n:names)if(key==n.name){mask|=std::uint64_t(1)<<n.bit;found=true;break;}
        if(!found){mask=0;return false;}
        if(plus==std::string_view::npos)return mask!=0;
        text.remove_prefix(plus+1);
    }
}
inline unsigned cd_bit_count(std::uint64_t v){unsigned n=0;while(v){v&=v-1;++n;}return n;}
struct CdAction { bool previous{},next{}; };
struct CdEdges {
    bool z{},x{},wait{true};std::uint64_t pad{};
    CdAction step(bool prev_key,bool next_key,std::uint64_t held,bool pad_connected,
                  std::uint64_t prev_combo,std::uint64_t next_combo,bool blocked){
        if(!pad_connected)held=pad=0; // a disconnect is not a release
        if(blocked){wait=true;z=prev_key;x=next_key;pad=held;return {};}
        if(wait){wait=prev_key||next_key||(held&(prev_combo|next_combo))!=0;z=prev_key;x=next_key;pad=held;return {};}
        auto released=[&](std::uint64_t combo){return pad_connected&&combo&&(pad&combo)==combo&&(held&combo)!=combo;};
        bool p=released(prev_combo),n=released(next_combo);
        if(p&&n){if(cd_bit_count(prev_combo)>cd_bit_count(next_combo))n=false;else p=false;}
        CdAction action{(prev_key&&!z)||p,(next_key&&!x)||n};
        z=prev_key;x=next_key;pad=held;return action;
    }
};
// Selection is transactional: failed/absent playback never changes the cursor.
// The caller never consumes any input, including successful switches (like the
// Tweaks keyboard hook). Initial race selection and explicit switches are one-shot.
using CdPlay=bool(*)(void*,const CdTrack&,std::size_t,bool loop);
struct CdSwitcher {
    bool enabled{};std::vector<CdTrack> tracks;std::size_t cursor{};
    void* user{};CdPlay play{};
    CdTitleState title{};
    bool select(std::size_t index){
        if(!enabled||tracks.empty()||!play)return false;
        if(index>=tracks.size())index=0;
        if(!play(user,tracks[index],index,false))return false;
        cursor=index;return true;
    }
    bool change(CdAction action){
        if(!enabled||tracks.empty()||(!action.previous&&!action.next))return false;
        std::size_t index=cursor<tracks.size()?cursor:0;
        if(action.previous)index=index?index-1:tracks.size()-1;
        if(action.next)index=index+1==tracks.size()?0:index+1;
        if(!select(index))return false;
        title.changed(); // only a successful manual change, never initial/race select
        return true; // simultaneous keyboard keys: previous, then next
    }
};
inline CdSwitcher& cd_switcher(){static CdSwitcher state;return state;}
inline bool cd_switcher_race_load(std::uint32_t mode,unsigned track){
    return mode==16u&&cd_switcher().select(track);
}
// The explicit/current-directory file wins over Sound/<path>. Only after that
// choice do enabled same-stem FLAC, then WAV, win. Existence is independent of
// read/decode success: no format fallback after choosing a winner.
struct CdResolved { std::string path;MusicFileFormat format{MusicFileFormat::Vorbis}; };
template<class Exists>
CdResolved resolve_cd_track(const std::string& path,const std::string& sound,MusicReplacementPolicy policy,Exists exists){
    std::string base=path;
    if(!exists(base)){base=sound+"/"+path;if(!exists(base))return {};}
    const auto flac=music_replacement_name(base,MusicFileFormat::Flac);
    const auto wav=music_replacement_name(base,MusicFileFormat::Wav);
    const auto selected=select_music_file(policy,policy.allow_wav&&exists(wav),policy.allow_flac&&exists(flac));
    if(selected!=MusicFileFormat::Vorbis)return {music_replacement_name(base,selected),selected};
    auto dot=base.find_last_of('.');std::string ext=dot==std::string::npos?std::string():base.substr(dot);
    for(auto& ch:ext)ch=char(std::tolower(static_cast<unsigned char>(ch)));
    // Explicit WAV/FLAC catalog entries obey the same opt-ins as replacements.
    if(ext==".wav"&&policy.allow_wav)return {base,MusicFileFormat::Wav};
    if(ext==".flac"&&policy.allow_flac)return {base,MusicFileFormat::Flac};
    if(ext==".ogg")return {base,MusicFileFormat::Vorbis};
    return {};
}
}
