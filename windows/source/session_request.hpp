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
}
