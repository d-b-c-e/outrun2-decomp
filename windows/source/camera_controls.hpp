#pragma once
#include <string>
namespace outrun::desktop {
class Ini;
void camera_controls_configure(const Ini&,bool disabled,float physical_fov);
void camera_controls_wrap_input_filter();
bool camera_controls_tunable();
bool camera_controls_triples();
std::string camera_controls_label();
std::string camera_controls_notice();
void camera_controls_toggle_set();
void camera_controls_zoom(float degrees); // positive = wider, negative = narrower
void camera_controls_reset();
}
