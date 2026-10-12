#pragma once
// The ordinary session request (hub artifacts/outrun-owner-delivery-plan-20261012/RECORDING-SKETCH.md, romi 33093 /
// 33165): Wheelkit's (or a runner's) one-shot, expiring instruction to record this ordinary session, written beside
// the fence's files but distinct from inject.on: %LOCALAPPDATA%\dbce\outrun2006-decomp\session-request.txt.
//   outrun.session-request@1
//   requestId=<32 lowercase hex>
//   action=record | playback
//   expiresUnix=<unix seconds, after now and at most 10 min ahead>
//   result=<absolute folder>          status.json / result.json go here
//   recording=<absolute folder>       the capture goes in its capture\ child (record)
//   packageSealSha256=<64 hex>        playback only: the source recording's seal
//   tape=<absolute file>              playback only: the managed export of the source (P21Finalizer --export-recording)
//   tapeSha256=<64 hex>               playback only: the tape's bytes (the export has no binding of its own)
//   exeSha256=<64 hex>                playback only: the recording's exe; the running one must be it
//   startup=<absolute folder>         playback only: the package's startup snapshot (startup_state.hpp)
//   startupSealSha256=<64 hex>        playback only: sha256 of startup\complete.tsv
// Strict: at most 4 KiB, UTF-8, the first line exactly the schema, every other line key=value, each key once, no
// unknown key. result and recording are separate siblings (neither inside the other) and outside the game folder.
// What a refusal means (romi 33093): a record request that is refused starts ordinary play with no capture; a
// playback request, or one whose action cannot be read, never yields ordinary play: the process ends before the
// frontend (this build plays nothing back). Pure: tests/test_session_request.cpp.
#include <cstdint>
#include <filesystem>
#include <string>
namespace outrun::desktop::session {
inline constexpr const char* RequestSchema="outrun.session-request@1";
enum class Action { None, Record, Playback };
struct Request {
    Action action{Action::None};
    std::string request_id;
    std::uint64_t expires_unix{};
    std::filesystem::path result,recording,tape,startup;
    std::string package_seal_sha256,tape_sha256,exe_sha256,startup_seal_sha256;
};
struct Parsed {
    bool ok{};
    Request request;
    std::string why;
    // The action the request named, or Playback when it could not be read: refused, it must not become ordinary play.
    Action refused_as{Action::None};
};
Parsed parse(const std::string& bytes,std::uint64_t now_unix,const std::filesystem::path& game_folder);
// %LOCALAPPDATA%\dbce\outrun2006-decomp\session-request.txt (empty when LOCALAPPDATA is not set).
std::filesystem::path request_path();
// A path's UTF-8 bytes as a std::string, alike in C++17 and C++20 (where u8string() is a std::u8string).
inline std::string utf8(const std::filesystem::path& p){const auto s=p.u8string();return std::string(s.begin(),s.end());}
// The start's intake (an unlatched process, before the game starts; ordinary_recording.cpp): the request at `file`,
// claimed first by a rename beside itself (.taken-<pid>-<n>: no later launch can take it), then copied exclusively into its
// result folder as session-request.txt once it parses (any volume; never over an earlier one), else kept beside itself
// as .refused-<pid>-<n>. None: no request, ordinary play. Record: armed (the result and recording folders made, the
// capture folder not there yet; `exe_sha256` the running exe's). Refused: a record request refused, ordinary play without a capture
// (status.json "failed" when its result folder is known). Playback: admitted (the running exe is the recording's, the
// tape's bytes and the startup snapshot verified, the recording folder made without a capture, `home` = <result>\home
// made and seeded; `recorded_settings` the snapshot's OutRunDesktop.ini bytes when it had one). Exit: a refused playback
// request or an unreadable one: never ordinary play, the process ends before the game.
struct Intake {
    enum class Kind { None, Record, Refused, Exit, Playback } kind{Kind::None};
    Request request;
    std::string exe_sha256,why;
    std::filesystem::path home;
    std::string recorded_settings;bool has_recorded_settings{};
};
Intake take(const std::filesystem::path& file,const std::filesystem::path& game_folder,bool shim_backend,std::uint64_t now_unix);
}
