#pragma once
// The recorder's capture composition, moved from tests/queued_frames_spike/native.cpp (Astra 16553): a fixed channel
// schema per capture, its worst-case preflight, packets from channels, the capture identity (a fresh nonce and the
// closed file's size and SHA-256), the session-writer sink run only on the worker, and the worker report member.
// Members are created new (CREATE_NEW) and never overwritten. Failures throw; nothing is silently trimmed.
#include <windows.h>
#include <bcrypt.h>
#include "frame_channels.hpp"
#include "sha256.hpp"
#include "worker.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <locale>
#include <memory>
#include <thread>
#include <vector>

namespace dbce::shim::recorder {
namespace fs=std::filesystem;

inline std::vector<std::string> schema(const Channels& c){
    if(c.empty()||c.size()>MaxValues)throw std::invalid_argument("complete schema exceeds channel budget");
    std::vector<std::string> names;
    for(const auto& v:c){if(!dbce::session::ascii(v.first,256,true))throw std::invalid_argument("channel name");names.push_back(v.first);}
    return names;
}
inline std::size_t row_bound(const std::vector<std::string>& names){
    // Writer uses precision 17 and defaultfloat: finite binary64 needs fewer than 32 ASCII bytes.
    // 192 covers the fixed JSON syntax, uint64 sequence, elapsed binary64 and newline.
    std::size_t bytes=192;for(const auto& name:names)bytes+=dbce::session::quote(name).size()+34;return bytes;
}
inline void preflight(const std::vector<std::string>& names,double hz,double seconds,dbce::session::Limits limits={}){
    if(names.empty()||names.size()>1024||!std::isfinite(hz)||hz<=0||!std::isfinite(seconds)||seconds<=0||
       !std::isfinite(limits.seconds)||limits.seconds<=0||limits.seconds>86400||seconds>limits.seconds||
       limits.bytes<16384||limits.bytes>1024ull*1024*1024||limits.recordBytes<1024||limits.recordBytes>1024*1024)
        throw std::invalid_argument("schema/rate/duration/limits bound");
    const long double frame_count=std::ceil(static_cast<long double>(hz)*seconds)+1;
    if(frame_count>=static_cast<long double>(UINT64_MAX))throw std::invalid_argument("frame bound overflow");
    const auto frames=static_cast<std::uint64_t>(frame_count);
    const auto row=row_bound(names);
    // Metadata is separately bounded below 8192, plus the writer's 8192 footer reserve.
    if(row>limits.recordBytes||limits.bytes<16384||frames>(limits.bytes-16384)/row)
        throw std::invalid_argument("requested capture exceeds worst-case file budget");
}
inline Packet packet_of(const Channels& c,const std::vector<std::string>& names,std::uint64_t sequence,std::int64_t source_us){
    if(c.size()!=names.size())throw std::invalid_argument("one complete fixed schema per capture");
    Packet p;p.sequence=sequence;p.source_us=source_us;p.width=std::uint32_t(names.size());
    for(std::size_t i=0;i<names.size();++i)p.values[i]=c.at(names[i]);
    return p;
}

// The capture's identity: a fresh nonce, the producer thread (which must never run the writer), and the closed file.
struct Identity {
    std::string nonce,sha;std::uint64_t bytes{};
    const std::thread::id producer=std::this_thread::get_id();std::atomic<unsigned> owner_calls{0};
    Identity(){std::array<std::uint8_t,16> n{};if(BCryptGenRandom(nullptr,n.data(),ULONG(n.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)throw std::runtime_error("nonce failed");nonce=hex(n.data(),n.size());}
    void worker(){if(std::this_thread::get_id()==producer){++owner_calls;throw std::runtime_error("writer called on producer");}}
    void closed(const fs::path& p){
        worker();struct File{HANDLE h;~File(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}} f{
            CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
        LARGE_INTEGER n{};
        if(f.h==INVALID_HANDLE_VALUE||!GetFileSizeEx(f.h,&n)||n.QuadPart<=0||n.QuadPart>1024ll*1024*1024)throw std::runtime_error("closed source size");
        std::vector<std::uint8_t> b(std::size_t(n.QuadPart));DWORD got{};
        if(!ReadFile(f.h,b.data(),DWORD(b.size()),&got,nullptr)||got!=b.size())throw std::runtime_error("closed source read");
        sha=hex(sha256(b.data(),b.size()));bytes=b.size();
    }
};

// The session writer on the worker. `before_write` is a test seam (a fixture holds the writer on a row).
struct QueuedSink final:Sink {
    dbce::session::Writer writer;fs::path path;std::vector<std::string> names;std::shared_ptr<Identity> id;std::int64_t origin;
    dbce::session::Metadata meta;dbce::session::Limits limits;std::function<void()> before_write;
    QueuedSink(fs::path p,std::vector<std::string> n,std::shared_ptr<Identity> i,std::int64_t o,dbce::session::Metadata m,
               dbce::session::Limits l={},std::function<void()> hook={})
        :path(std::move(p)),names(std::move(n)),id(std::move(i)),origin(o),meta(std::move(m)),limits(l),before_write(std::move(hook)){}
    bool open()override{id->worker();meta.properties.emplace("captureNonce",id->nonce);return writer.open(path.wstring(),meta,limits);}
    bool write(const Packet& p)override{
        id->worker();if(before_write)before_write();
        Channels c;for(std::size_t i=0;i<names.size();++i)c[names[i]]=p.values[i];
        return writer.sample(double(p.source_us-origin)/1e6,c);
    }
    bool stop()override{id->worker();if(!writer.stop())return false;id->closed(path);return true;}
    bool stopped_at_limit()const override{return !writer.active()&&(writer.reason()=="durationLimit"||writer.reason()=="fileSizeLimit"||writer.reason()=="recordSizeLimit");}
    void abandon()noexcept override{writer.abandon();}
    std::string reason()const override{return writer.reason();}
};

// A capture member, created new: an existing file is refused, never overwritten.
inline void write_new(const fs::path& path,const std::string& text){
    HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("capture member exists or cannot be created: "+path.string());
    DWORD n=0;const bool ok=WriteFile(h,text.data(),DWORD(text.size()),&n,nullptr)&&n==text.size();
    const bool closed=CloseHandle(h)!=FALSE;
    if(!ok||!closed)throw std::runtime_error("capture member write failed: "+path.string());
}
// The finalizer's validated export (P21Finalizer --seal-export): per row "ROW <n>", then n "<channel> <value>" lines with
// round-trip values. Streamed one row at a time (a long capture is far larger as maps than the file); strict: a malformed
// header, an oversized row, a non-numeric or duplicate channel throws. Returns the row count.
inline std::uint64_t read_export(const fs::path& path,const std::function<void(const Channels&)>& row){
    std::ifstream in(path);in.imbue(std::locale::classic());
    if(!in)throw std::runtime_error("validated export unreadable: "+path.string());
    std::uint64_t rows=0;std::string tag;
    while(in>>tag){
        std::size_t n=0;
        if(tag!="ROW"||!(in>>n)||n==0||n>MaxValues)throw std::runtime_error("export row header at row "+std::to_string(rows));
        Channels c;
        for(std::size_t k=0;k<n;++k){std::string key;double v=0;
            if(!(in>>key>>v)||!c.emplace(key,v).second)throw std::runtime_error("export channel at row "+std::to_string(rows));}
        row(c);++rows;
    }
    if(!in.eof())throw std::runtime_error("export read failed after row "+std::to_string(rows));
    return rows;
}
// The worker's final, quiescent report, only for an eligible result.
inline void report(const fs::path& root,const Report& r,const Identity& id,std::uint64_t first,std::int64_t origin){
    if(!(r.may_seal()&&r.capture_counts_final&&!r.first_rejected&&r.wrong_producer==0))throw std::runtime_error("eligible actual worker result required");
    auto s=dbce::session::jsonStream();
    s<<int(r.outcome)<<'\t'<<r.accepted<<'\t'<<r.written<<'\t'<<r.attempted<<'\t'<<r.declared_last<<'\t'<<r.last_written
     <<'\t'<<first<<'\t'<<origin<<'\t'<<id.nonce<<'\t'<<id.bytes<<'\t'<<id.sha<<'\t'<<r.producer_done<<'\t'<<r.worker_done
     <<'\t'<<r.drain_timed_out<<'\t'<<r.queue_full<<'\t'<<r.invalid<<'\t'<<r.after_failure<<'\t'<<r.wrong_producer
     <<'\t'<<r.after_end<<'\t'<<r.after_end_wrong_producer<<'\t'<<r.discarded<<'\t'<<r.first_rejected<<'\t'<<r.sink_reason<<'\n';
    write_new(root/"worker.tsv",s.str());
}
}
