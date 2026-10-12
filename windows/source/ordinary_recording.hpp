#pragma once
// Play with Recording on the ordinary shim backend (hub artifacts/outrun-owner-delivery-plan-20261012/
// RECORDING-SKETCH.md r2; romi 33093/33134/33147/33165/33281/33423): the ordinary ProviderHost's recorder.
// - Unarmed (no record request): no capture at all, as NoCapture was.
// - Armed: the shared ShimRecorder writes capture contract @2 into <recording>\capture, one row per tick pushed at the
//   consumed-input seam (shim_input.cpp). Normal outputs stay as the owner's settings say.
// - Stop (F11, the request-bound stop file, the capture limit, the exit): finish() on the input thread, then exactly
//   ONE drain() on a saver thread with the whole save budget (a drain that times out is final, worker.hpp); its
//   result is cached for the host's later stop. Only when the six raw members pass the admission (exact names, the
//   worker report final with every row written, the contract and profile bytes) does the saver write
//   recorder-complete.json (outrun.recorder-complete@1) into the capture beside them.
// - "complete" is all the native side claims. The saved-while-alive seal (recording-complete.tsv) and its receipt
//   (<result>\recording-verified.json, outrun.recording-verified@1) are the managed finalizer's (romi 33281/33423);
//   the badge says saved only once that receipt names this request, this process and this artifact. The P21 process
//   seal at exit stays separate.
// - status.json (outrun.session-status@1) in the result folder: written by the input thread only, on each change;
//   state recording | saving | complete | saved (complete and the receipt observed) | failed.
#include "outrun_recorder.hpp"
#include "session_request.hpp"
#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <string>
namespace outrun::desktop {
class OrdinaryRecorder {
public:
    struct Arm {
        session::Request request;
        std::string profile;                   // the exact [Controls] bytes Core parsed
        std::string exe_sha256;                // the running OutRunDesktop.exe, read at start
        std::string adapter_version;           // the shim adapter's version line (shim_input.cpp)
        double hz=60,seconds=300;              // the preflight-supported maximum (romi 33147: no 20 min claim)
        bool saver_thread=true;                // test seam: false behaves as a saver thread that could not start
    };
    enum class State { None, Recording, Saving, Complete, Failed };
    OrdinaryRecorder();                        // unarmed
    explicit OrdinaryRecorder(Arm);            // a handle: copies share the one recording (the host holds one)
    bool armed()const;
    // Input thread.
    bool recording()const;                     // armed, and not stopped
    void set_devices(std::vector<dbce::shim::DeviceReport>);
    void push(const Snapshot&);
    std::uint64_t pushed()const;
    // The capture limit: hz x seconds rows, or the rows' own span one second short of the writer's duration limit.
    bool at_limit()const;
    // A tick whose snapshot never reached the consumed-input seam (the filter ran twice without it): the capture
    // cannot claim the final input of every row, so it never completes.
    void count_missed_final();
    void stop(const char* by);                 // "f11" | "stop-request" | "limit" | "exit"; once
    void fail(const std::string& why);         // before any capture (no shim host): failed, nothing to drain
    // The managed receipt (<result>\recording-verified.json), read when complete and not yet saved; true once saved.
    bool observe_receipt();
    State state()const;                        // any thread
    std::string why()const;
    bool saved()const;                         // any thread: the receipt was observed
    std::string badge()const;                  // any thread (the present thread draws it): empty when unarmed
    // Waits (bounded) for the saver; true when it finished. Input thread, at the exit.
    bool wait_saved(dbce::shim::hosting::Clock::time_point deadline);
    // The status writer (input thread): status.json when the state or the reason changed since the last write.
    void publish_status(bool force=false);
    const session::Request* request()const;
    // ProviderHost's recorder contract.
    void finish();                             // the host's stop: stop("exit") when still recording
    struct Drained{dbce::shim::hosting::RecOutcome outcome;bool joined;};
    Drained drain(dbce::shim::hosting::Clock::time_point deadline);
    struct Shared;
private:
    std::shared_ptr<Shared> s_;
};
// status.json (outrun.session-status@1) in the request's result folder; also for a refused request (state "failed").
bool write_session_status(const session::Request&,const char* state,const std::string& why,const std::string& stopped_by,
                          const std::string& started_utc);
// Writes `bytes` to `path` in one step (a temp file, flushed, then MoveFileEx REPLACE_EXISTING | WRITE_THROUGH).
bool replace_file_atomically(const std::filesystem::path& path,const std::string& bytes);
// The managed receipt's fields when it is one flat JSON object of strings, integers and booleans (anything else:
// nullopt). Strings are kept unescaped only when plain ASCII without escapes; integers and booleans as written.
std::optional<std::map<std::string,std::string>> flat_json(const std::string& text);
// SHA-256 of a file, streamed, lowercase hex; `bytes` its length. Throws when it cannot be read.
std::string file_sha256(const std::filesystem::path& p,std::uint64_t& bytes);
}
