// The teardown audit's view of the production recorder (windows/host/recorder): its classes are header-only and emit no
// code until used, so this unit uses each lifetime-bearing type (the thread end, the controller, the producer, the
// capture sink) and takes their members' code into one object. It is compiled only by teardown_objects.py; nothing
// links or runs it.
#include "capture.hpp"

namespace audit_unit {
void recorder_types() {
    dbce::shim::recorder::ThreadEnd end;   // its destructor: the one detach of a still-running worker (D9)
    (void)end.wait_until(std::chrono::steady_clock::now());
    auto id = std::make_shared<dbce::shim::recorder::Identity>();
    dbce::shim::recorder::Controller c(16, 4, 16 * sizeof(dbce::shim::recorder::Packet),
        std::make_unique<dbce::shim::recorder::QueuedSink>("audit.jsonl", std::vector<std::string>{"a", "b", "c", "d"}, id, 0,
            dbce::session::Metadata{"audit", "audit", "audit", {}, {}}), 0);
    auto p = c.producer();
    (void)p.push(dbce::shim::recorder::Packet{});
    p.finish(0);
    (void)c.drain(std::chrono::milliseconds(1));
    (void)c.join_until(std::chrono::steady_clock::now());
    dbce::shim::recorder::write_new("audit.tsv", "x");
}
}
