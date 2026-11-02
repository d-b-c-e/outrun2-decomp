#pragma once
#include <string>
#include <array>
namespace outrun::desktop {
class Ini;
void camera_controls_configure(const Ini&,bool disabled,float physical_fov);
void camera_controls_wrap_input_filter();
bool camera_controls_available();
bool camera_controls_tunable();
bool camera_controls_triples();
std::string camera_controls_label();
std::string camera_controls_notice();
std::string camera_controls_comparison_notice();
void camera_controls_end_comparison();
void camera_controls_toggle_set();
void camera_controls_zoom(float degrees); // positive = wider, negative = narrower
struct CameraFovValues {float calculated{},current{},offset{};bool valid{};}; // vertical degrees
CameraFovValues camera_controls_fov();
void camera_controls_set_fov(float degrees); // absolute current vertical FOV, stored as an offset on its calculation
void camera_controls_reset();
void camera_controls_activate(unsigned action); // same eleven actions as CameraKeyLabels
void camera_controls_move(unsigned axis,float amount); // side / up / forward / pitch
bool camera_controls_pose_available();
std::array<float,4> camera_controls_pose();
std::array<std::string,11> camera_controls_key_names();
std::array<std::string,11> camera_controls_key_errors();
bool camera_controls_keys_migrated();
void camera_controls_set_key(unsigned,const std::string&);
void camera_controls_default_keys();
float camera_controls_step(unsigned); // move metres / tilt degrees / FOV degrees
void camera_controls_set_step(unsigned,float);
void camera_controls_default_steps();
}
