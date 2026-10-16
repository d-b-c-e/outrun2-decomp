# Level Select Shift source successor

Owner source fallback reopened through sana 168751; Kiki handoff 168940.
This isolated successor preserves cumulative 319f5ec758433ec335dce25bfdd1ab7ae26beb41.
Kiki's separate RandomHighwayAnimSets slice is not included by implication.

## Concrete gap implemented

Either Shift key held in the game's window now requests the enabled level
menu's alternate course, using its existing classification and commit path.
The existing LS/X paths remain intact. No menu, renderer or geometry substitute
was introduced. `[Misc] EnableLevelSelect` remains false by default.

The publisher reads the existing normalized keyboard snapshot on the game
thread after the backend filter, without an OS/global keyboard poll. F6's input
owner remains outermost and neutralizes the full sample. Focus loss already
clears both Shift keys through the window's keyboard owner.

The modifier is a separate `NativeInputState` boolean, not a fabricated LS or
X switch. Only the opted-in 4E7800 adapter consumes it. Original PC button,
frontend feature, pad, axis, horn and keyboard-provenance values are unchanged.
Ordinary recording adds `final.levelSelectShift` to @2 final channels; old rows
default released, new values must be exactly 0 or 1, and @1 remains unchanged.
The actual consumed replay copies this value after live-input arbitration and
clears it at refusal/end. A playback/test process never installs the live
keyboard publisher. No raw keyboard state can override an admitted replay row.

The local Tweaks hook uses unmasked global `GetAsyncKeyState(VK_SHIFT)`; this
port intentionally supports only a held key delivered to its own window.
Global background input/recent-press semantics and full parity are not claimed.

## Verification status

Source review and `git diff --check` only. No compiler, fixture executable,
game, heavy batch, video analysis or installer ran during Forza's lease.

Focused fixtures authored, not executed:

1. `level_select`: Shift uses the independently enumerated 66-row course map,
   both classification gates, release and default-off policy; no switch mutation.
2. `level_select_input`: actual publisher with synthetic backend/panel owners,
   both Shift keys, held-bit interpretation, focus clear, disabled/test/playback,
   panel block, one backend call and no duplicate wrapper recursion.
3. `outrun_capture2`: strict optional modifier, old rows, @1 unchanged and every
   original @2 channel unchanged between held/released samples.
4. `consumed_replay`: modifier round-trip over all synthetic tape rows, old row
   compatibility and clearing stale modifiers at refusal/end.

A later named slot should compile these affected targets and their cumulative
dependencies, plus the changed desktop/network production units. The shared
input header changed: a later cumulative desktop link must use coherent new
objects, never old input/context objects. Do not repeat unrelated accepted music,
audio, driving or physical qualification. Base Level Select and title-spacing
runtime qualification remain pending; combine them in the next useful run.

No owner install or public push. Original branch/worktree, receipts and binaries
remain intact. Shared `pc_screen.hpp` is untouched; ROADMAP edits only the
EnableLevelSelect row, leaving Kiki's highway rows independent.
