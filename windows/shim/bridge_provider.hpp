#pragma once
// Copied-source provider: owns only memory leases, never a physical device.
#include "shim.hpp"
#include <map>
#include <functional>
#include <mutex>

namespace dbce::shim {
struct BridgeDevice {DeviceInfo identity;ObjectCaps caps;std::uint64_t generation{};};
struct BridgeSample {SourceRead source;RawState raw;Call poll{Call::NotCalled},state{Call::NotCalled};};
class BridgeProvider final:public ProviderOwner {
public:
    static constexpr std::size_t Limit=64;
    explicit BridgeProvider(std::function<std::int64_t()> clock):clock_(std::move(clock)){}
    bool devices(const std::vector<BridgeDevice>&,std::string& why);
    // Complete frame batch: omitted samples are Pending; failed submission clears
    // all delivery. Each accepted physical attempt can be consumed just once.
    bool samples(const std::vector<BridgeSample>&,std::string& why);
    void finish_frame();
    std::vector<DeviceInfo> enumerate() override;
    OpenResult open(const DeviceInfo&) override;
    Call reacquire(Handle) override;
    void close(Handle) override;
    Call poll(Handle) override;
    Call state(Handle,RawState&) override;
    bool source_read(Handle,SourceRead&) override;
    std::int64_t now_us() override{return clock_();}
    ProviderShutdownReport shutdown(int) override;
private:
    struct Entry {BridgeDevice device;std::uint64_t last_sequence{};std::int64_t last_end{};bool failed{};};
    std::mutex mutex_;
    std::function<std::int64_t()> clock_;
    std::vector<Entry> entries_;
    std::uint64_t greatest_generation_=0; // globally unique source generation, never reused
    std::map<Handle,std::uint64_t> leases_;
    std::map<std::uint64_t,BridgeSample> frame_;
    Handle next_=0;bool stopped_=false;
    Entry* entry(std::uint64_t generation);
    const BridgeSample* sample(Handle);
};
}
