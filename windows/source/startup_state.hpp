#pragma once
// A recording's startup snapshot (outrun.startup-state@1; Wheelkit docs/OUTRUN-PLAYBACK-CONTRACT.md, sana 37135/37176):
// what the game read at the start of an ordinary recording, so that its playback starts from the same state in a
// process-private home, never in the owner's files. The companion writes it at -Record with the game closed (the owner's
// files only read); a playback process verifies it and seeds its home from it.
//   <startup>\complete.tsv          the seal: startupSealSha256 is its sha256
//     outrun.startup-state@1
//     requestId<TAB><the recording's 32 hex>
//     exeSha256<TAB><the recording's exe, 64 hex>
//     file<TAB><relative>\t<size>\t<sha256>   one per member present
//     absent<TAB><relative>                    options.ini / OutRunDesktop.ini / SaveGame when there was none
//   <startup>\SaveGame\**, options.ini, OutRunDesktop.ini   the members, as they were
// The inventory is the consumer's, fixed here (the fence's writable state, test_fence.cpp, plus the settings file):
// SaveGame/** (files only, at most 4 folders deep, the fence's limits) and the two top-level files. Strict: anything
// else in the folder or the seal, a link, a duplicate, or a member whose bytes differ refuses the snapshot.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace outrun::desktop::startup {
inline constexpr const char* Schema="outrun.startup-state@1";
inline constexpr std::size_t MaxFiles=256+2;                 // the fence's SeedLimits, plus the two top-level files
inline constexpr std::uint64_t MaxBytes=(64ull<<20)+(1ull<<20);
struct Member { std::string relative; bool absent{}; std::uint64_t size{}; std::string sha256; };
struct Snapshot { std::string request_id,exe_sha256; std::vector<Member> members; };
struct Verified { bool ok{}; Snapshot snapshot; std::string why; };
// A relative member path the inventory admits ("/" separated).
bool admitted(const std::string& relative);
// The seal's bytes parsed (no file access): schema, the two identity lines, the members' shape and the inventory rules.
Verified parse(const std::string& seal_bytes);
// <folder>\complete.tsv against `seal_sha256`, then parse, then the folder holds exactly the listed members (no link,
// nothing else) with the listed sizes and hashes, and the snapshot names `exe_sha256`.
Verified verify(const std::filesystem::path& folder,const std::string& seal_sha256,const std::string& exe_sha256);
// A new home (it must not exist) seeded from a verified snapshot: home\SaveGame (always made) with the SaveGame members,
// and options.ini when present. Each copy is new (never over a file) and hashed again after the copy. "" or why not.
std::string seed(const std::filesystem::path& folder,const Snapshot& snapshot,const std::filesystem::path& home);
// The playback process's settings: the recorded OutRunDesktop.ini with its [Window] and [Triple] sections replaced by
// the owner's current ones (the rig's presentation), every other byte the recording's. Pure.
std::string playback_settings(const std::string& recorded,const std::string& owner);
}
