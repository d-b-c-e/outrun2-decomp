#pragma once
// One control-worker pass. All file operations stay behind Io, off the input
// thread. Core owns admission/order/TTL and terminal neutrality. No game APIs.
#include "shim.hpp"
#include <chrono>
#include <stdexcept>
#include <thread>

namespace dbce::shim::control {
// Busy: the open met a sharing or lock conflict (another handle holds the file incompatibly, e.g. a publisher in the
// middle of replacing it). The pump retries it on later passes while it has been Busy for at most BusyBoundMs in a
// row (exactly BusyBoundMs is still within the bound); more than that fails (Pump::pass).
enum class ReadState { Missing, Bytes, Busy, Error };
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
    // A monotonic millisecond clock for the Busy bound (never the wall clock). Fakes override it.
    virtual std::uint64_t monotonic_ms(){return std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());}
    // The concrete sink adds immutable process/nonce/build/profile identity.
    // An accepted write means these entire records were accepted, never partial.
    virtual bool append(const Journal&)=0;
    virtual bool finish()=0;        // flush/close on worker, completion requires acknowledgment
};
struct Options { bool latched{},fences_verified{},force_absent{}; std::string expected_nonce; };
enum class State { New, Polling, Failed, Closed };
class Pump {
public:
    // A Busy inject.txt is retried while it has been Busy for at most this long in a row (exactly this long is still
    // within); after more than this the session fails, as an I/O failure would.
    static constexpr std::uint64_t BusyBoundMs=2000;
private:
    Core& core_; Io& io_; const Options options_;
    std::thread::id worker_{};
    State state_{State::New}; std::string reason_;
    bool have_bytes_{}; std::string last_bytes_;
    std::uint64_t batches_offered_{},identical_skipped_{},journal_writes_{};
    bool busy_{}; std::uint64_t busy_since_{},busy_passes_{};
    bool finished_{},complete_{},journal_failed_{};
    void fail(const std::string& why){
        if(state_==State::Failed||state_==State::Closed)return;
        state_=State::Failed;reason_=bounded_diagnostic(why);core_.fail_control(reason_);
    }
    bool closed(){if(core_.control_state().closed){state_=State::Closed;return true;}return false;}
    // One Core cut (one lock): separate drains could carry an epoch's status ahead of its EpochBegin.
    Journal drain(){auto c=core_.drain_journal();return {std::move(c.events),std::move(c.status),c.status_dropped,c.status_coalesced,c.events_dropped,c.control_events_dropped,batches_offered_,identical_skipped_};}
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
    // The owning thread entry point must catch exceptions, including misuse of
    // this single-worker API (ProviderHost does so); never let them escape it.
    void pass(){
        if(worker_==std::thread::id{})worker_=std::this_thread::get_id();
        if(worker_!=std::this_thread::get_id())throw std::logic_error("control pump has one worker");
        if(state_==State::Failed||state_==State::Closed||closed())return;
        bool writing=false;
        try {
            if(state_==State::New)start();
            if(state_!=State::Polling||closed())return;
            const auto run=core_.control_state().run;
            if(options_.latched&&(run==Run::Waiting||run==Run::Running)){
                const auto read=io_.read_batch();
                if(closed())return;
                // Busy: nothing is offered, so Core's own deadlines keep running. A held batch expires neutral on its
                // original deadline; a Busy pass never renews a batch, a heartbeat or output authority. The bound
                // restarts only after a successful snapshot or a missing file (a Bytes or Missing read).
                if(read.state==ReadState::Busy){
                    const auto now=io_.monotonic_ms();
                    if(!busy_){busy_=true;busy_since_=now;}
                    ++busy_passes_;
                    if(now>=busy_since_&&now-busy_since_>BusyBoundMs){
                        fail("inject.txt busy for more than "+std::to_string(BusyBoundMs)+" ms: "+read.reason);return;}
                }
                else busy_=false;
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
            writing=true;
            if(!io_.append(journal)){journal_failed_=true;fail("runtime journal append failed");return;}
            writing=false;
            ++journal_writes_;
        }catch(const std::exception& e){if(writing)journal_failed_=true;fail(std::string("control operation threw: ")+e.what());}
         catch(...){if(writing)journal_failed_=true;fail("control operation threw");}
    }
    // Host has stopped input ticks and closed the gate. Unlike pass(), this
    // drains the final input-thread records even after closure, then finishes
    // the sink. Always attempt sink cleanup, including after an earlier fault.
    bool finish(){
        if(finished_)return complete_;
        if(worker_!=std::thread::id{}&&worker_!=std::this_thread::get_id())throw std::logic_error("control finish on wrong worker");
        if(!core_.control_state().closed)throw std::logic_error("close control admission before final drain");
        finished_=true;bool ok=state_!=State::Failed;
        // A read/parse fault still needs its next-tick terminal event journaled.
        // A broken journal is never retried or represented as gap-free.
        try{if(!journal_failed_){const bool written=io_.append(drain());if(written)++journal_writes_;else ok=false;}
            else ok=false;}catch(...){ok=false;}
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
    std::uint64_t busy_passes()const{return busy_passes_;}
};
}
