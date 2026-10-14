#pragma once
// Optional software-only observer of the just-completed native GamePlCar control.
#include "ffb_producer2.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace outrun::desktop::ffb::shadow {
struct Gate {
    std::uint32_t mode{},generation{},continuity{},frame_counter{},update_index{};
    std::uint64_t attempt{};
    bool car_active{},paused{},panel_open{},recovery_pending{};
};
struct Sample {
    Gate gate{};
    bool producer_fresh{};
    const std::uint8_t* car{};
    std::size_t car_bytes{};
};

// Core has no file, device, game or F6 dependency. A failed writer detaches it.
class Core {
public:
    explicit Core(std::function<bool(const std::string&)> write);
    static std::string header();
    void before_frame(const Gate& gate) noexcept;
    void observe(const Sample& sample) noexcept;
    void stop() noexcept;
    bool active()const{return active_;}
private:
    bool write_row(const Gate& gate,const char* origin,const char* gate_reason,const producer2::Row& row) noexcept;
    void neutral(const Gate& gate,const char* origin,const char* why);
    void mode(std::uint32_t current);
    void generation(const Gate& gate,const char* origin);
    bool recovery(const Gate& gate,const char* origin);
    std::function<bool(const std::string&)> write_;
    producer2::Stream stream_;
    std::uint32_t epoch_{},generation_{},continuity_{};
    std::uint32_t valid_frame_{},valid_update_{};
    bool previous_game_{},have_valid_pair_{},neutral_latched_{true},active_{true};
    bool panel_open_{},paused_{};
};
}
