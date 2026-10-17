#pragma once
#include "desktop_settings.hpp"
namespace outrun::desktop {
void gamepad_vibration_configure(const Ini&,bool ordinary);
void gamepad_vibration_wrap_input_filter();
void gamepad_vibration_stop();
void gamepad_vibration_shutdown();
const char* gamepad_vibration_status_text();
}
