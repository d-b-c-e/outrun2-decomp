// The teardown audit's view of the production host (windows/host): a header-only template emits no code until it is
// instantiated, so this unit instantiates ProviderHost with a minimal lazy recorder and takes every member's address.
// It is compiled only by teardown_objects.py; nothing links or runs it.
#include "provider_host.hpp"

namespace audit_unit {
struct LazyRecorder {   // the host's recorder contract: nothing starts before the first push
    void finish() noexcept {}
    struct Drained { dbce::shim::hosting::RecOutcome outcome; bool joined; };
    Drained drain(dbce::shim::hosting::Clock::time_point) { return {dbce::shim::hosting::RecOutcome::Complete, true}; }
};
}
template class dbce::shim::hosting::ProviderHost<audit_unit::LazyRecorder>;   // emit every member
