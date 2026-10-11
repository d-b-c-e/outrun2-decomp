// R2 (Astra 16553/16754): the adapter/action replay of a sealed OutRunDesktop capture, offline.
//   capture_replay <sealed capture folder> <validated export (P21Finalizer --seal-export)>
// Every recorded row's Frame and GameView run through a fresh Adapter built from the capture's own profile.ini
// transmission; its signals must equal the recorded ones, field by field. nativeSuppressed depends on the native pad
// input, which a capture does not record, so it is reported separately and not claimed. This is an adapter/action
// replay: not a raw-to-Core replay, a mappings test or a route claim. Prints one JSON summary line; exit 0 only when
// every row reconstructs.
// The export it consumes must be freshly generated from this sealed capture by the finalizer (--seal-export or
// --read-export, which verify the seal); the runner always does so (Astra 16889/16903). The row-count and first/last
// frame comparison with worker.tsv below is a sanity check against a mixed-up pair, not a binding.
#include "outrun_capture.hpp"
#include "shim.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

namespace {
namespace rec = dbce::shim::recorder;
using namespace outrun::desktop;
std::string read_text(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary); if (!f) throw std::runtime_error("unreadable: " + p.string());
    std::stringstream s; s << f.rdbuf(); return s.str();
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("usage: capture_replay <sealed capture folder> <validated export>");
        const std::filesystem::path capture(argv[1]), exported(argv[2]);
        if (read_text(capture / "bundle.meta") != CaptureContract) throw std::runtime_error("not an OutRunDesktop capture");
        if (!std::filesystem::exists(capture / "complete.tsv")) throw std::runtime_error("capture not sealed (no complete.tsv)");
        const auto profile = dbce::shim::read_profile(read_text(capture / "profile.ini"));
        if (!profile.ok) throw std::runtime_error("capture profile.ini does not parse");
        // Sanity check only (see above): the rows are the sealed worker report's written frames.
        std::vector<std::string> worker;{std::stringstream w(read_text(capture / "worker.tsv"));std::string f;while(std::getline(w,f,'\t'))worker.push_back(f);}
        if(worker.size()!=23)throw std::runtime_error("unknown worker report layout");
        const auto written=std::stoull(worker[2]),last=std::stoull(worker[5]),first_seq=std::stoull(worker[6]);
        std::uint64_t seen_first=0,seen_last=0;
        Adapter adapter(profile.section.transmission);
        std::uint64_t matched = 0, race = 0, injected = 0, native = 0; std::map<std::string, std::uint64_t> fields;
        std::string first;
        const auto rows = rec::read_export(exported, [&](const rec::Channels& row) {
            const auto frame = rec::decode(frame_channels(row));
            const auto view = decoded_view(row); const auto want = decoded_signals(row);
            if (!seen_first) seen_first = frame.seq; seen_last = frame.seq;
            outrun::platform::NativeInputState sink{}; adapter.apply(frame, view, sink);
            if (want.in_gameplay) ++race;
            for (const auto& a : frame.action) if (a.injected) { ++injected; break; }
            if (want.native_suppressed) ++native;
            bool same = true;
            for (const char* f : signal_differences(adapter.signals(), want)) {
                if (std::string(f) == "nativeSuppressed") continue;   // unrecorded native input: reported, not claimed
                same = false; ++fields[f];
                if (first.empty()) first = "frame " + std::to_string(want.frame_seq) + " " + f;
            }
            if (same) ++matched;
        });
        if (rows != written || seen_first != first_seq || seen_last != last)
            throw std::runtime_error("the export is not this capture's (rows " + std::to_string(rows) + "/" + std::to_string(written) +
                                     ", frames " + std::to_string(seen_first) + ".." + std::to_string(seen_last) + " vs " +
                                     std::to_string(first_seq) + ".." + std::to_string(last) + ")");
        std::string mismatch = "{";
        for (const auto& f : fields) mismatch += (mismatch.size() > 1 ? "," : "") + std::string("\"") + f.first + "\":" + std::to_string(f.second);
        mismatch += "}";
        const bool pass = rows > 0 && matched == rows;
        std::printf("{\"result\":\"%s\",\"rows\":%llu,\"matched\":%llu,\"raceRows\":%llu,\"injectedRows\":%llu,\"nativeSuppressedRows\":%llu,"
                    "\"transmission\":\"%s\",\"mismatchFields\":%s,\"firstMismatch\":\"%s\"}\n",
            pass ? "pass" : "fail", (unsigned long long)rows, (unsigned long long)matched, (unsigned long long)race,
            (unsigned long long)injected, (unsigned long long)native, profile.section.transmission.c_str(), mismatch.c_str(), first.c_str());
        return pass ? 0 : 1;
    } catch (const std::exception& e) { std::fprintf(stderr, "capture_replay: %s\n", e.what()); return 2; }
}
