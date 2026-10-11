#pragma once
// The test fence for a latched (test-requested) process (windows/docs/INPUT-SHIM.md section 15, slice 2b).
//
// Order at process start, before anything writes (2b review: latch before any writer):
//   1. latch_requested(): does %LOCALAPPDATA%\dbce\outrun2006-decomp\inject.on exist? Nothing is written or read.
//   2. latched: prepare_run() makes the run folder exclusively, refuses reparse points, copies the owner's writable
//      state (SaveGame and options.ini) into it within bounds, hashes every copied file and writes run.json. The game
//      folder is only read. Any failure is a refusal: the game must not initialize.
//   3. The entry point then points home() at the run folder (data stays in the game folder), sends runtime.log there,
//      clears every OR2_* environment switch and keeps the game folder read-only, including the EXE image cache.
#include <cstdint>
#include <string>
#include <vector>
namespace outrun::desktop::fence {
struct SeedLimits { std::size_t max_files=256; std::uint64_t max_bytes=64ull<<20; };
struct SeededFile { std::wstring relative; std::uint64_t size{}; std::string sha256; };
struct Plan {
    bool latched{};
    std::wstring dbce_folder;            // %LOCALAPPDATA%\dbce\outrun2006-decomp
    std::wstring game_root;              // data, read only
    std::wstring run_root;               // writable state; home() under the latch
    std::string nonce;                   // from inject.on when it parses (the run is refused later if it does not)
    std::string refusal;                 // non-empty: refused, the game must not initialize
    std::vector<SeededFile> seeded;
    std::uint64_t seeded_bytes{};
    bool exe_cache_present{};            // the game folder has an EXE image cache (used read-only)
};

// %LOCALAPPDATA%\dbce\outrun2006-decomp, or "" when LOCALAPPDATA is unset.
std::wstring dbce_folder();
// Step 1: only whether inject.on exists.
bool latch_requested(const std::wstring& dbce_folder);
// Step 2. `pid` names the run folder together with the nonce (or "nononce" when inject.on does not parse).
Plan prepare_run(const std::wstring& dbce_folder,const std::wstring& game_root,std::uint32_t pid,const SeedLimits& limits={});
// The reason, in %LOCALAPPDATA%\dbce\outrun2006-decomp\refused-<pid>.txt (never in the game folder).
bool write_refusal(const Plan&,std::uint32_t pid);

// Effective paths under the plan (ordinary: the game folder; latched: the run folder).
std::wstring home_of(const Plan&,const std::wstring& game_root);
std::wstring log_path(const Plan&,const std::wstring& game_root);

// Clears every environment variable whose name starts with OR2_ (diagnostic writers and development switches).
// Returns the names cleared.
std::vector<std::string> clear_development_switches();

// Helpers (exposed for tests).
std::string sha256_file(const std::wstring& path,std::uint64_t& size,bool& ok);
bool has_reparse_point_on_path(const std::wstring& path);   // the path or any existing folder above it
std::string utf8(const std::wstring&);
}
