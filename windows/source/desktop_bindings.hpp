#pragma once
// Saved binding drafts and accepted-observation adaptation. No reader, enumeration, device or force calls.
#include "../vendor/controls/dbce_binding_editor.hpp"
#include "../shim/shim.hpp"
#include <memory>

namespace outrun::desktop::bindings {
namespace edit=dbce::controls::edit;
std::vector<edit::AcceptedObservation> accepted_observations(const dbce::shim::Frame&,
    const std::vector<dbce::shim::DeviceReport>&,double local_now);
// A device the Native reader (wheel_input) already opened, as it read it this tick: DIJOYSTATE2 axes X Y Z Rx Ry Rz and
// the two sliders at the 0..65535 range that reader sets, raw buttons and POVs, its DIDEVCAPS button/POV counts, and the
// axis inventory read once at open (which axes exist, and which read back 0..65535).
struct NativeSample {
    std::string dev,prod,name;
    std::uint64_t epoch{},sequence{};   // epoch: the handle's open count; sequence: its successful reads
    std::uint32_t buttons{},povs{};
    std::uint32_t axis_mask{},range_mask{};   // the axes the device has, and those reading 0..65535 (wheel_axes.hpp)
    std::array<std::int32_t,8> axes{};
    std::array<std::uint8_t,128> button_state{};
    std::array<std::uint32_t,4> pov_state{};
};
std::vector<edit::AcceptedObservation> native_observations(const std::vector<NativeSample>&,double local_now);
bool action_kind(const std::string& action,dbce::controls::Kind kind);
struct View {
    bool opened{},dirty{},pending_restart{},restart_required{},file_changed{},listening{};
    std::string action,transmission,message,selected_instance;
    edit::SelectionMode selection{edit::SelectionMode::Saved};
    std::optional<dbce::controls::Binding> binding;
    std::vector<edit::AcceptedObservation> devices;
};
class Editor {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    explicit Editor(const std::string& fixed_path,bool ordinary_process);
    ~Editor();
    bool open();
    bool opened()const;
    void cancel();                         // discard local edits only
    void hide();                           // cancel capture, preserve draft
    void saved_file_changed();             // Stop FFB persisted Off; retain stale draft, require explicit Cancel
    View view(double now,bool can_edit);
    void observe(const std::vector<edit::AcceptedObservation>&,double now,bool can_edit);
    bool select_action(const std::string&);
    bool select_device(const std::optional<edit::AcceptedObservation>&,double now,bool can_edit);
    bool begin_capture(double now,bool can_edit);
    void stop_capture();
    bool clear(bool can_edit);
    bool invert(bool value,bool can_edit);
    bool transmission(const std::string&,bool can_edit);
    bool save(bool can_edit);               // explicit guarded save/backup; never reload active Core
};
}
