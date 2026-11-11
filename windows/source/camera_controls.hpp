#pragma once
// The race cameras, the game's side. The game's own views stay as they are (bumper, cockpit, chase), and a Bonnet view
// the game lacks is added to its cycle after the bumper. What is attached to each view (Game or Realistic framing with
// F7, the zoom and position offsets with the numpad, their saved keys and the on-screen name) is the Wheelkit runtime's
// (wheelkit.h wk_cameras_frame); this file applies it to the view the game calculated.
namespace outrun::platform { struct NativeRuntimeContext; }
namespace outrun::desktop {
class Ini;
// realistic_fov: the runtime's vertical field of view for the seat's true view (radians; 0 without measured screens).
void camera_controls_configure(const Ini&,float realistic_fov);
// Once per input tick, from the Wheelkit filter: tells the runtime which view is on screen and takes what applies to it.
void camera_controls_tick(const platform::NativeRuntimeContext*);
}
