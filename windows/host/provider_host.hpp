#pragma once
// Bounded host ownership of the actual Core and DirectInput provider. Extracted
// without lifecycle changes from the P21k proof; all fixtures use this same code.
// This host does not apply a game adapter or issue force commands. The caller owns
// neutralization, capture policy and the final unresolved-process decision.
#include "dinput_provider.hpp"
#include "shim.hpp"
#include "host_lifecycle.hpp"
#include "control_pump.hpp"
#include "candidate.hpp"
#include <condition_variable>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace dbce::shim::hosting {

// Everything the provider worker touches is in this shared state (D1): a worker that outlives the host keeps the core
// and the provider alive, so nothing it can still call is ever destroyed under it. The core holds a reference to the
// provider; members are destroyed in reverse order, the core first.
struct WorkerState {
    std::shared_ptr<dbce::shim::ProviderOwner> provider;   // the DirectInput provider, or an owned copied-input provider
    std::shared_ptr<dbce::shim::Provider> front;   // optional, over `provider` (the WheelFfb bridge); the core reads it
    std::shared_ptr<dbce::shim::Core> core;
    // Extension 4: inert until the owner thread requests a listing or selects a device. It uses `provider` itself,
    // never `front`, so a force front never sees a candidate.
    std::shared_ptr<dbce::shim::Candidate> candidate;
    std::atomic<bool> stop{false};
    std::mutex m; std::condition_variable cv; bool done = false;
    std::atomic<long long> passes{0};
};

// The spike's budgets, plus release_ms (Astra 13277): the provider's shutdown releases idle devices (Unacquire, Release)
// and DirectInput itself synchronously, driver calls with no bound of their own, so that phase gets its own budget.
struct Budget { int recorder_ms = 1000, leases_ms = 200, release_ms = 200, workers_ms = 200, control_ms = 200; };

struct ControlWorkerState {
    std::shared_ptr<WorkerState> owner;           // Core AND its referenced provider outlive late I/O
    std::shared_ptr<control::Io> io;
    control::Options options;
    std::atomic<bool> stop{false};
    std::mutex m; std::condition_variable cv; bool done=false,complete=false;
};

// The release phase's own state: a release still inside a driver call keeps the provider alive (D1).
struct ReleaseState {
    std::shared_ptr<dbce::shim::ProviderOwner> provider;
    std::mutex m; std::condition_variable cv; bool done = false;
    dbce::shim::ProviderShutdownReport rep;
};

struct Report : LifetimeReport {
    bool control_present=false,control_joined=true,control_complete=true,control_final_tick=true;
    int released = 0, held = 0;     // DInputProvider::ShutdownReport
    bool input_released = false;
    std::string provider_detail;
};

template<class RecorderType> class ProviderHost {
    std::wstring folder_; std::string nonce_;
    std::thread::id input_;
    HANDLE report_ = INVALID_HANDLE_VALUE;   // pre-opened: the report never waits behind a stdio lock (D2)
    std::unique_ptr<RecorderType> recorder_;
    std::shared_ptr<WorkerState> w_;
    std::thread worker_, release_, control_thread_;
    std::shared_ptr<ControlWorkerState> control_;
    bool latched_{};
    bool control_close_requested_{},control_final_tick_{};
    bool stopped_ = false; Report cache_;
public:
    // `input` is one reference to the DirectInput object (real or fake); the provider takes it over.
    // `front` (optional) wraps the DirectInput provider in another Provider the core reads through (shim/wheelffb_bridge);
    // it lives in the worker's shared state, so a worker that outlives the host keeps it alive too (D1).
    // RecorderType must be lazy: no worker or write begins before the first push. Construction may still throw before
    // this host can stop it. The fake recorder, the real P21 recorder and NoCapture all satisfy this contract.
    // `controls_reads`: the highest controls schema this host's Core applies (STD-037 item 4; read_profile). 1, the
    // default, is the DLL ABI's and every existing caller's. A consumer whose readers all opt in passes 2; without it the
    // in-process Core refuses a Schema 2 section although the consumer's own readers apply it (calibration 9a run1).
    ProviderHost(std::wstring folder, std::string nonce, IDirectInput8W* input, const std::string& profile, bool latched,
        std::unique_ptr<RecorderType> recorder, dbce::shim::Capability capability = {}, HWND window = nullptr,
        std::int64_t freshness_us = 100'000,
        std::function<std::shared_ptr<dbce::shim::Provider>(const std::shared_ptr<dbce::shim::DInputProvider>&)> front = {},
        int controls_reads = 1, const dbce::shim::Declaration* extras = nullptr)
        : folder_(std::move(folder)), nonce_(std::move(nonce)), input_(std::this_thread::get_id()),
          recorder_(std::move(recorder)),
          w_(std::make_shared<WorkerState>()),latched_(latched) {
        if (!recorder_) throw std::invalid_argument("recorder required");
        auto direct = std::make_shared<dbce::shim::DInputProvider>(input, window);   // takes `input` over
        w_->provider = direct;
        if (front) { w_->front = front(direct); if (!w_->front) throw std::invalid_argument("front provider refused"); }
        start(profile, latched, capability, freshness_us, controls_reads, extras);
    }
    // A provider the host owns but did not create: shim/bridge_provider's copied reads, with no DirectInput object. The
    // core reads it directly (no front), and its shutdown runs in the same bounded release phase as DirectInput's.
    ProviderHost(std::wstring folder, std::string nonce, std::shared_ptr<dbce::shim::ProviderOwner> provider, const std::string& profile,
        bool latched, std::unique_ptr<RecorderType> recorder, dbce::shim::Capability capability = {},
        std::int64_t freshness_us = 100'000, int controls_reads = 1, const dbce::shim::Declaration* extras = nullptr)
        : folder_(std::move(folder)), nonce_(std::move(nonce)), input_(std::this_thread::get_id()),
          recorder_(std::move(recorder)),
          w_(std::make_shared<WorkerState>()),latched_(latched) {
        if (!recorder_ || !provider) throw std::invalid_argument("recorder and provider required");
        w_->provider = std::move(provider);
        start(profile, latched, capability, freshness_us, controls_reads, extras);
    }
private:
    void start(const std::string& profile, bool latched, dbce::shim::Capability capability, std::int64_t freshness_us, int controls_reads,
               const dbce::shim::Declaration* extras = nullptr) {
        dbce::shim::CoreConfig cfg; cfg.latched = latched; cfg.capability = capability;
        cfg.fresh_us = freshness_us;   // ordinary 100 ms bound; fixture wrapper explicitly supplies its own value
        w_->core = std::make_shared<dbce::shim::Core>(w_->front ? *w_->front : static_cast<dbce::shim::Provider&>(*w_->provider), cfg);
        // A game's declared extras are planned from the same bytes, with their devices after the common table.
        if (extras) w_->core->set_profile(dbce::shim::read_profile(profile, controls_reads), *extras, profile, controls_reads);
        else w_->core->set_profile(dbce::shim::read_profile(profile, controls_reads));
        {
            std::vector<std::string> bound;   // Core's table is immutable from here; extension 3 offers these devices
            for (const auto& d : w_->core->device_reports()) bound.push_back(d.identity.dev);
            w_->candidate = std::make_shared<dbce::shim::Candidate>(std::move(bound));
        }
        report_ = CreateFileW((folder_ + L"\\host-shutdown.json").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        auto s = w_;
        worker_ = std::thread([s] {
            while (!s->stop.load()) {
                s->core->service_devices();   // may sit inside a driver call (Acquire, EnumDevices, an open) for as long as the driver keeps it
                s->candidate->service(*s->provider);   // no provider call unless the owner thread asked for one
                ++s->passes;
                std::unique_lock<std::mutex> l(s->m);
                s->cv.wait_for(l, std::chrono::milliseconds(5), [&] { return s->stop.load(); });
            }
            { std::lock_guard<std::mutex> l(s->m); s->done = true; }
            s->cv.notify_all();
        });
    }
public:
    ProviderHost(const ProviderHost&) = delete; ProviderHost& operator=(const ProviderHost&) = delete;

    const dbce::shim::Frame& tick() { return tick_frame(w_->core->tick()); }
    // An extras host's one tick (Core::tick(contexts)): the common frame and the extras from one device read.
    const dbce::shim::Frame& tick(std::uint32_t active_contexts) { return tick_frame(w_->core->tick(active_contexts)); }
    bool has_extras() const { return w_->core->has_extras(); }
    const dbce::shim::ExtrasFrame& extras_frame() const { return w_->core->extras_frame(); }   // input thread
    const dbce::shim::Declaration& extras_declaration() const { return w_->core->extras_declaration(); }
    std::string extras_report() const { return w_->core->extras_report(); }                    // input thread
private:
    const dbce::shim::Frame& tick_frame(const dbce::shim::Frame& frame) { // input thread; a close must reach Core before the final drain
        if(control_close_requested_){
            bool neutral=!latched_||(frame.suppress_native&&run_terminal(frame.run)&&frame.down==0);
            if(latched_)for(const auto& action:frame.action)neutral=neutral&&action.value==0;
            control_final_tick_=neutral;
        }
        return frame;
    }
public:
    std::vector<dbce::shim::DeviceReport> device_reports()const { return w_->core->device_reports(); } // input thread
    dbce::shim::StartReport start_report()const { return w_->core->start_report(); }           // input thread
    long long worker_passes() const { return w_->passes.load(); }
    // Extension 4, input thread: requests go to the worker; candidate_tick reads an open candidate after Core's tick.
    dbce::shim::Candidate& candidate() { return *w_->candidate; }
    void candidate_tick(std::uint64_t frame_seq, std::int64_t tick_us) { w_->candidate->tick(*w_->provider, frame_seq, tick_us); }
    RecorderType& recorder() { return *recorder_; } // input thread; the host owns its full lifetime
    void close_control_admission(){
        if(std::this_thread::get_id()!=input_)throw std::logic_error("close control on input thread");
        w_->core->close_control();
        control_close_requested_=true;
    }

    // Input thread, once. I/O is performed only on the owned control worker.
    // The desktop entry point supplies the already verified immutable fences.
    void start_control(std::shared_ptr<control::Io> io,control::Options options){
        if(std::this_thread::get_id()!=input_||stopped_||control_||!io)throw std::logic_error("invalid control worker start");
        if(options.latched!=latched_||(latched_&&options.expected_nonce!=nonce_))throw std::logic_error("control worker fence identity differs from host");
        auto s=std::make_shared<ControlWorkerState>();s->owner=w_;s->io=std::move(io);s->options=std::move(options);
        control_=s;
        control_thread_=std::thread([s]{
            bool complete=false;
            try{
                control::Pump pump(*s->owner->core,*s->io,s->options);
                while(!s->stop.load()){
                    pump.pass();
                    std::unique_lock<std::mutex> l(s->m);
                    s->cv.wait_for(l,std::chrono::milliseconds(25),[&]{return s->stop.load();});
                }
                complete=pump.finish();
            }catch(...){
                s->owner->core->fail_control("control worker threw");
                try{(void)s->io->finish();}catch(...){}
            }
            {std::lock_guard<std::mutex> l(s->m);s->complete=complete;s->done=true;}
            s->cv.notify_all();
        });
    }

    void write_report(const char* state, const Report* r) {
        if (report_ == INVALID_HANDLE_VALUE) return;
        FILETIME created{}, exited{}, kernel{}, user{};
        unsigned long long identity = 0;
        if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
            identity = (static_cast<unsigned long long>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        std::string t = std::string("{\"state\":\"") + state + "\",\"nonce\":\"" + nonce_ + "\",\"pid\":" +
            std::to_string(GetCurrentProcessId()) + ",\"creationTime\":" + std::to_string(identity);
        if (r) {
            t += ",\"recorder\":\"" + std::string(name(r->recorder)) + "\",\"recorderOutcome\":" + std::to_string(int(r->recorder)) + ",\"recorderJoined\":" + (r->recorder_joined ? "true" : "false") +
                 ",\"released\":" + std::to_string(r->released) + ",\"held\":" + std::to_string(r->held) +
                 ",\"inputReleased\":" + (r->input_released ? "true" : "false") + ",\"elapsedUs\":" + std::to_string(r->elapsed_us) + ",\"unresolved\":[";
            for (size_t i = 0; i < r->unresolved.size(); ++i) t += (i ? ",\"" : "\"") + r->unresolved[i] + "\"";
            t += "],\"controlPresent\":"+std::string(r->control_present?"true":"false")+
                 ",\"controlJoined\":"+(r->control_joined?"true":"false")+",\"controlComplete\":"+(r->control_complete?"true":"false")+
                 ",\"controlFinalTick\":"+(r->control_final_tick?"true":"false");
        }
        t += "}";
        SetFilePointer(report_, 0, nullptr, FILE_BEGIN); DWORD n = 0;
        WriteFile(report_, t.data(), DWORD(t.size()), &n, nullptr); SetEndOfFile(report_);
    }

    // D3: the input thread only, once; every phase bounded and every phase attempted.
    Report stop(Budget b = {}) {
        if (stopped_) return cache_;
        stopped_ = true;
        const auto t0 = Clock::now();
        Report r;
        write_report("stopping", nullptr);                                            // phase 0
        if (std::this_thread::get_id() != input_) r.unresolved.push_back("stop off the input thread");
        // Caller closes admission, then ticks/applies its final neutral adapter
        // frame, then stops input ticks. Closing here alone cannot publish the
        // Core's terminal event, so require that order whenever control exists.
        w_->core->close_control();
        if(control_){
            r.control_present=true;control_->stop=true;control_->cv.notify_all();
            // Do not read input-thread state on the already-refused misuse path.
            r.control_final_tick=std::this_thread::get_id()==input_&&control_final_tick_;
            if(!r.control_final_tick)r.unresolved.push_back("control closure has no final input tick");
            const auto deadline=Clock::now()+std::chrono::milliseconds(b.control_ms);
            std::unique_lock<std::mutex> l(control_->m);
            const bool returned=control_->cv.wait_until(l,deadline,[&]{return control_->done;});
            r.control_complete=returned&&control_->complete;l.unlock();
            r.control_joined=returned&&join_by(control_thread_,deadline);
            if(!r.control_complete)r.unresolved.push_back("control journal incomplete");
            if(!r.control_joined)r.unresolved.push_back("worker control");
        }
        recorder_->finish();                                                           // phase 4: off the producer thread a cancellation
        const auto drained = recorder_->drain(Clock::now() + std::chrono::milliseconds(b.recorder_ms));
        r.recorder = drained.outcome; r.recorder_joined = drained.joined;
        if (r.recorder != RecOutcome::Complete) r.unresolved.push_back(std::string("recorder: ") + name(r.recorder));
        if (!r.recorder_joined) r.unresolved.push_back("recorder: worker not joined");
        // Phase 5, leases and release. This thread is out of tick() (it is running stop). The real provider stops admitting,
        // releases what is idle and waits for calls in progress at most leases_ms. A device still inside a call is leaked
        // to that call, never released under it; DirectInput is released only when no call can still use it. Those
        // releases are driver calls with no bound of their own (Astra 13277), so the provider's shutdown runs on a release
        // thread of the host's, waited for at most leases_ms + release_ms. A release still inside the driver is left
        // running with the provider it holds (D1), its report unknown, and stop goes on towards D4.
        auto rs = std::make_shared<ReleaseState>(); rs->provider = w_->provider;
        const int leases = b.leases_ms;
        release_ = std::thread([rs, leases] {
            dbce::shim::ProviderShutdownReport rep;
            try { rep = rs->provider->shutdown(leases); } catch (...) { rep.detail = "the provider's shutdown threw"; }
            { std::lock_guard<std::mutex> l(rs->m); rs->rep = std::move(rep); rs->done = true; }
            rs->cv.notify_all();
        });
        dbce::shim::ProviderShutdownReport rep; bool returned = false;
        const auto release_deadline = Clock::now() + std::chrono::milliseconds(b.leases_ms + b.release_ms);
        {
            std::unique_lock<std::mutex> l(rs->m);
            returned = rs->cv.wait_until(l, release_deadline, [&] { return rs->done; });
            if (returned) rep = rs->rep;
        }
        if (!returned) { r.unresolved.push_back("DirectInput release did not return: a driver call is still inside the provider's shutdown"); }
        else if (!join_by(release_, release_deadline)) r.unresolved.push_back("release thread did not end: its exit work (a driver's thread-exit callback) is still running");
        r.released = rep.released; r.held = rep.held; r.input_released = rep.input_released; r.provider_detail = rep.detail;
        if (rep.held) r.unresolved.push_back("lease leaked: " + std::to_string(rep.held) + " device(s) still inside a call");
        // Phase 6, workers: one deadline. A worker still inside a driver call is left running; it owns its state.
        w_->stop = true; w_->cv.notify_all();
        if (!join_worker_until(Clock::now() + std::chrono::milliseconds(b.workers_ms))) r.unresolved.push_back("worker provider");
        // Phase 7, DirectInput.
        if (!rep.input_released) r.unresolved.push_back("DirectInput not released: a call may still use it");
        r.resolved = r.unresolved.empty();
        r.elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
        write_report(r.resolved ? "resolved" : "unresolved", &r);                       // phase 8
        cache_ = r;
        return r;
    }

    // D4, as reviewed: a latched process with an unresolved stop parks for the identity-bound supervisor; an ordinary
    // one ends itself by TerminateProcess, labelled forced in the report already written.
    [[noreturn]] void park_for_supervisor() { HANDLE never = CreateEventW(nullptr, TRUE, FALSE, nullptr); for (;;) WaitForSingleObject(never, INFINITE); }
    [[noreturn]] void forced_exit() { TerminateProcess(GetCurrentProcess(), ForcedExitCode); for (;;) Sleep(INFINITE); }

    // D9: never stops, never waits. A worker that was never joined is detached; it keeps its own state alive.
    ~ProviderHost() {
        if (worker_.joinable()) worker_.detach();
        if (release_.joinable()) release_.detach();
        if (control_thread_.joinable()) control_thread_.detach();
        if (report_ != INVALID_HANDLE_VALUE) CloseHandle(report_);
    }

private:
    bool join_worker_until(Clock::time_point deadline) {
        std::unique_lock<std::mutex> l(w_->m);
        if (!w_->cv.wait_until(l, deadline, [&] { return w_->done; })) return false;   // still owned; the destructor detaches (D9)
        l.unlock(); return join_by(worker_, deadline);   // done, but the thread may still be in its exit work
    }
};
}
