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
//   packageSealSha256=<64 hex>        playback only
//   tape=<absolute file>              playback only
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
    std::filesystem::path result,recording,tape;
    std::string package_seal_sha256;
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
// claimed first by a rename beside itself (.taken-<pid>: no later launch can take it), then copied exclusively into its
// result folder as session-request.txt once it parses (any volume; never over an earlier one), else kept beside itself
// as .refused-<pid>. None: no request, ordinary play. Record: armed (the result and recording folders made, the
// capture folder not there yet; `exe_sha256` the running exe's). Refused: a record request refused, ordinary play without a capture
// (status.json "failed" when its result folder is known). Exit: a playback request (this build plays nothing back) or
// an unreadable one: never ordinary play, the process ends before the game.
struct Intake {
    enum class Kind { None, Record, Refused, Exit } kind{Kind::None};
    Request request;
    std::string exe_sha256,why;
};
Intake take(const std::filesystem::path& file,const std::filesystem::path& game_folder,bool shim_backend,std::uint64_t now_unix);
}
