# Mounted race camera adjustments

F6 > Cameras offers independent saved adjustments for the existing **Bumper**
and **In-car** views. The native Change view cycle remains chase -> in-car ->
bumper -> chase. No new camera family, chase camera or scripted view is added.

| Default key | Adjustment | Default step |
|---|---|---|
| Numpad 8 / 2 | Forward / back | 0.02 metres |
| Numpad 9 / 3 | Up / down | 0.02 metres |
| Numpad 4 / 6 | Left / right | 0.02 metres |
| Numpad 7 / 1 | Look down / up | 1 degree |
| Numpad + / - | Widen / narrow vertical FOV | 2 degrees |
| Numpad 5 | Reset active view pose and FOV | One press |

Each view retains its offsets and FOV across view changes and restart. Reset
affects only that view. Zero offsets use the stock pose and projection without
additional arithmetic. Movement follows the native view's horizontal heading;
up is world vertical. Tilt is pitch, with positive values looking up.

In Simple, expand **Adjustment bindings** to see the actual assignments and
Bind, Clear or Restore numpad defaults. A keyboard without a numpad can bind
F1-F5, F7, F9, F10, F12 or the punctuation positions `- = [ ] ; '`, grave,
backslash, comma, period and slash. Letters, digits, navigation, modifiers,
F6/F8/F11 and the HUD assignment are protected. Bind accepts one key after all
keys are released, commits on release, and rejects chords and conflicts. Cancel,
focus loss, leaving Cameras or the ten-second timeout retain the prior binding.
**Physical wheel/gamepad/button-box binding is unsupported by this camera
editor.** It does not add a device reader or redesign shared wheel input.

Advanced edits a selected view and the per-press steps. Pose reset changes only
that view, step defaults change only steps, and numpad defaults change only
camera bindings. All proposed batches validate before saving. Translation is
bounded to +/-2 m, pitch to +/-30 degrees, FOV offset to +/-60 degrees. A nonzero
FOV offset clamps the effective vertical FOV to 20..140 degrees; zero preserves
the native value. Move steps accept 0.001..0.2 m, tilt/FOV steps 0.1..10 degrees.
Held movement begins repeating after 0.3 seconds, at most once per action every
0.08 seconds. Stalls over 0.15 seconds require release; repeats never catch up.
Reset does not repeat.

## Native support and limitations

Eligibility comes from camera +34A == 0 (bumper) or 1 (in-car), with +33C/+340/
+344 and +364 transition state clear and +94/+98/+9C debug ownership clear.
The desktop host opts in only during ordinary GAME mode 0x10, unpaused and
outside AUTOSCENE ownership; the countdown/start path remains stock. The native
race path rebuilds eye/target from its existing tables every frame, then applies
the saved offset before the native angle, interpolation and projection path.
The FOV offset follows the existing native or physical-triple FOV baseline;
the off-center projection, aspect, near plane and triple renderer remain intact.
Other platforms leave the adjustment callback unset and retain stock behavior.

In-car uses the game's existing per-car eye table (818BB0). This feature does
not supply missing dashboard/cockpit artwork, new seat models or collision
avoidance: moving the eye can expose missing geometry or clip bodywork/ground.
Chase, attract/demo, goal, ending, time-over, countdown and game-owned camera
transitions are excluded. Other native race dispatch modes remain stock.

Shortcut changes/reset are suppressed during F6 (including binding/text input),
focus loss, pause, transitions, recording, playback and fenced tests. Capture,
focus and panel transitions require release before a new adjustment. The brief
closed-panel feedback shows the active view and values. Help points to the
actual bindings in Cameras. Camera-only keys do not take keyboard steering
custody from a wheel. Simulation inputs, tape format, physics and force state
are unchanged; this feature never resumes stopped FFB.

## Saving and offline evidence

Settings live in `[CameraAdjustment]` of the existing OutRunDesktop.ini:
`Bumper`/`InCar` + `Right`, `Up`, `Forward`, `Tilt`, `Fov`; `MoveStep`, `TiltStep`,
`FovStep`; and `Key0`..`Key10` in the action order above. Keys are DIK positions;
zero is unbound. Invalid saved values recover individually, retaining other
valid views and custom bindings. Conflicting/unsupported saved keys are unbound.

Bindings and explicit step batches use the existing atomic multi-key INI
writer. Other sections, calibration, comments, custom controls and unowned
camera keys are retained. Poses save after 0.75 seconds without edits or on
shutdown, rather than each frame. Failed saves preserve effective bindings and
retain a proposal under **Retry camera save / Cancel camera save**. Cancel
discards the proposal and restores saved camera values. No failed write
publishes a partial default/reset batch.

The ignored `build-camera-adjustment` directory retains the interrupted build
failure and original passing fixtures. The final Ninja Release build uses the
installed Clang19 toolchain and its real x64 compiler builtins via the existing
`OR2_CLANG_BUILTINS` option. Build only `OutRunDesktop` and
`test_camera_adjustment`; run:

```powershell
ctest --test-dir build-camera-adjustment/desktop --output-on-failure -R '^(camera_adjustment)$'
```

Focused camera coverage includes units/directions, independent views/reset,
finite bounds, repeat/stall/release behavior, lifecycle gates, capture timeout/
cancel/chords/conflicts, atomic default rollback, injected save failure with
Retry/Cancel, deferred saves, restart and unrelated settings preservation.
Synthetic native race tables exercise the production race path for zero/stock
equivalence, repeated-frame stability, chase exclusion, transition preservation,
native view cycle and projection/triple-FOV baseline behavior.

October 22 offline result: **OutRunDesktop Release build PASS; camera fixture
PASS (1132 checks)**. Evidence: `build-camera-adjustment/build-final.txt`,
`configure-final.txt`, `fixtures-final.txt` and
`desktop/Testing/Temporary/LastTest.log`. The inherited `fixtures-first.txt`
retains passing desktop_bindings and hud_toggle results; their production
modules are unchanged. Its camera_render_binding case was skipped because no
retail EXE image was configured, and is not claimed as passing. The interrupted
`build.txt`/`build-retry.txt` failures are retained alongside the successful
real-runtime links. No stub runtime library was used for the successful build.

**Runtime gates remain unverified:** rendered F6 layout and keyboard navigation,
actual Windows capture/hook ordering, Num Lock/layout behavior, focus/pause and
scripted transitions in a running game, physical keys/devices, per-car clipping,
and visible triple projection/interpolation. No game, device, input injection,
display change or installation was performed for these fixtures.
