#pragma once
// Atomic observation transport. Domain encoding and identity/freshness validation
// are separate: a coherent snapshot alone does not qualify a game or recording.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <thread>

namespace dbce::shim::observe {
constexpr std::size_t PayloadWords = 4096;
static_assert(sizeof(void*) == 8, "Observer transport currently qualified only for x64");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free && sizeof(std::atomic<std::uint32_t>) == 4,
              "Payload words require the qualified atomic ABI");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free && sizeof(std::atomic<std::uint64_t>) == 8,
              "Sequence requires the qualified x64 atomic ABI");
struct alignas(64) Storage {
    std::atomic<std::uint64_t> sequence;
    std::array<std::atomic<std::uint32_t>, PayloadWords> words;
    Storage() noexcept : sequence(0) { for(auto& w:words) w.store(0, std::memory_order_relaxed); }
};
static_assert(offsetof(Storage, words) == 8 && alignof(Storage) == 64, "Fixed mapped offsets");
using Payload = std::array<std::uint32_t, PayloadWords>;
struct Snapshot { std::uint64_t sequence{}; Payload words{}; };

class Writer {
    Storage& storage_;
    std::thread::id thread_ = std::this_thread::get_id();
    std::uint64_t last_{};
public:
    explicit Writer(Storage& s) : storage_(s) {}
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    // Exactly one input-thread writer. All payload operations are atomic, even
    // when a reader overlaps an update. Never wait for a reader or allocate.
    bool publish(const Payload& payload) noexcept {
        if(std::this_thread::get_id()!=thread_ || last_ > std::numeric_limits<std::uint64_t>::max()-2) return false;
        auto expected=last_;
        if(!storage_.sequence.compare_exchange_strong(expected,last_+1,std::memory_order_seq_cst)) return false;
        for(std::size_t i=0;i<PayloadWords;++i) storage_.words[i].store(payload[i],std::memory_order_seq_cst);
        last_+=2;
        storage_.sequence.store(last_,std::memory_order_seq_cst);
        return true;
    }
};

// Reader never writes to the mapping. Failure leaves the caller's previous
// snapshot untouched; it must not be displayed as a new observation.
namespace detail {
template<class ReadWord>
inline bool read_words(const Storage& storage, Snapshot& out, unsigned attempts, ReadWord read_word) noexcept {
    if(attempts>32) attempts=32;
    for(unsigned a=0;a<attempts;++a) {
        const auto before=storage.sequence.load(std::memory_order_seq_cst);
        if(before==0 || (before&1)) continue;
        Snapshot next; next.sequence=before;
        for(std::size_t i=0;i<PayloadWords;++i) next.words[i]=read_word(i);
        if(storage.sequence.load(std::memory_order_seq_cst)==before) { out=next;return true; }
    }
    return false;
}
}
inline bool read(const Storage& storage, Snapshot& out, unsigned attempts=8) noexcept {
    return detail::read_words(storage,out,attempts,[&](std::size_t i){return storage.words[i].load(std::memory_order_seq_cst);});
}
}
