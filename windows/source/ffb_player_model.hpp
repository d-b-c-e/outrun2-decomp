#pragma once
// Explicit player-model choice and a pure request seam for fixtures. Runtime producer@2 delivery
// is wired separately through ffb_shadow_live and the existing gated ffbdev::Delivery.
#include "ffb_shadow.hpp"
#include <memory>
#include <string>

namespace outrun::desktop { class Ini; }
namespace outrun::desktop::ffb::player {
inline constexpr const char* LegacyId="outrun-legacy@1";
enum class Model { Legacy,Producer2,Refused };
struct Selection {
    Model model{Model::Legacy};
    producer2::Strengths strengths{};
    std::string refusal;
};
// [FFB] Model is absent or explicitly LegacyId by default. Producer gains live only in
// [FFBProducer2], each an independent strict 0..100 integer with reference 50.
Selection select(const Ini& settings);

// A selected producer request through a caller-owned software sink. It reuses the shadow's
// continuity/gate logic and producer2::Stream; this class itself has no device dependency.
class SelectedRequests {
public:
    using Sink=shadow::Core::RequestSink;
    SelectedRequests(const Selection& selection,Sink sink);
    void before_frame(const shadow::Gate& gate) noexcept;
    void observe(const shadow::Sample& sample) noexcept;
    bool active()const;
private:
    std::unique_ptr<shadow::Core> core_;
};
}
