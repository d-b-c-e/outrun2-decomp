// The F6 pages (wheelkit_runtime.cpp hands them to the runtime): the runtime's descriptor format
// (wheelkit runtime/native/panel/dbce_panel_descriptor.h). Rows the game handles are listed in wheelkit_runtime.cpp's
// table with the OutRunDesktop.ini key each one saves; ffb.enabled and ffb.steering are the runtime's own. No row for
// the screens or telemetry (the rig profile says them), the graphics preset (Wheelkit's app option) or the texture packs.
R"json({"settings":{
 "product":"OutRun 2006 Coast 2 Coast","version":"0.9.1-alpha","modPage":"https://github.com/d-b-c-e/outrun2-decomp",
 "keys":"F6 settings","pages":{
 "controls":[
  {"id":"controls.profile","kind":"status","label":"Wheel profile","text":"No profile from Wheelkit"},
  {"id":"controls.sensitivity","kind":"percent","label":"Steering sensitivity","min":25,"max":300,"default":100,"apply":"next-start","help":"How far the cars steer for a turn of the wheel."},
  {"id":"controls.deadzone","kind":"percent","label":"Steering dead zone","min":0,"max":90,"default":0,"apply":"next-start","help":"Wheel travel around the center that does not steer."},
  {"id":"controls.manual","kind":"toggle","label":"Start on Manual","default":false,"apply":"next-start","help":"Preselects Manual in the game's transmission menu."},
  {"id":"controls.horn","kind":"toggle","label":"Horn","default":true,"level":"advanced","apply":"next-start","help":"The horn button sounds the horn in the race."}
 ],
 "ffb":[
  {"id":"ffb.status","kind":"status","label":"Force feedback","text":"Off"},
  {"id":"ffb.enabled","kind":"toggle","label":"Force feedback","default":true,"help":"The front tires' grip, felt in the wheel. F8 stops it."},
  {"id":"ffb.steering","kind":"percent","label":"Steering strength","default":50,"help":"How strongly the wheel pulls."}
 ],
 "display":[
  {"id":"display.latency","kind":"choice","label":"Frame latency","choices":["0 (plain)","1","2","3"],"default":"2","apply":"next-start","help":"Frames that may wait for the display; 2 is the default."},
  {"id":"display.ui-scaling","kind":"choice","label":"UI scaling","choices":["Stretch 2D","Wide HUD (partial)","Centered 4:3"],"default":"Centered 4:3","apply":"next-start","help":"How menus and the HUD fit a wide screen."},
  {"id":"display.letterbox","kind":"choice","label":"Black bars","choices":["Off","Outside the race","Always"],"default":"Outside the race","apply":"next-start","help":"Black bars beside the centered 4:3 picture."},
  {"id":"display.borderless","kind":"toggle","label":"Borderless window","default":true,"level":"advanced","apply":"next-start","help":"Off opens a 1280x720 window instead."},
  {"id":"display.cursor","kind":"toggle","label":"Hide the mouse cursor","default":true,"level":"advanced","apply":"next-start","help":"Hides the Windows cursor over the game."},
  {"id":"display.culling","kind":"toggle","label":"Draw the whole stage","default":false,"level":"advanced","apply":"next-start","help":"No pop-in; more drawing work."}
 ],
 "help":[
  {"id":"help.log","kind":"status","label":"Log","text":"runtime.log beside the game"},
  {"id":"help.runtime","kind":"status","label":"Wheelkit runtime","text":"not loaded"}
 ]}}})json"
