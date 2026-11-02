#pragma once
// The control journal's identity for a latched host, built once on the input thread before the control worker starts
// (moved from OutRunDesktop's shim_input.cpp so the desktop and dbce_shim.dll build it the same way). No file is read:
// the profile bytes are the ones Core parsed. The executable and adapter-schema hashes are not built yet; the journal
// reports them as unavailable rather than inventing them (CONTROL-JOURNAL.md section 10).
#include "control_journal.hpp"
#include "shim.hpp"
#include <memory>
#include <string>
namespace dbce::shim::hosting {

inline std::uint64_t process_start_time() {
    FILETIME a, b, k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &a, &b, &k, &u)) return 0;
    return (std::uint64_t(a.dwHighDateTime) << 32) | a.dwLowDateTime;
}

// `controls_reads` as the host's (ProviderHost): only the file hash and the revision are taken, which a refusing read
// also sets, but the identity reads the profile as the Core it describes. `input_source` (optional) is the copied-input
// bridge's versioned provider identity, written on every journal row (CONTROL-JOURNAL.md); never a sample capture.
template <class Host>
std::shared_ptr<const control::JournalIdentity> journal_identity(const Host& host, const std::string& nonce, const std::string& profile,
                                                                  const char* core_version, const char* adapter_version,
                                                                  int controls_reads = 1, const char* input_source = nullptr) {
    auto id = std::make_shared<control::JournalIdentity>();
    id->id.process = GetCurrentProcessId();
    id->id.process_start = process_start_time();
    for (std::size_t i = 0; i < id->id.nonce.size() && i < nonce.size(); ++i) id->id.nonce[i] = std::uint8_t(nonce[i]);
    const auto parsed = read_profile(profile, controls_reads);
    id->id.profile = parsed.file_hash;             // read_profile hashes the very bytes it parsed
    id->latched = true; id->nonce_text = nonce;
    id->executable_known = false; id->build_known = false;   // the identity builder (by handle, shared with the observer) is next
    id->core_version = core_version; id->adapter_version = adapter_version;
    if (input_source) id->input_source = input_source;
    const auto devices = host.device_reports();
    for (const auto& d : devices) id->devices.push_back(d.identity);
    id->id.profile_devices = std::uint32_t(id->devices.size());
    const auto start = host.start_report();
    id->start_line = start.line;
    for (const auto& n : start.notes) id->start_line += "\n" + n;
    id->profile_revision = parsed.ok ? parsed.section.revision : "";
    return id;
}
}
