#pragma once
// P21k (windows/docs/P21-SHUTDOWN.md, proof row P21k; P28's P21 half in INPUT-SHIM.md section 17): the reviewed P21 host
// lifetime around the real slice-3 stack. zero's Core and DInputProvider run over fake COM objects (shim_fake_com.hpp),
// with the provider worker on its own thread as the core's threading model has it (shim.hpp, section 4): the worker calls
// Core::service_devices, and the input thread calls Core::tick and owns stop(). Test composition, not production code,
// and not host adoption: no device, no game, no launch.
#include "dinput_provider.hpp"
#include "shim.hpp"
#include "p21_host_spike/host.hpp"   // the reviewed spike's Clock, Recorder (fake writer), RecOutcome, ForcedExitCode
#include <condition_variable>
#include <memory>
#include <string>
#include <thread>

namespace p21k {
using p21_spike::Clock;
using p21_spike::Gate;
using p21_spike::RecOutcome;
using p21_spike::Recorder;

// Everything the provider worker touches is in this shared state (D1): a worker that outlives the host keeps the core
// and the provider alive, so nothing it can still call is ever destroyed under it. The core holds a reference to the
// provider; members are destroyed in reverse order, the core first.
struct WorkerState {
    std::shared_ptr<dbce::shim::DInputProvider> provider;
    std::shared_ptr<dbce::shim::Core> core;
    std::atomic<bool> stop{false};
    std::mutex m; std::condition_variable cv; bool done = false;
    std::atomic<long long> passes{0};
};

// The spike's budgets, plus release_ms (Astra 13277): the provider's shutdown releases idle devices (Unacquire, Release)
// and DirectInput itself synchronously, driver calls with no bound of their own, so that phase gets its own budget.
struct Budget { int recorder_ms = 1000, leases_ms = 200, release_ms = 200, workers_ms = 200; };

// The release phase's own state: a release still inside a driver call keeps the provider alive (D1).
struct ReleaseState {
    std::shared_ptr<dbce::shim::DInputProvider> provider;
    std::mutex m; std::condition_variable cv; bool done = false;
    dbce::shim::DInputProvider::ShutdownReport rep;
};

struct Report : p21_spike::Report {
    int released = 0, held = 0;     // DInputProvider::ShutdownReport
    bool input_released = false;
    std::string provider_detail;
};

class ProviderHost {
    std::wstring folder_; std::string nonce_;
    std::thread::id input_;
    HANDLE report_ = INVALID_HANDLE_VALUE;   // pre-opened: the report never waits behind a stdio lock (D2)
    Recorder recorder_;
    std::shared_ptr<WorkerState> w_;
    std::thread worker_, release_;
    bool stopped_ = false; Report cache_;
public:
    // `input` is one reference to the DirectInput object (real or fake); the provider takes it over.
    ProviderHost(std::wstring folder, std::string nonce, IDirectInput8W* input, const std::string& profile, bool latched)
        : folder_(std::move(folder)), nonce_(std::move(nonce)), input_(std::this_thread::get_id()),
          recorder_(folder_ + L"\\worker.tsv", nonce_, std::make_shared<Gate>(), std::make_shared<Gate>()),
          w_(std::make_shared<WorkerState>()) {
        w_->provider = std::make_shared<dbce::shim::DInputProvider>(input, nullptr);
        dbce::shim::CoreConfig cfg; cfg.latched = latched;
        cfg.fresh_us = 10'000'000;   // real QPC time: a slow machine must not make samples stale (as test_dinput_provider)
        w_->core = std::make_shared<dbce::shim::Core>(*w_->provider, cfg);
        w_->core->set_profile(dbce::shim::read_profile(profile));
        report_ = CreateFileW((folder_ + L"\\host-shutdown.json").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        auto s = w_;
        worker_ = std::thread([s] {
            while (!s->stop.load()) {
                s->core->service_devices();   // may sit inside a driver call (Acquire, EnumDevices, an open) for as long as the driver keeps it
                ++s->passes;
                std::unique_lock<std::mutex> l(s->m);
                s->cv.wait_for(l, std::chrono::milliseconds(5), [&] { return s->stop.load(); });
            }
            { std::lock_guard<std::mutex> l(s->m); s->done = true; }
            s->cv.notify_all();
        });
    }
    ProviderHost(const ProviderHost&) = delete; ProviderHost& operator=(const ProviderHost&) = delete;

    const dbce::shim::Frame& tick() { return w_->core->tick(); }   // input thread
    long long worker_passes() const { return w_->passes.load(); }

    void write_report(const char* state, const Report* r) {
        if (report_ == INVALID_HANDLE_VALUE) return;
        std::string t = std::string("{\"state\":\"") + state + "\",\"nonce\":\"" + nonce_ + "\"";
        if (r) {
            t += ",\"recorder\":\"" + std::string(p21_spike::name(r->recorder)) + "\",\"recorderJoined\":" + (r->recorder_joined ? "true" : "false") +
                 ",\"released\":" + std::to_string(r->released) + ",\"held\":" + std::to_string(r->held) +
                 ",\"inputReleased\":" + (r->input_released ? "true" : "false") + ",\"elapsedUs\":" + std::to_string(r->elapsed_us) + ",\"unresolved\":[";
            for (size_t i = 0; i < r->unresolved.size(); ++i) t += (i ? ",\"" : "\"") + r->unresolved[i] + "\"";
            t += "]";
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
        recorder_.finish();                                                           // phase 4: off the producer thread a cancellation
        const auto drained = recorder_.drain(Clock::now() + std::chrono::milliseconds(b.recorder_ms));
        r.recorder = drained.outcome; r.recorder_joined = drained.joined;
        if (r.recorder != RecOutcome::Complete) r.unresolved.push_back(std::string("recorder: ") + p21_spike::name(r.recorder));
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
            dbce::shim::DInputProvider::ShutdownReport rep;
            try { rep = rs->provider->shutdown(leases); } catch (...) { rep.detail = "the provider's shutdown threw"; }
            { std::lock_guard<std::mutex> l(rs->m); rs->rep = std::move(rep); rs->done = true; }
            rs->cv.notify_all();
        });
        dbce::shim::DInputProvider::ShutdownReport rep; bool returned = false;
        const auto release_deadline = Clock::now() + std::chrono::milliseconds(b.leases_ms + b.release_ms);
        {
            std::unique_lock<std::mutex> l(rs->m);
            returned = rs->cv.wait_until(l, release_deadline, [&] { return rs->done; });
            if (returned) rep = rs->rep;
        }
        if (!returned) { release_.detach(); r.unresolved.push_back("DirectInput release did not return: a driver call is still inside the provider's shutdown"); }
        else if (!p21_spike::join_by(release_, release_deadline)) r.unresolved.push_back("release thread did not end: its exit work (a driver's thread-exit callback) is still running");
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
    [[noreturn]] void forced_exit() { TerminateProcess(GetCurrentProcess(), p21_spike::ForcedExitCode); for (;;) Sleep(INFINITE); }

    // D9: never stops, never waits. A worker that was never joined is detached; it keeps its own state alive.
    ~ProviderHost() {
        if (worker_.joinable()) worker_.detach();
        if (release_.joinable()) release_.detach();
        if (report_ != INVALID_HANDLE_VALUE) CloseHandle(report_);
    }

private:
    bool join_worker_until(Clock::time_point deadline) {
        std::unique_lock<std::mutex> l(w_->m);
        if (!w_->cv.wait_until(l, deadline, [&] { return w_->done; })) { l.unlock(); if (worker_.joinable()) worker_.detach(); return false; }
        l.unlock(); return p21_spike::join_by(worker_, deadline);   // done, but the thread may still be in its exit work
    }
};
}
