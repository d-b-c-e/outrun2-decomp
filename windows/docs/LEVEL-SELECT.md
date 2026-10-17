# Optional level selection

Native source with an ordinary F6 next-start control; focused qualification of this successor is pending.
This does not change an installed game.

```ini
[Misc]
EnableLevelSelect = false
```

This defaults to false, as in the local Tweaks reference. When enabled, the
first frontend opens the existing PC level menu (key 27) instead of key 0,
matching Tweaks' patch at 445756. The return-from-race history path is separate
and stays unchanged. The original translated factory, widgets and renderer
remain in use; this adds no synthetic menu or course list.

F6 Controls Advanced offers **Start with level selection** through the existing
one-key writer and failed-save rollback. It reads/writes the same Misc key;
the current menu/input route stays unchanged until the next game start. Help
describes the 66-course menu, eligible held Shift/native X/left-stick alternates,
the panel's Shift block, and preserved return history/license progress. A latched
process still refuses saves, and the playback panel remains unavailable. No new
input action, menu, protocol channel or runtime publisher is introduced here.

The screen's 4E7800 control has a small native implementation while enabled:

- The 66 authored rows retain the PC's two-column course ordering, previous/
  next wrapping, back result, and transition to key 28 on confirmation.
- Native SwitchNow bits 0x10 and 0x100 select alternate courses only where the
  original two classification tables permit them. The 0x100 addition is local
  to this screen, as Tweaks' LS alias; it uses the existing final native switch
  record and introduces no global key polling or input binding.
- Either Shift key delivered to the game's own window is an additional held
  modifier. The existing normalized DIK snapshot supplies it; focus loss clears
  that snapshot and the open F6 panel blocks it. The publisher is installed only
  with this option on, in ordinary play (including Play with Recording).
- Confirmation updates the network module's course words and the native live
  course state together, then calls the original owner transition with argument
  3. The classification bytes come from the prepared owner EXE image.
- With the setting off, the first menu remains key 0 and 4E7800 continues through
  its original translation. No generated code, save, course file, camera,
  simulation clock, renderer, accepted settings or installed file is edited.

Shift stays separate from native X/LS, PC buttons, frontend features, pad bits
and keyboard provenance. `NativeInputState.level_select_shift` reaches 4E7800
after the existing final-input/replay arbitration; no switch bits are rewritten.
Ordinary @2 recordings add `final.levelSelectShift` with a strict 0/1 value.
Consumed replay restores it from the recorded row; older rows without it mean
released. @1 captures remain unchanged. Test/playback processes never install
the live keyboard publisher, and refused/finished consumed replay clears it with
the existing neutral input. Existing @2 values retain their meanings; new @2
captures include this additional channel even when it is zero.

The local Tweaks reference (`dbce-mods-outrun-2006/src/hooks_misc.cpp:1011-1044`)
ORs LS and unmasked `GetAsyncKeyState(VK_SHIFT)` into the X result. This port uses
window-delivered held state, so it deliberately does not reproduce global
background input or the asynchronous API's recent-press bit. No full parity,
menu visibility, route completion, rendering, physical input or recording/playback
runtime result is claimed.

The synthetic `test_level_select` fixture exercises every authored row's
independently enumerated course mapping, alternate classification limits and
both tables, callback order, native X/added LS masks, release, navigation/wrap,
back/idle, row mutation during inherited input, signed extremes, short storage
and incomplete services. It is C++17 with /W4 /WX on MSVC-compatible targets.
The separate `level_select_input` fixture uses the actual desktop publisher with
synthetic backend/panel owners: both Shift keys, blocked/focus-cleared state,
disabled/test/playback policy, no duplicate wrappers and original input fields.
`outrun_capture2` covers optional-channel compatibility and strict values;
`consumed_replay` covers recorded modifiers and neutral end-of-tape input. These
source additions still need their focused fixture qualification. The cumulative native,
desktop, capture and replay targets must be compiled together against the changed
input header; do not link old prebuilt input/context objects into this successor.
Use focused affected checks in a named build slot, not a replay of unrelated
successful suites. A later useful runtime check
should include the title spacing from 54a853f3; no isolated title replay is
needed. Existing title timing/placement and audio results remain historical.
