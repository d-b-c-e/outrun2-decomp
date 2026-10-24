# Tweaks fast-load mode 3: source implementation boundary

Mode 3 is **not implemented** in the native port. The existing `0`, `1` and
`2` settings keep their current behavior. This note pins the source work
needed for full Tweaks parity, without treating the loading-only modes as a
substitute for it.

The [Tweaks source at `270cf935`](https://github.com/emoose/OutRun2006Tweaks/blob/270cf935697ea7d23e9d62abe0ab06054ab00a7c/src/hooks_framerate.cpp)
keeps its frame limiter active in mode 3. While there is time before the next
deadline, it calls `FileLoad_Ctrl` repeatedly until the loader reports idle or
the remaining time is at most 0.5 ms. Efficient sleeps are capped at 4 ms so
the loader gets another opportunity before the deadline. When the limiter is
disabled, it still calls the loader once. Tweaks removes the original per-frame
loader call and changes the last request server's always-true return to its
actual result; otherwise the pump could spin through the entire wait when no
files remain.

The native host now services one queued road-table request when the ordinary
`0x44FBA0` call arrives after Present. This is one prerequisite for Mode 3;
the robot-script and other native resource routes still read synchronously.
The translated `F_44fba0` exists but is not connected to the host call.

Tweaks' remaining integration differences need to be addressed together:

- `runtime_frame_step_417c7b` requests `0x44FBA0` once after Present
  (`src/driving/pc_common_control.cpp`). The CPU deadline currently runs
  inside D3D11 Present, before that call (`xbox/source/d3d11_display.cpp`).
  Pumping in the deadline therefore needs the current frame's loader service
  without racing or repeating an uncontrolled original call.
- The currently unconnected translated `F_44fba0` (`src/platform/bulk_tr.cpp`) ends its ADV request
  path with the unconditional `eax=1` at `0x45AED7`. Its result is not a
  truthful idle signal for a repeated pump. A native mode-3 path must retain
  all five request servers and return the actual final result, as Tweaks does;
  changing only the frame limiter would keep busy-looping on an idle loader.

The remaining routes need pending work and a truthful aggregate idle result.
Mode 3 then needs to keep loader calls on the game thread, preserve the
existing order of game updates and Present, stop at the frame deadline, and
restore the ordinary one-call path for modes `0`–`2`. The no-CPU-cap case still
needs one call per frame. Focused checks should cover busy-to-idle, always-idle,
the deadline and bounded sleep, the no-cap case, and unchanged `0`–`2` call
counts before the new setting is exposed in F6 and Wheelkit. A build and
in-game loading/streaming comparison are separate gates. Until then Mode 3
is not a meaningful setting for the native port.
