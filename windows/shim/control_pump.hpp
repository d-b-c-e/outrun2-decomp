#pragma once
// One control-worker pass. All file operations stay behind Io, off the input
// thread. Core owns admission/order/TTL and terminal neutrality. No game APIs.
#include "shim.hpp"
#include <stdexcept>
#include <thread>

namespace dbce::shim::control {
enum class ReadState { Missing, Bytes, Error };
struct Read { ReadState state{ReadState::Missing}; std::string bytes, reason; };
struct Journal {
    std::vector<Event> events;
    std::vector<StatusChange> status;
    std::uint64_t status_dropped{},status_coalesced{},events_dropped{},control_events_dropped{};
    std::uint64_t batches_offered{},identical_skipped{};
};
struct Io {
    virtual ~Io()=default;
    // Snapshot bytes, bounded on the open handle. Error includes oversize,
    // sharing/access failure and a non-regular file; Missing is only not-found.
    virtual Read read_session()=0;  // <=512 bytes
    virtual Read read_batch()=0;    // <=MaxBatchBytes
    virtual std::uint64_t unix_seconds()=0;
    // The concrete sink adds immutable process/nonce/build/profile identity.
    // An accepted write means these entire records were accepted, never partial.
    virtual bool append(const Journal&)=0;
    virtual bool finish()=0;        // flush/close on worker, completion requires acknowledgment
};
struct Options { bool latched{},fences_verified{},force_absent{}; std::string expected_nonce; };
enum class State { New, Polling, Failed, Closed };
class Pump {
    Core& core_; Io& io_; const Options options_;
    std::thread::id worker_{};
    State state_{State::New}; std::string reason_;
    bool have_bytes_{}; std::string last_bytes_;
    std::uint64_t batches_offered_{},identical_skipped_{},journal_writes_{};
    bool finished_{},complete_{};
    void fail(const std::string& why){
        if(state_==State::Failed||state_==State::Closed)return;
        state_=State::Failed;reason_=why.substr(0,512);core_.fail_control(reason_);
    }
    bool closed(){if(core_.control_state().closed){state_=State::Closed;return true;}return false;}
    Journal drain(){return {core_.drain_events(),core_.drain_status(),core_.status_dropped(),core_.status_coalesced(),core_.events_dropped(),core_.control_events_dropped(),batches_offered_,identical_skipped_};}
    void start(){
        state_=State::Polling;
        if(!options_.latched)return; // ordinary play never opens either injection file
        const auto read=io_.read_session();
        if(closed())return; // a slow read must not rearm after host shutdown
        if(read.state!=ReadState::Bytes){fail("inject.on unavailable: "+read.reason);return;}
        Session session;std::string why;
        if(!parse_session(read.bytes,io_.unix_seconds(),session,why)){fail(why);return;}
        if(session.nonce!=options_.expected_nonce){fail("inject.on nonce differs from the entry fence");return;}
        core_.request_admission(session,io_.unix_seconds(),options_.fences_verified,options_.force_absent);
    }
public:
    Pump(Core& core,Io& io,Options options):core_(core),io_(io),options_(std::move(options)){}
    Pump(const Pump&)=delete;Pump& operator=(const Pump&)=delete;
    // Exactly one worker calls pass/status. Host closes Core's gate before
    // stopping/waiting for that worker; it must retain Core/provider/Io until
    // the worker's OS thread has actually ended (including exit callbacks).
    void pass(){
        if(worker_==std::thread::id{})worker_=std::this_thread::get_id();
        if(worker_!=std::this_thread::get_id())throw std::logic_error("control pump has one worker");
        if(state_==State::Failed||state_==State::Closed||closed())return;
        try {
            if(state_==State::New)start();
            if(state_!=State::Polling||closed())return;
            const auto run=core_.control_state().run;
            if(options_.latched&&(run==Run::Waiting||run==Run::Running)){
                const auto read=io_.read_batch();
                if(closed())return;
                if(read.state==ReadState::Error){fail("inject.txt unreadable: "+read.reason);return;}
                if(read.state==ReadState::Bytes){
                    if(read.bytes.size()>MaxBatchBytes){fail("inject.txt exceeds its byte bound");return;}
                    if(have_bytes_&&read.bytes==last_bytes_)++identical_skipped_;
                    else {
                        auto offered=core_.offer_batch(read.bytes);++batches_offered_;
                        if(offered.verdict==BatchVerdict::Refused){fail("inject.txt refused: "+offered.why);return;}
                        last_bytes_=read.bytes;have_bytes_=true;
                    }
                }
            }
            if(closed())return;
            Journal journal=drain();
            // Counts remain observable in every journal, including an otherwise
            // empty pass. A failed append is terminal, never silently retried as
            // if the drained evidence had been persisted.
            if(!io_.append(journal)){fail("runtime journal append failed");return;}
            ++journal_writes_;
        }catch(const std::exception& e){fail(std::string("control operation threw: ")+e.what());}
         catch(...){fail("control operation threw");}
    }
    // Host has stopped input ticks and closed the gate. Unlike pass(), this
    // drains the final input-thread records even after closure, then finishes
    // the sink. Always attempt sink cleanup, including after an earlier fault.
    bool finish(){
        if(finished_)return complete_;
        if(worker_!=std::thread::id{}&&worker_!=std::this_thread::get_id())throw std::logic_error("control finish on wrong worker");
        if(!core_.control_state().closed)throw std::logic_error("close control admission before final drain");
        finished_=true;bool ok=state_!=State::Failed;
        try{if(ok){ok=io_.append(drain());if(ok)++journal_writes_;}}catch(...){ok=false;}
        try{if(!io_.finish())ok=false;}catch(...){ok=false;}
        complete_=ok;state_=ok?State::Closed:State::Failed;
        if(!ok&&reason_.empty())reason_="control final drain or sink close failed";
        return complete_;
    }
    State state()const{return state_;}
    const std::string& reason()const{return reason_;}
    std::uint64_t batches_offered()const{return batches_offered_;}
    std::uint64_t identical_skipped()const{return identical_skipped_;}
    std::uint64_t journal_writes()const{return journal_writes_;}
};
}
