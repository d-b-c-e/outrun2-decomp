#pragma once
// Executable design proof for P21 (windows/docs/P21-SHUTDOWN.md r2). Not production shim code: the workers, the
// recorder and the provider are fakes whose only job is to block where the real ones can.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace p21_spike {
using Clock = std::chrono::steady_clock;
constexpr DWORD ForcedExitCode = 0xDBCE0021;   // ordinary process, unresolved stop (D4)

// A gate a fake call blocks on until the test opens it (a driver call or a write that does not return).
struct Gate {
    std::mutex m; std::condition_variable cv; bool open = true; bool entered = false;
    void close() { std::lock_guard<std::mutex> l(m); open = false; entered = false; }
    void release() { { std::lock_guard<std::mutex> l(m); open = true; } cv.notify_all(); }
    void pass() { std::unique_lock<std::mutex> l(m); entered = true; cv.notify_all(); cv.wait(l, [&] { return open; }); }
    bool wait_entered(int ms) { std::unique_lock<std::mutex> l(m); return cv.wait_for(l, std::chrono::milliseconds(ms), [&] { return entered; }); }
};

// One worker thread. Everything it touches is in its own shared state (D1), so it can outlive the host.
struct WorkerState {
    std::string name;
    std::atomic<bool> stop{false};
    std::mutex m; std::condition_variable cv; bool done = false;
    std::shared_ptr<Gate> gate;                 // where its fake blocking call waits
    std::atomic<int> calls{0};
};
class Worker {
    std::shared_ptr<WorkerState> s_;
    std::thread t_;
public:
    Worker(std::string name, std::shared_ptr<Gate> gate) : s_(std::make_shared<WorkerState>()) {
        s_->name = std::move(name); s_->gate = std::move(gate);
        auto s = s_;
        t_ = std::thread([s] {
            while (!s->stop.load()) {
                s->gate->pass();               // a paced call that may block (EnumDevices, a file read)
                ++s->calls;
                std::unique_lock<std::mutex> l(s->m);
                s->cv.wait_for(l, std::chrono::milliseconds(5), [&] { return s->stop.load(); });
            }
            { std::lock_guard<std::mutex> l(s->m); s->done = true; }
            s->cv.notify_all();
        });
    }
    Worker(const Worker&) = delete; Worker& operator=(const Worker&) = delete;
    void request_stop() { s_->stop = true; s_->cv.notify_all(); }
    // True: joined before the deadline. False: still running, detached; it owns its state and ends with the process.
    bool join_until(Clock::time_point deadline) {
        std::unique_lock<std::mutex> l(s_->m);
        if (!s_->cv.wait_until(l, deadline, [&] { return s_->done; })) { l.unlock(); if (t_.joinable()) t_.detach(); return false; }
        l.unlock(); if (t_.joinable()) t_.join(); return true;
    }
    const std::string& name() const { return s_->name; }
    std::shared_ptr<WorkerState> state() const { return s_; }
    ~Worker() { if (t_.joinable()) t_.detach(); }   // never waits (D9)
};

// The P20 outcome subset P21 needs: first terminal wins; a timeout or cancellation is never overwritten.
enum class RecOutcome : int { Open, Draining, Complete, TimedOut, Cancelled };
inline const char* name(RecOutcome o) {
    switch (o) { case RecOutcome::Open: return "Open"; case RecOutcome::Draining: return "Draining"; case RecOutcome::Complete: return "Complete";
                 case RecOutcome::TimedOut: return "TimedOut"; default: return "Cancelled"; }
}
struct RecorderState {
    std::atomic<RecOutcome> outcome{RecOutcome::Open};
    std::atomic<bool> producer_done{false};
    std::thread::id producer;
    std::mutex m; std::condition_variable cv; bool worker_done = false;
    std::shared_ptr<Gate> stop_gate;            // the sink's stop/close/hash may block here
    std::shared_ptr<Gate> report_gate;          // the worker's own report write may block here, after Complete
    std::wstring report_path; std::string nonce;
    bool fail(RecOutcome why) {
        auto prior = outcome.load();
        while (prior == RecOutcome::Open || prior == RecOutcome::Draining)
            if (outcome.compare_exchange_weak(prior, why)) { cv.notify_all(); return true; }
        return false;
    }
};
inline bool write_file(const std::wstring& path, const std::string& text) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD n = 0; const bool ok = WriteFile(h, text.data(), DWORD(text.size()), &n, nullptr) && n == text.size();
    CloseHandle(h); return ok;
}
class Recorder {
    std::shared_ptr<RecorderState> s_;
    std::thread t_;
public:
    Recorder(std::wstring report_path, std::string nonce, std::shared_ptr<Gate> stop_gate, std::shared_ptr<Gate> report_gate) : s_(std::make_shared<RecorderState>()) {
        s_->report_path = std::move(report_path); s_->nonce = std::move(nonce); s_->stop_gate = std::move(stop_gate); s_->report_gate = std::move(report_gate);
        s_->producer = std::this_thread::get_id();     // the input thread creates it and is its only producer
        auto s = s_;
        t_ = std::thread([s] {
            { std::unique_lock<std::mutex> l(s->m); s->cv.wait(l, [&] { return s->producer_done.load() || s->outcome.load() == RecOutcome::Cancelled; }); }
            bool stopped = false;
            if (s->outcome.load() == RecOutcome::Draining) { s->stop_gate->pass(); stopped = true; }   // inner stop, close, closed-file hash
            RecOutcome final = s->outcome.load();
            if (stopped) { auto d = RecOutcome::Draining; if (s->outcome.compare_exchange_strong(d, RecOutcome::Complete)) final = RecOutcome::Complete; else final = s->outcome.load(); }
            // The worker's own report, its last act (D3 step 4): never written by the host. The write itself can block
            // after the outcome is already Complete (Astra 11867), so completion of the outcome is not completion of the worker.
            s->report_gate->pass();
            write_file(s->report_path, std::string("outcome\t") + name(final) + "\nnonce\t" + s->nonce + "\n");
            { std::lock_guard<std::mutex> l(s->m); s->worker_done = true; }
            s->cv.notify_all();
        });
    }
    Recorder(const Recorder&) = delete; Recorder& operator=(const Recorder&) = delete;
    // Producer only (D7). Off the producer thread this is a cancellation, never a finish.
    bool finish() {
        if (std::this_thread::get_id() != s_->producer) { s_->fail(RecOutcome::Cancelled); s_->cv.notify_all(); return false; }
        auto o = RecOutcome::Open; s_->outcome.compare_exchange_strong(o, RecOutcome::Draining);
        s_->producer_done = true; s_->cv.notify_all(); return true;
    }
    // The outcome and, separately, whether the worker thread actually finished and was joined. A Complete outcome
    // whose worker is still inside its report write is not a resolved recorder.
    struct Drained { RecOutcome outcome; bool joined; };
    Drained drain(Clock::time_point deadline) {
        std::unique_lock<std::mutex> l(s_->m);
        if (!s_->cv.wait_until(l, deadline, [&] { return s_->worker_done; })) { l.unlock(); s_->fail(RecOutcome::TimedOut); return {s_->outcome.load(), false}; }
        l.unlock(); if (t_.joinable()) t_.join(); return {s_->outcome.load(), true};
    }
    bool worker_done() { std::lock_guard<std::mutex> l(s_->m); return s_->worker_done; }
    std::shared_ptr<RecorderState> state() const { return s_; }
    ~Recorder() { if (t_.joinable()) { s_->fail(RecOutcome::Cancelled); t_.detach(); } }   // never waits (D9)
};

// The provider's read leases: one device may be inside a (blocked) read when shutdown runs; it is then leaked, never
// released under the reader (slice 3.1 semantics).
struct FakeLeases {
    std::shared_ptr<Gate> in_use;   // a reader holds the device while this gate is closed and entered
    int leaked = 0;
    void shutdown(Clock::time_point deadline) {
        while (Clock::now() < deadline) {
            { std::lock_guard<std::mutex> l(in_use->m); if (!(in_use->entered && !in_use->open)) { leaked = 0; return; } }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        leaked = 1;
    }
};

struct Budget { int recorder_ms = 1000, leases_ms = 200, workers_ms = 200; };
struct Report {
    bool resolved = false;
    std::vector<std::string> unresolved;
    RecOutcome recorder = RecOutcome::Open;
    bool recorder_joined = false;               // independent of the outcome (Astra 11867)
    long long elapsed_us = 0;
};
struct HostConfig {
    std::wstring folder; std::string nonce;
    std::shared_ptr<Gate> provider_gate = std::make_shared<Gate>(), control_gate = std::make_shared<Gate>(),
                          recorder_gate = std::make_shared<Gate>(), lease_gate = std::make_shared<Gate>(),
                          report_gate = std::make_shared<Gate>();
};

class Host {
    HostConfig c_;
    std::thread::id input_;
    HANDLE report_ = INVALID_HANDLE_VALUE;      // pre-opened: the report never waits behind a stdio lock (D2)
    Recorder recorder_;
    Worker provider_, control_;
    FakeLeases leases_;
    bool stopped_ = false; Report report_cache_;
public:
    std::atomic<bool> destroyed_without_stop{false};
    explicit Host(HostConfig c) : c_(std::move(c)), input_(std::this_thread::get_id()),
        recorder_(c_.folder + L"\\worker.tsv", c_.nonce, c_.recorder_gate, c_.report_gate), provider_("provider", c_.provider_gate), control_("control", c_.control_gate) {
        leases_.in_use = c_.lease_gate;
        report_ = CreateFileW((c_.folder + L"\\host-shutdown.json").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    Host(const Host&) = delete; Host& operator=(const Host&) = delete;
    void write_report(const char* state, const Report* r) {
        if (report_ == INVALID_HANDLE_VALUE) return;
        std::string t = std::string("{\"state\":\"") + state + "\",\"nonce\":\"" + c_.nonce + "\"";
        if (r) {
            t += ",\"recorder\":\"" + std::string(name(r->recorder)) + "\",\"recorderJoined\":" + (r->recorder_joined ? "true" : "false") +
                 ",\"elapsedUs\":" + std::to_string(r->elapsed_us) + ",\"unresolved\":[";
            for (size_t i = 0; i < r->unresolved.size(); ++i) t += (i ? ",\"" : "\"") + r->unresolved[i] + "\"";
            t += "]";
        }
        t += "}";
        SetFilePointer(report_, 0, nullptr, FILE_BEGIN); DWORD n = 0;
        WriteFile(report_, t.data(), DWORD(t.size()), &n, nullptr); SetEndOfFile(report_);
    }
    // D3: input thread only, once, every phase bounded and every phase attempted.
    Report stop(Budget b = {}) {
        if (stopped_) return report_cache_;
        stopped_ = true;
        const auto t0 = Clock::now();
        Report r;
        write_report("stopping", nullptr);                                      // phase 0
        if (std::this_thread::get_id() != input_) r.unresolved.push_back("stop off the input thread");
        recorder_.finish();                                                     // phase 4 (cancels off the producer thread)
        const auto drained = recorder_.drain(Clock::now() + std::chrono::milliseconds(b.recorder_ms));
        r.recorder = drained.outcome; r.recorder_joined = drained.joined;
        if (r.recorder != RecOutcome::Complete) r.unresolved.push_back(std::string("recorder: ") + name(r.recorder));
        if (!r.recorder_joined) r.unresolved.push_back("recorder: worker not joined");
        leases_.shutdown(Clock::now() + std::chrono::milliseconds(b.leases_ms)); // phase 5
        if (leases_.leaked) r.unresolved.push_back("lease leaked");
        provider_.request_stop(); control_.request_stop();                      // phase 6: one shared deadline
        const auto workers_deadline = Clock::now() + std::chrono::milliseconds(b.workers_ms);
        for (Worker* w : {&provider_, &control_}) if (!w->join_until(workers_deadline)) r.unresolved.push_back("worker " + w->name());
        r.resolved = r.unresolved.empty();                                      // phase 7 would release DirectInput only now
        r.elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
        write_report(r.resolved ? "resolved" : "unresolved", &r);               // phase 8
        report_cache_ = r;
        return r;
    }
    // D4, latched and unresolved: tainted; the external supervisor ends the process by its own handle.
    [[noreturn]] void park_for_supervisor() { HANDLE never = CreateEventW(nullptr, TRUE, FALSE, nullptr); for (;;) WaitForSingleObject(never, INFINITE); }
    // D4, ordinary and unresolved: no DLL detach can deadlock; labelled forced in the report already written.
    [[noreturn]] void forced_exit() { TerminateProcess(GetCurrentProcess(), ForcedExitCode); for (;;) Sleep(INFINITE); }
    std::shared_ptr<WorkerState> provider_state() const { return provider_.state(); }
    std::shared_ptr<RecorderState> recorder_state() const { return recorder_.state(); }
    // D9: never stops, never waits. Without a stop the shutdown file never reads "resolved".
    ~Host() {
        if (!stopped_) destroyed_without_stop = true;
        if (report_ != INVALID_HANDLE_VALUE) CloseHandle(report_);
    }
};

// D8: the runner's finalizer seals only with all three independent facts, bound to the capture's nonce.
struct WorkerReport { bool present = false; std::string outcome, nonce; };
struct HostReport { bool present = false; std::string state, nonce; };
struct ExitObservation { bool observed = false, identity_matches = false, forced = false; DWORD code = 0; };
inline bool may_seal(const std::string& nonce, const WorkerReport& w, const HostReport& h, const ExitObservation& e) {
    return w.present && w.outcome == "Complete" && w.nonce == nonce &&
           h.present && h.state == "resolved" && h.nonce == nonce &&
           e.observed && e.identity_matches && !e.forced && e.code == 0;
}
}
