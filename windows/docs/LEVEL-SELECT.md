# Optional level selection

Source-only, unbuilt and unrun. This does not change an installed game.

```ini
[Misc]
EnableLevelSelect = false
```

This defaults to false, as in the local Tweaks reference. When enabled, the
first frontend opens the existing PC level menu (key 27) instead of key 0,
matching Tweaks' patch at 445756. The return-from-race history path is separate
and stays unchanged. The original translated factory, widgets and renderer
remain in use; this adds no synthetic menu or course list.

The screen's 4E7800 control has a small native implementation while enabled:

- The 66 authored rows retain the PC's two-column course ordering, previous/
  next wrapping, back result, and transition to key 28 on confirmation.
- Native SwitchNow bits 0x10 and 0x100 select alternate courses only where the
  original two classification tables permit them. The 0x100 addition is local
  to this screen, as Tweaks' LS alias; it uses the existing final native switch
  record and introduces no global key polling or input binding.
- Confirmation updates the network module's course words and the native live
  course state together, then calls the original owner transition with argument
  3. The classification bytes come from the prepared owner EXE image.
- With the setting off, the first menu remains key 0 and 4E7800 continues through
  its original translation. No generated code, save, course file, camera,
  simulation clock, renderer, accepted settings or installed file is edited.

Tweaks also accepts an asynchronous Windows Shift key for the alternate course.
That alias is not implemented here. No full parity, menu visibility, route
completion, rendering, physical input or recording/playback result is claimed.

The synthetic `test_level_select` fixture exercises every authored row's
independently enumerated course mapping, alternate classification limits and
both tables, callback order, native X/added LS masks, release, navigation/wrap,
back/idle, row mutation during inherited input, signed extremes, short storage
and incomplete services. It is C++17 with /W4 /WX on MSVC-compatible targets.
The fixture and three affected production units still need a separately
reviewed offline recipe and named build slot. A later useful runtime check
should include the title spacing from 54a853f3; no isolated title replay is
needed. Existing title timing/placement and audio results remain historical.
