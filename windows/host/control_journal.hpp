#pragma once
// The control journal (windows/docs/CONTROL-JOURNAL.md): bounded, identity-bearing JSON lines in a per-process
// folder. Every handle operation (create, write, flush, close) runs on the owned control worker, never in a
// constructor and never on the input thread.
//
// Spike limitation (option A, Astra 16313): any sink failure, including the finite 2 MiB ordinary cap, fails the
// pump; the host then reports control_complete=false and its reviewed unresolved policy ends an ordinary process with
// the forced exit at shutdown. That is not player behaviour. A production policy (counted drops, option B) needs its
// own review. The cap is finite and can be exhausted; the proof keeps a cap-exhaustion case.
#include <windows.h>
#include "control_snapshot.hpp"
#include "json_text.hpp"
#include "observer_domain.hpp"
#include "sha256.hpp"
#include <array>
#include <functional>
#include <memory>
#include <thread>

namespace dbce::shim::control {

// Built once off the input tick, before admission and before either header is published; the observer is given
// the same `id` (CONTROL-JOURNAL.md section 2). The journal never hashes or re-reads anything itself.
struct JournalIdentity {
    observe::Identity id;                       // process, start, nonce, devices, executable, build, profile
    bool latched{};
    std::string nonce_text;                     // the fence nonce, latched only
    std::string profile_revision, core_version, adapter_version;
    std::string input_source;                  // optional versioned external provider identity; not a sample capture
    std::vector<DeviceInfo> devices;            // the immutable profile device table, in index order
    std::string start_line;                     // the human start line (also in runtime.log), split into rows
    // False until the shared identity builder hashes them; the journal then writes null, never an invented value.
    bool executable_known{true},build_known{true};
};

constexpr std::size_t LatchedJournalCap=std::size_t(8)<<20, OrdinaryJournalCap=std::size_t(2)<<20;
struct JournalLimits {
    std::size_t line_bytes=8192;                // a whole line, identity included
    std::size_t chunk_bytes=65536;              // one WriteFile; only whole lines
    std::size_t field_bytes=3072;               // an escaped reason, detail or start-line row (512 raw bytes of controls)
    std::size_t name_bytes=1560;                // an escaped device name (260 UTF-16 units, all controls)
    std::size_t small_bytes=128;                // versions, revision, nonce text, status and kind names
    std::size_t cap_bytes=LatchedJournalCap;
    std::size_t reserve_bytes=4096;             // the final counters and end lines
    // Test seam: one write of a whole chunk; returns false or fewer bytes on a fault. The default is WriteFile.
    std::function<bool(HANDLE,const char*,DWORD,DWORD&)> write;
};

namespace journal_detail {
using dbce::shim::json_string;   // shim/json_text.hpp: shared with the ordinary DLL path
inline std::string num(std::uint64_t v){return std::to_string(v);}
inline std::string snum(std::int64_t v){return std::to_string(v);}
inline std::uint64_t filetime_now(){FILETIME f;GetSystemTimeAsFileTime(&f);return (std::uint64_t(f.dwHighDateTime)<<32)|f.dwLowDateTime;}
}

class JournalFile {
    std::wstring folder_; bool create_folder_; std::shared_ptr<const JournalIdentity> id_; JournalLimits lim_;
    HANDLE file_{INVALID_HANDLE_VALUE};
    bool opened_{},failed_{},finished_{},complete_{};
    std::string failure_,pending_,prefix_;
    std::thread::id worker_{};
    std::uint64_t seq_{},drain_{},bytes_{},empty_skipped_{},last_eseq_{},pending_seq_{};
    bool have_counters_{},have_eseq_{};
    std::array<std::uint64_t,6> counters_{};

    bool fail(const std::string& why){if(!failed_){failed_=true;failure_=bounded_diagnostic(why);}return false;}
    bool on_worker(){
        if(worker_==std::thread::id{})worker_=std::this_thread::get_id();
        return worker_==std::this_thread::get_id();
    }
    bool write_raw(const std::string& bytes){
        if(bytes.empty())return true;
        DWORD done=0;
        const bool ok=lim_.write?lim_.write(file_,bytes.data(),DWORD(bytes.size()),done):WriteFile(file_,bytes.data(),DWORD(bytes.size()),&done,nullptr)!=FALSE;
        if(!ok||done!=bytes.size())return fail("journal write failed or was short");
        bytes_+=done;return true;
    }
    // One whole line into the pending chunk. The identity prefix and seq/drain come first.
    bool line(const std::string& body){
        std::string l="{"+prefix_+",\"seq\":"+journal_detail::num(seq_)+",\"drain\":"+journal_detail::num(drain_)+","+body+"}\n";
        if(l.size()>lim_.line_bytes)return fail("journal line above its bound");
        if(pending_.size()+l.size()>lim_.chunk_bytes&&!flush())return false;
        if(pending_.empty())pending_seq_=seq_;
        pending_+=l;++seq_;return true;
    }
    // Write the pending chunk unless it would cross the cap; then the reserved tail ends the journal incomplete.
    bool flush(){
        if(failed_)return false;
        if(bytes_+pending_.size()+lim_.reserve_bytes>lim_.cap_bytes){
            if(!pending_.empty())seq_=pending_seq_;   // the unwritten lines give their seq numbers back: no gap
            pending_.clear();
            std::string tail;
            if(counters_line(tail)&&end_line(tail,false,"cap"))(void)write_raw(tail);
            return fail("journal cap reached");
        }
        const bool ok=write_raw(pending_);pending_.clear();return ok;
    }
    std::string counters_body()const{
        using journal_detail::num;
        return "\"type\":\"counters\",\"asOf\":\"drain\",\"statusDropped\":"+num(counters_[0])+",\"statusCoalesced\":"+num(counters_[1])+
            ",\"eventsDropped\":"+num(counters_[2])+",\"controlEventsDropped\":"+num(counters_[3])+",\"batchesOffered\":"+num(counters_[4])+
            ",\"identicalSkipped\":"+num(counters_[5])+",\"emptyDrainsSkipped\":"+num(empty_skipped_)+
            ",\"recorderOverruns\":{\"available\":false},\"observerPublished\":{\"available\":false}";
    }
    bool counters_line(std::string& into){return raw_line(into,counters_body());}
    bool end_line(std::string& into,bool complete,const char* reason){
        using journal_detail::num;
        std::string body="\"type\":\"end\",\"complete\":"+std::string(complete?"true":"false")+",\"lines\":"+num(seq_+1)+",\"bytes\":"+num(bytes_+into.size());
        if(have_eseq_)body+=",\"lastEseq\":"+num(last_eseq_);
        if(reason){body+=",\"reason\":";journal_detail::json_string(body,reason,lim_.small_bytes);}
        return raw_line(into,body);
    }
    // A tail line bypasses the chunk/cap logic (its room is reserved), still bounded per line.
    bool raw_line(std::string& into,const std::string& body){
        std::string l="{"+prefix_+",\"seq\":"+journal_detail::num(seq_)+",\"drain\":"+journal_detail::num(drain_)+","+body+"}\n";
        if(l.size()>lim_.line_bytes)return false;
        into+=l;++seq_;return true;
    }
    bool render_prefix(){
        using journal_detail::json_string;using journal_detail::num;
        const auto& i=*id_;
        prefix_="\"v\":1,\"pid\":"+num(i.id.process)+",\"created\":"+num(i.id.process_start);
        if(!i.input_source.empty()){
            prefix_+=",\"inputSource\":";
            if(!json_string(prefix_,i.input_source,lim_.small_bytes))return false;
        }
        if(i.latched)prefix_+=",\"nonce\":\""+hex(i.id.nonce.data(),i.id.nonce.size())+"\"";
        prefix_+=",\"profileSha256\":\""+hex(i.id.profile)+"\",\"build\":{\"core\":";
        if(!json_string(prefix_,i.core_version,lim_.small_bytes))return false;
        prefix_+=",\"adapter\":";
        if(!json_string(prefix_,i.adapter_version,lim_.small_bytes))return false;
        prefix_+=",\"executableSha256\":"+(i.executable_known?"\""+hex(i.id.executable)+"\"":std::string("null"))+
                 ",\"buildSha256\":"+(i.build_known?"\""+hex(i.id.build)+"\"":std::string("null"))+"}";
        return true;
    }
    bool open_journal(){
        using journal_detail::json_string;using journal_detail::num;
        opened_=true;
        if(!id_)return fail("journal identity missing");
        if(id_->devices.size()!=id_->id.profile_devices)return fail("journal device table differs from the identity");
        if(!render_prefix())return fail("journal identity field above its bound or not UTF-8");
        if(create_folder_&&!CreateDirectoryW(folder_.c_str(),nullptr))return fail("journal folder exists or cannot be created ("+num(GetLastError())+")");
        const DWORD attributes=GetFileAttributesW(folder_.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return fail("journal folder missing or a link");
        const std::wstring path=folder_+L"\\controls-journal.jsonl";
        file_=CreateFileW(path.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file_==INVALID_HANDLE_VALUE)return fail("journal file exists or cannot be created ("+num(GetLastError())+")");
        const auto& i=*id_;
        std::string body="\"type\":\"header\",\"mode\":\""+std::string(i.latched?"latched":"ordinary")+"\",\"devices\":"+num(i.devices.size())+
            ",\"limits\":{\"line\":"+num(lim_.line_bytes)+",\"chunk\":"+num(lim_.chunk_bytes)+",\"cap\":"+num(lim_.cap_bytes)+"},\"sinkCreated\":"+num(journal_detail::filetime_now())+",\"profileRevision\":";
        if(!json_string(body,i.profile_revision,lim_.small_bytes))return fail("journal profile revision above its bound or not UTF-8");
        if(i.latched){body+=",\"nonceText\":";if(!json_string(body,i.nonce_text,lim_.small_bytes))return fail("journal nonce text above its bound or not UTF-8");}
        if(!line(body))return false;
        for(std::size_t d=0;d<i.devices.size();++d){
            std::string row="\"type\":\"device\",\"index\":"+num(d)+",\"instance\":";
            bool ok=json_string(row,i.devices[d].dev,lim_.small_bytes);
            row+=",\"product\":";ok=ok&&json_string(row,i.devices[d].prod,lim_.small_bytes);
            row+=",\"name\":";ok=ok&&json_string(row,i.devices[d].name,lim_.name_bytes);
            if(!ok)return fail("journal device row above its bound or not UTF-8");
            if(!line(row))return false;
        }
        std::size_t k=0,start=0;
        while(start<=i.start_line.size()&&!i.start_line.empty()){
            const auto stop=i.start_line.find('\n',start);
            const auto text=i.start_line.substr(start,stop==std::string::npos?std::string::npos:stop-start);
            std::string row="\"type\":\"startline\",\"index\":"+num(k++)+",\"text\":";
            if(!json_string(row,text,lim_.field_bytes))return fail("journal start-line row above its bound or not UTF-8");
            if(!line(row))return false;
            if(stop==std::string::npos)break;
            start=stop+1;
        }
        return flush();
    }
    static const char* of_name(StatusChange::Of of){return of==StatusChange::Of::Action?"action":of==StatusChange::Of::Device?"device":"run";}

public:
    // Latched: `folder` is the fence's run folder (exists). Ordinary: a new per-process folder this call creates.
    JournalFile(std::wstring folder,bool create_folder,std::shared_ptr<const JournalIdentity> id,JournalLimits limits)
        :folder_(std::move(folder)),create_folder_(create_folder),id_(std::move(id)),lim_(std::move(limits)){}
    JournalFile(const JournalFile&)=delete;JournalFile& operator=(const JournalFile&)=delete;
    // The last owner is the control worker (it finishes, or an abandoned worker holds its state until it exits, D9);
    // a worker that never started never opened a handle. Nothing here waits.
    ~JournalFile(){if(file_!=INVALID_HANDLE_VALUE)CloseHandle(file_);}

    // Worker only. True only when every record of `j` is written; a fault latches and is never retried.
    bool append(const Journal& j){
        using journal_detail::json_string;using journal_detail::num;using journal_detail::snum;
        if(finished_||failed_)return false;
        if(!on_worker())return fail("journal used off its worker");
        if(!opened_&&!open_journal())return false;
        const std::array<std::uint64_t,6> now{j.status_dropped,j.status_coalesced,j.events_dropped,j.control_events_dropped,j.batches_offered,j.identical_skipped};
        if(j.events.empty()&&j.status.empty()&&have_counters_&&now==counters_){++empty_skipped_;return true;}   // the skip count is not compared
        ++drain_;
        for(const auto& e:j.events){
            std::string body="\"type\":\"event\",\"eseq\":"+num(e.seq)+",\"us\":"+snum(e.us)+",\"kind\":"+num(std::uint64_t(e.kind))+",\"kindName\":";
            if(!json_string(body,event_name(e.kind),lim_.small_bytes))return fail("journal event kind name");
            body+=",\"device\":"+snum(e.device)+",\"epoch\":"+num(e.epoch)+",\"batch\":"+num(e.batch)+",\"detail\":";
            if(!json_string(body,e.detail,lim_.field_bytes))return fail("journal event detail above its bound or not UTF-8");
            if(!line(body))return false;
            last_eseq_=e.seq;have_eseq_=true;
        }
        for(const auto& s:j.status){
            std::string body="\"type\":\"status\",\"us\":"+snum(s.us)+",\"of\":\""+of_name(s.of)+"\",\"index\":"+snum(s.index);
            if(s.of==StatusChange::Of::Action&&s.index>=0&&s.index<ActionCount){body+=",\"action\":";if(!json_string(body,action_name(s.index),lim_.small_bytes))return fail("journal action name");}
            body+=",\"status\":";
            if(!json_string(body,s.status,lim_.small_bytes))return fail("journal status above its bound or not UTF-8");
            body+=",\"reason\":";
            if(!json_string(body,s.reason,lim_.field_bytes))return fail("journal status reason above its bound or not UTF-8");
            body+=",\"device\":"+snum(s.device)+",\"epoch\":"+num(s.epoch);
            if(!line(body))return false;
        }
        counters_=now;have_counters_=true;
        if(!line(counters_body()))return false;
        return flush();
    }
    // Worker only, once. Complete only if no fault latched and the end line, flush and close all succeed.
    bool finish(){
        if(finished_)return complete_;
        finished_=true;
        bool ok=on_worker()||fail("journal finished off its worker");
        if(ok&&!opened_)ok=open_journal();
        if(ok&&!failed_){std::string tail;ok=end_line(tail,true,nullptr)&&write_raw(tail);}
        if(ok&&!failed_&&!FlushFileBuffers(file_))ok=fail("journal flush failed");
        if(file_!=INVALID_HANDLE_VALUE){if(!CloseHandle(file_))ok=fail("journal close failed");file_=INVALID_HANDLE_VALUE;}
        complete_=ok&&!failed_;
        return complete_;
    }
    bool failed()const{return failed_;}
    const std::string& failure()const{return failure_;}
};

// The concrete control Io: bounded snapshot reads of inject.on / inject.txt plus the journal sink.
class FileIo final : public Io {
    std::wstring session_,batch_;
    JournalFile journal_;
public:
    FileIo(std::wstring session_path,std::wstring batch_path,std::wstring journal_folder,bool create_folder,
           std::shared_ptr<const JournalIdentity> id,JournalLimits limits)
        :session_(std::move(session_path)),batch_(std::move(batch_path)),journal_(std::move(journal_folder),create_folder,std::move(id),std::move(limits)){}
    Read read_session()override{return read_snapshot(session_,512);}
    Read read_batch()override{return read_snapshot(batch_,MaxBatchBytes);}
    std::uint64_t unix_seconds()override{
        FILETIME f;GetSystemTimeAsFileTime(&f);
        const std::uint64_t t=(std::uint64_t(f.dwHighDateTime)<<32)|f.dwLowDateTime;
        return (t-116444736000000000ULL)/10000000ULL;
    }
    bool append(const Journal& j)override{return journal_.append(j);}
    bool finish()override{return journal_.finish();}
    const JournalFile& journal()const{return journal_;}
};

// The ordinary per-process folder name, under the dbce folder (CONTROL-JOURNAL.md section 1).
inline std::wstring ordinary_journal_folder(const std::wstring& dbce,DWORD pid,std::uint64_t created){
    return dbce+L"\\controls-"+std::to_wstring(pid)+L"-"+std::to_wstring(created);
}
}
