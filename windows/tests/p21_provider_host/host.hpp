#pragma once
// Fixture defaults only. Both simulated and real recorder/device probes use
// the same host implementation that the desktop can adopt.
#include "provider_host.hpp"
#include "p21_host_spike/host.hpp"
namespace p21k {
using dbce::shim::hosting::Clock;
using dbce::shim::hosting::Budget;
using dbce::shim::hosting::Report;
using p21_spike::Gate;
using p21_spike::RecOutcome;
using p21_spike::Recorder;
template<class RecorderType = Recorder>
class BasicProviderHost : public dbce::shim::hosting::ProviderHost<RecorderType> {
    using Base = dbce::shim::hosting::ProviderHost<RecorderType>;
public:
    // Keep the prior fixture's relaxed clock default; production's default is 100 ms.
    BasicProviderHost(std::wstring folder, std::string nonce, IDirectInput8W* input,
                      const std::string& profile, bool latched, std::unique_ptr<RecorderType> recorder,
                      dbce::shim::Capability capability = {}, HWND window = nullptr, std::int64_t freshness_us = 10'000'000)
        : Base(std::move(folder), std::move(nonce), input, profile, latched,
               std::move(recorder), capability, window, freshness_us) {}
    BasicProviderHost(std::wstring folder, std::string nonce, IDirectInput8W* input,
                      const std::string& profile, bool latched)
        : Base(folder, nonce, input, profile, latched,
            std::make_unique<RecorderType>(folder + L"\\worker.tsv", nonce,
                std::make_shared<Gate>(), std::make_shared<Gate>()), {}, nullptr, 10'000'000) {}
};
using ProviderHost = BasicProviderHost<>;
}
