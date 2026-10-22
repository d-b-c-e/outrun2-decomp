#pragma once
#include "desktop_settings.hpp"
#include "driving/pc_camera.hpp"
#include <array>
#include <functional>
#include <mutex>
#include <optional>
namespace outrun::desktop::camera {
constexpr unsigned Count=11;
struct Key { unsigned scan; const char* name; };
const std::vector<Key>& keys();
const char* key_name(unsigned scan);
const char* action_name(unsigned action);
struct Config {
    std::array<driving::MountedCameraPose,2> views{};
    std::array<unsigned,Count> bindings{{0x48,0x50,0x49,0x51,0x4b,0x4d,0x47,0x4f,0x4e,0x4a,0x4c}};
    float move_step{0.02f},tilt_step{1.f},fov_step{2.f};
};
using KeyStates=std::array<bool,256>;
class Model {
    Config saved_;
    std::array<double,Count> next_{};
    std::array<bool,Count> previous_{};
    bool wait_{true},dirty_{};
    double last_tick_{-1},last_edit_{};
    std::uint32_t clears_{};
    int capture_{-1};
    double capture_until_{};
    bool capture_wait_{};
    unsigned capture_key_{};
public:
    Config effective;
    std::optional<Config> proposal;
    std::vector<unsigned> reserved;
    std::string path,message;
    bool enabled{};
    int active_view{-1};
    double feedback_until{};
    void load(const Ini&,bool allowed);
    bool valid(const Config&,std::string&)const;
    bool propose(const Config&);
    bool retry();
    void cancel_proposal();
    void flush(double now,bool force=false);
    void edit_pose(unsigned view,const driving::MountedCameraPose&,double now);
    void begin_capture(unsigned action,double now);
    void cancel_capture(const char* why="Binding cancelled; previous assignment retained");
    bool capturing()const{return capture_>=0;}
    void capture(const std::vector<unsigned>& pressed,bool any_down,bool chord,double now);
    driving::MountedCameraPose tick(int view,bool eligible,const KeyStates& down,bool physical_down,
        double now,std::uint32_t clears,bool blocked=false);
};
Model& model();
std::recursive_mutex& mutex();
std::string values(int view,const driving::MountedCameraPose&);
}
