# Direct wheel camera actions

Eleven optional Digital `[Controls]` actions reach the same pose/FOV/reset consumer as the keyboard and F6 commands. They have no default physical binding. Original canonical indexes0..30 remain unchanged; the extension appends31..41.

| ID | Index | Result |
| --- | --- | --- |
| cameraForward / cameraBack |31 /32| Forward/back by MoveStepMetres |
| cameraUp / cameraDown |33 /34| Up/down by MoveStepMetres |
| cameraLeft / cameraRight |35 /36| Side offset by MoveStepMetres |
| cameraTiltDown / cameraTiltUp |37 /38| Pitch by TiltStepDegrees |
| cameraZoomIn / cameraZoomOut |39 /40| Narrow/widen by FovStepDegrees (STD-048) |
| cameraReset |41| Reset the current camera/set FOV offset and current car's pose offsets |

Only the active logical camera, saved Game/Realistic set and current car are edited. These are real owned settings writes, with the existing atomic writer, error reporting and unrelated-byte preservation. They end a session screen comparison before editing. Loading or idle/unbound controls writes nothing.

Both ordinary input backends use their existing samples. Native reads the already-qualified binding/device sample; Shim consumes that tick's resolved Frame and requires each action to be Live. Camera processing adds no separate reader, duplicate handle, extra device poll or OS keyboard injection; explicitly bound devices use existing backend ownership. Each action waits for its own release after startup, panel/focus blocking or device loss. A short tap acts on release. Hold-repeat starts after350ms and repeats every100ms for pose/FOV, without an extra release increment; Reset never repeats. A lost/unavailable sample is blocked, not a synthetic release. Held shifter gates do not block camera actions.

Camera actions remain disabled in pause/frontend, F6 settings, recording, playback and latched runs under the existing camera ownership contract. The original driving/menu/gear actions and64-bit edge meanings are preserved. The Shift+F7 comparison is session-only; direct camera adjustments use the existing saved tuning behavior.

## Source pins and compatibility

The existing sync_shim.py mirror route advances native/shim from d26776a785028ec868ee9f942836257ca78db4b4 to reviewed toolkit02c2547461832ed64096045730f52603cc5713f3. The intervening canonical source adds optional extras/snapshot/candidate DLL paths and their tests/docs. They retain their existing opt-in/defaults-off behavior; the game adds no declaration or new backend. Six game-owned WheelFfb bridge/control-publish/observer files were hash-verified unchanged. No WheelFfb DLL, managed/Unity library, owner install or runtime pin is replaced.

Core has42 actions; observer writes3 and reads2. Old31-action recordings get a neutral camera tail only when the entire extension is absent; partial extensions refuse. Source device tables/recording sample bounds rise to42, while each observer payload still allows8 sampled devices. The DLL facade stays ABI2/31 actions/296 bytes and old capture schema. Shared camera bindings are skipped/reported unsupported there and cannot open camera-only devices or break ordinary wheel input. That facade was reviewed by lili before merge; its full recorded-frame296-byte equality fixture and x64/x86 fake-COM tests pass in the toolkit.

Native qualification logs are under work/validation-20261102/direct-camera-*. Required checks cover the actual command/writer, provider->resolved Frame->camera FOV while H-pattern gear/steering stay held, Core/adapter/controls declarations, native legacy Tape playback, consumed playback, ABI2, exact66-file vendor pin and a Desktop compile. These are offline checks. Physical bindings, visible tuning/save behavior and owner acceptance require a later admitted rig run; this work does not install or launch a game.


## Offline results

Named Release Desktop and eleven focused test/consumer targets build. The initial focused batch passed11/12; the new focus-clear fixture required the keyboard reader's own neutral epoch/released ticks before its independent old keyboard assertions. After that fixture-only correction, camera241 inert checks pass; retail read-only model/profile camera271 pass. The first retail retry had a mistyped root argument; the corrected call derives the root from the verified OR2_EXE cache parent. All failed logs/fixture state are retained; no production code changed for either correction.

Other passing focused evidence remains: actual provider/resolved-frame/native manual-transmission camera+held-gear/steering46 checks; canonical camera contract830; Core192; adapter1479; legacy/new Tape536; capture@2 243; consumed replay27; ABI2 fake-COM1528; desktop controls233; controls consumer21; exact66-file vendor pin. Native DirectInput runtime execution is not covered; its existing sample consumer is compiled. No game/GPU/backend/window/real-device initialization or owner install occurred.

CPU GO495086 released11:33:59, fixture-only495125 released11:36:27 and retail-only495141 released immediately after its result. Jobs2/BelowNormal parent; exact tokens/timestamps/logs are direct-camera-* in local validation. The default-off curve/comparison GPU and physical gates remain unchanged.
