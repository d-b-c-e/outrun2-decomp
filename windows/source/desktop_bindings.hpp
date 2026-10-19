#pragma once
// Saved binding drafts and accepted-observation adaptation. No reader, enumeration, device or force calls.
#include "../vendor/controls/dbce_binding_editor.hpp"
#include "../shim/shim.hpp"
#include <memory>

namespace outrun::desktop::bindings {
namespace edit=dbce::controls::edit;
std::vector<edit::AcceptedObservation> accepted_observations(const dbce::shim::Frame&,
    const std::vector<dbce::shim::DeviceReport>&,double local_now);
bool action_kind(const std::string& action,dbce::controls::Kind kind);
struct View {
    bool opened{},dirty{},pending_restart{},restart_required{},listening{};
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
