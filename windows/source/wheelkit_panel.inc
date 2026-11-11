// The F6 pages (wheelkit_runtime.cpp hands them to the runtime): the runtime's descriptor format
// (wheelkit runtime/native/panel/dbce_panel_descriptor.h). Rows the game handles are listed in wheelkit_runtime.cpp's
// table with the OutRunDesktop.ini key each one saves; telemetry.enabled is the runtime's own.
R"json({"settings":{
 "product":"OutRun 2006 Coast 2 Coast","version":"0.2.0","modPage":"https://github.com/d-b-c-e/outrun2-decomp",
 "keys":"F6 settings","pages":{
 "controls":[
  {"id":"controls.profile","kind":"status","label":"Wheel profile","text":"No profile from Wheelkit"},
  {"id":"controls.sensitivity","kind":"percent","label":"Steering sensitivity","min":25,"max":300,"default":100,"apply":"next-start","help":"How far the cars steer for a turn of the wheel."},
  {"id":"controls.deadzone","kind":"percent","label":"Steering dead zone","min":0,"max":90,"default":0,"apply":"next-start","help":"Wheel travel around the center that does not steer."},
  {"id":"controls.manual","kind":"toggle","label":"Start on Manual","default":false,"apply":"next-start","help":"Preselects Manual in the game's transmission menu."},
  {"id":"controls.horn","kind":"toggle","label":"Horn","default":true,"level":"advanced","apply":"next-start","help":"The horn button sounds the horn in the race."}
 ],
 "ffb":[
  {"id":"ffb.status","kind":"status","label":"Force feedback","text":"Not available for OutRun 2006 yet"}
 ],
 "display":[
  {"id":"display.screens","kind":"choice","label":"Screens","choices":["Off","Surround","Separate monitors"],"default":"Off","apply":"next-start","help":"Three screens are used when you have them. Menus stay on the center screen."},
  {"id":"display.latency","kind":"choice","label":"Frame latency","choices":["0 (plain)","1","2","3"],"default":"2","apply":"next-start","help":"Frames that may wait for the display; 2 is the default."},
  {"id":"display.ui-scaling","kind":"choice","label":"UI scaling","choices":["Stretch 2D","Wide HUD (partial)","Centered 4:3"],"default":"Centered 4:3","apply":"next-start","help":"How menus and the HUD fit a wide screen."},
  {"id":"display.letterbox","kind":"choice","label":"Black bars","choices":["Off","Outside the race","Always"],"default":"Outside the race","apply":"next-start","help":"Black bars beside the centered 4:3 picture."},
  {"id":"display.views","kind":"choice","label":"Projection","choices":["Planar","Angled"],"default":"Planar","level":"advanced","apply":"next-start","help":"Angled draws one view per screen at its real angle. F7 then switches a camera between Game and Realistic."},
  {"id":"display.borderless","kind":"toggle","label":"Borderless window","default":true,"level":"advanced","apply":"next-start","help":"Off opens a 1280x720 window instead."},
  {"id":"display.cursor","kind":"toggle","label":"Hide the mouse cursor","default":true,"level":"advanced","apply":"next-start","help":"Hides the Windows cursor over the game."},
  {"id":"display.aniso","kind":"choice","label":"Anisotropic filtering","choices":["Game","2x","4x","8x","16x"],"default":"Game","level":"advanced","apply":"next-start","help":"Sharper textures at an angle."},
  {"id":"display.reflection","kind":"choice","label":"Car reflections","choices":["128","256","512","1024","2048"],"default":"128","level":"advanced","apply":"next-start","help":"The car reflection's resolution; 128 is the game's."},
  {"id":"display.lods","kind":"toggle","label":"Full detail for other cars","default":false,"level":"advanced","apply":"next-start","help":"Other cars always at their full model."},
  {"id":"display.culling","kind":"toggle","label":"Draw the whole stage","default":false,"level":"advanced","apply":"next-start","help":"No pop-in; more drawing work."},
  {"id":"display.transparency","kind":"toggle","label":"Transparency supersampling","default":false,"level":"advanced","apply":"next-start","help":"Smoother see-through edges with anti-aliasing on."},
  {"id":"display.scene-textures","kind":"toggle","label":"Replacement textures","default":false,"level":"advanced","apply":"next-start","help":"Loads a texture pack from the textures folder."},
  {"id":"display.ui-textures","kind":"toggle","label":"Replacement menu textures","default":false,"level":"advanced","apply":"next-start","help":"Loads menu textures from the textures folder."}
 ],
 "telemetry":[
  {"id":"telemetry.enabled","kind":"toggle","label":"Telemetry","default":true,"help":"Sends car data to SimHub and dashboards on this PC."},
  {"id":"telemetry.rpm","kind":"choice","label":"Engine speed","choices":["From speed and gear","From the tachometer"],"default":"From speed and gear","level":"advanced","apply":"next-start","help":"Where the engine speed sent to dashboards comes from."}
 ],
 "help":[
  {"id":"help.log","kind":"status","label":"Log","text":"runtime.log beside the game"},
  {"id":"help.runtime","kind":"status","label":"Wheelkit runtime","text":"not loaded"}
 ]}}})json"
