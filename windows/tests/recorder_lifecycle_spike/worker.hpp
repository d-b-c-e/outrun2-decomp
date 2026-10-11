#pragma once
// Executable design proof only; not a production recorder or unload-safe DLL worker.
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace recorder_spike {
constexpr std::size_t MaxValues=1024;
struct Packet {
    std::uint64_t sequence{};
    std::int64_t source_us{};
    std::uint32_t width{};
    std::array<double,MaxValues> values{};
};
static_assert(std::is_trivially_copyable<Packet>::value,"owned fixed snapshot");
enum class Outcome { Open,Draining,Complete,QueueFull,Invalid,Empty,DeclaredTailMismatch,WrongProducer,OpenFailure,WriteFailure,StopFailure,SinkException,Cancelled,ProducerAbandoned,TimedOut,WriterLimit };
inline bool failure(Outcome s){return s!=Outcome::Open&&s!=Outcome::Draining&&s!=Outcome::Complete;}
struct Sink {
    // Cleanup must not throw: an exception from abandon/destruction terminates the host.
    virtual ~Sink()noexcept=default;
    virtual bool open()=0;
    virtual bool write(const Packet&)=0;
    // Only true when the failed write cleanly closed at an intentional bound, not an I/O/flush error.
    virtual bool stopped_at_limit()const{return false;}
    virtual bool stop()=0;
    virtual void abandon()noexcept=0;
    virtual std::string reason()const{return {};}
};
// Fixture scheduling only. Null for real writer use. Storage must outlive the worker.
struct Hooks {virtual ~Hooks()=default;virtual void after_empty_observation(){};};
struct Report {
    // Live reads are snapshots; reconcile only after capture_counts_final AND producer calls quiesce.
    // Post-end diagnostic calls can still advance refusal counters after capture itself has finished.
    Outcome outcome{};
    std::uint64_t attempted{},accepted{},written{},queue_full{},invalid{},after_failure{},after_end{},wrong_producer{},after_end_wrong_producer{},declared_last{},last_written{};
    std::size_t ring_bytes{};
    bool producer_done{},worker_done{},drain_timed_out{};
    bool first_rejected{},capture_counts_final{}; // first pre-end loss rejection, not a later refusal
    std::uint64_t first_rejected_sequence{},discarded{};
    std::int64_t first_rejected_us{};
    Outcome first_rejected_reason{};
    std::string sink_reason;
    bool may_seal()const{return outcome==Outcome::Complete&&producer_done&&worker_done&&!drain_timed_out;}
};
struct State {
    std::vector<Packet> ring;
    const std::uint32_t width;
    const std::uint64_t first_sequence;
    std::atomic<std::uint64_t> read{0},write{0};
    std::atomic<Outcome> outcome{Outcome::Open};
    std::atomic<bool> producer_done{false},worker_done{false},drain_timed_out{false};
    std::atomic<std::uint64_t> attempted{0},accepted{0},written{0},queue_full{0},invalid{0},after_failure{0},after_end{0},wrong_producer{0},after_end_wrong_producer{0},declared_last{0},last_written{0};
    std::atomic_flag producer_busy=ATOMIC_FLAG_INIT;
    std::thread::id producer_thread;
    bool producer_thread_set{};
    std::atomic<bool> rejection_claimed{false},rejection_ready{false};
    std::uint64_t first_rejected_sequence{};
    std::int64_t first_rejected_us{};
    Outcome first_rejected_reason{};
    mutable std::mutex reason_mutex;
    std::string sink_reason;
    std::mutex wait_mutex;
    std::condition_variable changed;
    std::shared_ptr<Hooks> hooks;
    State(std::size_t capacity,std::uint32_t count,std::uint64_t first,std::shared_ptr<Hooks> h)
        :ring(capacity),width(count),first_sequence(first),hooks(std::move(h)){}
    void fail(Outcome why){
        auto prior=outcome.load();
        while(prior==Outcome::Open||prior==Outcome::Draining){
            if(outcome.compare_exchange_weak(prior,why))break;
        }
        changed.notify_all();
    }
    void rejected(const Packet& p,Outcome why){
        bool unclaimed=false;
        if(rejection_claimed.compare_exchange_strong(unclaimed,true)){
            first_rejected_sequence=p.sequence;first_rejected_us=p.source_us;first_rejected_reason=why;
            rejection_ready.store(true,std::memory_order_release);
        }
    }
    Report report()const{
        Report r;r.producer_done=producer_done.load(std::memory_order_acquire);r.worker_done=worker_done.load(std::memory_order_acquire);
        r.outcome=outcome.load();r.attempted=attempted;r.accepted=accepted;r.written=written;
        r.queue_full=queue_full;r.invalid=invalid;r.after_failure=after_failure;r.after_end=after_end;r.wrong_producer=wrong_producer;r.declared_last=declared_last;r.last_written=last_written;
        r.after_end_wrong_producer=after_end_wrong_producer;
        r.ring_bytes=ring.capacity()*sizeof(Packet);r.drain_timed_out=drain_timed_out;
        r.first_rejected=rejection_ready.load(std::memory_order_acquire);
        if(r.first_rejected){r.first_rejected_sequence=first_rejected_sequence;r.first_rejected_us=first_rejected_us;r.first_rejected_reason=first_rejected_reason;}
        r.capture_counts_final=r.producer_done&&r.worker_done;
        if(r.capture_counts_final)r.discarded=r.accepted-r.written;
        {std::lock_guard<std::mutex> lock(reason_mutex);r.sink_reason=sink_reason;}
        return r;
    }
};

class Producer {
    std::shared_ptr<State> s_;
    bool ended_{};
    std::int64_t last_us_=-1;
    std::uint64_t next_{};
    struct Entry {
        State& s;bool acquired{},correct{},post_end{};
        explicit Entry(State& state):s(state){
            acquired=!s.producer_busy.test_and_set(std::memory_order_acquire);
            if(acquired){
                if(!s.producer_thread_set){s.producer_thread=std::this_thread::get_id();s.producer_thread_set=true;}
                correct=s.producer_thread==std::this_thread::get_id();
            }
            if(!correct){post_end=s.producer_done.load(std::memory_order_acquire);if(!post_end)s.fail(Outcome::WrongProducer);}
        }
        ~Entry(){if(acquired)s.producer_busy.clear(std::memory_order_release);}
    };
public:
    explicit Producer(std::shared_ptr<State> s):s_(std::move(s)),next_(s_->first_sequence){}
    Producer(const Producer&)=delete;
    Producer& operator=(const Producer&)=delete;
    Producer(Producer&&)=default;
    Producer& operator=(Producer&&)=delete;
    ~Producer(){if(s_&&!ended_){s_->fail(Outcome::ProducerAbandoned);s_->producer_done.store(true,std::memory_order_release);s_->changed.notify_all();}}
    bool push(const Packet& p){
        ++s_->attempted;
        Entry entry(*s_);
        if(!entry.correct){
            if(entry.post_end)++s_->after_end_wrong_producer;
            else{++s_->wrong_producer;s_->rejected(p,Outcome::WrongProducer);}
            return false;
        }
        if(ended_){++s_->after_end;return false;}
        if(s_->outcome.load()!=Outcome::Open){++s_->after_failure;return false;}
        bool valid=p.width==s_->width&&p.sequence==next_&&p.sequence!=UINT64_MAX&&p.source_us>=0&&p.source_us>last_us_;
        for(std::size_t i=0;valid&&i<p.width;++i)valid=std::isfinite(p.values[i]);
        if(!valid){++s_->invalid;s_->rejected(p,Outcome::Invalid);s_->fail(Outcome::Invalid);return false;}
        const auto write=s_->write.load(std::memory_order_relaxed),read=s_->read.load(std::memory_order_acquire);
        if(write-read>=s_->ring.size()){++s_->queue_full;s_->rejected(p,Outcome::QueueFull);s_->fail(Outcome::QueueFull);return false;}
        s_->ring[std::size_t(write%s_->ring.size())]=p;
        ++s_->accepted;last_us_=p.source_us;++next_;
        s_->write.store(write+1,std::memory_order_release);
        s_->changed.notify_one();
        return true;
    }
    void finish(std::uint64_t last_source_sequence){
        Entry entry(*s_);if(!entry.correct)return;
        if(ended_)return;
        ended_=true;
        s_->declared_last=last_source_sequence;
        if(last_us_<0)s_->fail(Outcome::Empty);
        else if(last_source_sequence!=next_-1)s_->fail(Outcome::DeclaredTailMismatch);
        auto open=Outcome::Open;s_->outcome.compare_exchange_strong(open,Outcome::Draining);
        s_->producer_done.store(true,std::memory_order_release);
        s_->changed.notify_all();
    }
    Report report()const{return s_->report();}
};

class Controller {
    std::shared_ptr<State> s_;
    bool producer_taken_{};
    static void run(std::shared_ptr<State> s,std::unique_ptr<Sink> sink){
        bool stopped=false;
        try{
            if(!failure(s->outcome.load())){
                if(!sink->open())s->fail(Outcome::OpenFailure);
                while(!failure(s->outcome.load())){
                    const auto r=s->read.load(std::memory_order_relaxed),w=s->write.load(std::memory_order_acquire);
                    if(r!=w){
                        if(!sink->write(s->ring[std::size_t(r%s->ring.size())])){s->fail(sink->stopped_at_limit()?Outcome::WriterLimit:Outcome::WriteFailure);break;}
                        s->last_written=s->ring[std::size_t(r%s->ring.size())].sequence;
                        ++s->written;s->read.store(r+1,std::memory_order_release);
                        continue;
                    }
                    if(s->hooks)s->hooks->after_empty_observation();
                    if(s->producer_done.load(std::memory_order_acquire)){
                        // End can arrive after the empty observation above. Re-read publication AFTER end.
                        if(r!=s->write.load(std::memory_order_acquire))continue;
                        if(failure(s->outcome.load()))break;
                        if(s->accepted.load()==0){s->fail(Outcome::Empty);break;}
                        if(!sink->stop()){s->fail(Outcome::StopFailure);break;}
                        stopped=true;break;
                    }
                    // Notify is a latency hint; periodic predicate re-read avoids lost-wake dependence.
                    std::unique_lock<std::mutex> lock(s->wait_mutex);
                    s->changed.wait_for(lock,std::chrono::milliseconds(2));
                }
            }
        }catch(...){s->fail(Outcome::SinkException);}
        if(!stopped)sink->abandon();
        try{auto reason=sink->reason();std::lock_guard<std::mutex> lock(s->reason_mutex);s->sink_reason=std::move(reason);}
        catch(...){s->fail(Outcome::SinkException);}
        sink.reset(); // all sink lifetime, including cleanup, belongs to the worker
        {
            // Pair the completion predicate with the wait mutex to avoid a missed final wake.
            std::lock_guard<std::mutex> lock(s->wait_mutex);
            if(stopped){
                if(s->accepted.load()!=s->written.load()||s->last_written.load()!=s->declared_last.load())s->fail(Outcome::WriteFailure);
                else {auto draining=Outcome::Draining;s->outcome.compare_exchange_strong(draining,Outcome::Complete);}
            }
            s->worker_done.store(true,std::memory_order_release);
        }
        s->changed.notify_all();
    }
public:
    Controller(std::size_t capacity,std::uint32_t width,std::size_t payload_budget,std::unique_ptr<Sink> sink,
               std::uint64_t first=1,std::shared_ptr<Hooks> hooks={}){
        if(!sink||!capacity||width<1||width>MaxValues||first==UINT64_MAX||capacity>payload_budget/sizeof(Packet))
            throw std::invalid_argument("recorder preflight");
        s_=std::make_shared<State>(capacity,width,first,std::move(hooks));
        if(s_->ring.capacity()>payload_budget/sizeof(Packet))throw std::invalid_argument("allocated payload budget");
        std::thread(run,s_,std::move(sink)).detach();
    }
    Controller(const Controller&)=delete;
    Controller& operator=(const Controller&)=delete;
    ~Controller(){s_->fail(Outcome::Cancelled);}
    Producer producer(){if(producer_taken_)throw std::logic_error("one producer");producer_taken_=true;return Producer(s_);}
    Report report()const{return s_->report();}
    void cancel(){s_->fail(Outcome::Cancelled);}
    Report drain(std::chrono::milliseconds duration){
        const auto deadline=std::chrono::steady_clock::now()+duration;
        std::unique_lock<std::mutex> lock(s_->wait_mutex);
        s_->changed.wait_until(lock,deadline,[&]{return s_->worker_done.load();});
        if(!s_->worker_done.load()&&s_->outcome.load()!=Outcome::Complete){
            auto prior=s_->outcome.load();
            while(prior==Outcome::Open||prior==Outcome::Draining){
                if(s_->outcome.compare_exchange_weak(prior,Outcome::TimedOut)){s_->drain_timed_out=true;break;}
            }
            if(failure(prior))s_->drain_timed_out=true;
        }
        return s_->report();
    }
};
}
