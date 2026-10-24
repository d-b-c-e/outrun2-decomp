# Optional loading-only fast load

The source is based on cumulative `c5d3a9f` and remains default **off**. The existing host policy and INI parsing checks passed on product `2984189` (361 and 44 checks); sources are unchanged and those results are retained. Runtime behavior and loading speedup remain unqualified.

F6 Advanced Cameras now offers **Fast loading** as **Off (default)**, **Loading only (experimental)** or (October 17, see Mode 1) **Loading only, VSync kept (experimental)**. The matching Wheelkit @6 Cameras exposure is owned by Lola. Both choices save through the existing one-key writer for the next start; selecting Loading only writes:

```ini
[Performance]
FramerateFastLoad = 2
```

Absent or `0` keeps normal behavior. `0`, `1` and `2` are accepted. Other values, including Tweaks mode `3`, are logged and turn the option off. The existing INI reader strips surrounding whitespace and comments. No settings file is written by this feature. The startup snapshot/playback merger already retains `[Performance]` (only `[Window]` and `[Triple]` come from the current rig); no new snapshot format is introduced.

## Mode 2 behavior

This follows the loading-only intent of pinned OutRun2006Tweaks `51acc201`: bypass loading presentation waits and run one update per loading frame. It uses the existing native frame loop, clock, display interpolation hooks, graphics frame-latency wait, and swap-chain Present. It adds no timer, generic limiter, second frame loop, repeated file-service call, graphics device reset, or `FramerateUnlockExperimental` option.

- Loading means native START mode **13**, progress **less than 65**. `NativeStartModeState.stage` maps PC `0x8367A8`. Tweaks tests `!=65`; the native START stages advance through 64 to 65 and use `<65` for their loading picture.
- At the beginning of each frame the native loop snapshots whether it is a loading frame. Such a frame takes the existing cooperative single-update path at both configured 60 and 120 Hz. Loading is drawn at its current tick pose, without interpolating accelerated ticks.
- The existing pre-input frame-latency wait is skipped only for an accelerated frame whose **live** mode/progress is still loading. Present uses interval `0` under that same condition. The configured ordinary present interval is retained, including changes made through the existing video settings path.
- If the final loading tick reaches stage 65 or leaves START, the live gate restores normal Present **within that frame**. The next input sample uses the ordinary latency wait. Ordinary pacing resumes at stage 65 while still in START, which already owns the race camera/countdown.
- That final loading tick completes once, including its existing mode/event ordering. Its interpolation replay history is cleared before ordinary drawing resumes. After restored Present and the original small PC guard, the existing clock is sampled and its accumulator rebased once. Loading time does not become a backlog of gameplay updates; subsequent 60/120 Hz timing uses the existing policy.
- A scoped binding releases the native context pointers and override when its loop exits, including an early exit. Setup screens and later teardown presentation remain paced normally.

The original logical `44FBA0` file-service call stays in its original once-per-frame location. Tweaks mode 3's in-game streaming behavior is outside this feature; the [mode-3 source boundary](FAST-LOAD-MODE3.md) records the additional loader and frame-wait work it requires.

## Mode 1 behavior (source only, October 17)

Tweaks mode 1 skips only its frame limiter on load screens and leaves the configured presentation interval alone, so loading runs at the display's refresh with VSync. Here mode 1 uses the same loading gate, one-update path, interpolation handling, clock rebase and scoped binding as mode 2, with two differences while a frame is accelerated:

- The pre-input frame-latency wait is kept.
- Present uses interval `1` instead of the configured interval, or `0` when that interval is already `0` (VSync off). A 60 Hz display therefore gains nothing; a 120 or 180 Hz display loads at its refresh without tearing.

The CPU deadline pacing of the presentation controls is skipped for accelerated frames in both modes, as before. F6 Advanced Cameras adds **Loading only, VSync kept (experimental)**, which saves `FramerateFastLoad = 1`; the existing two choices keep their saved values. The matching Wheelkit row still lists only `0` and `2`.

Checks added in source: `fast_load` gains a mode-1 policy block (gate, intervals 0/1/3, kept wait, one update, scoped release), the production-loop fixture at 60/120 Hz for mode 1 (interval 1 and kept waits on loading frames) and mode-1 ordinary-pacing comparisons; `fast_load_config` accepts `1`. These are unbuilt and unrun.

## Focused checks and remaining gates

Retained passing host checks (product `2984189`; `artifacts/outrun-fast-load-prompts-cumulative-kiki-20261015/QUALIFIED-HOST.md` in the portfolio hub):

- Root CTest `fast_load` (**361 checks passed**): exact gate matrix; invalid/off configuration; stage-64 to 65 and mode-abort restoration; scoped cleanup; production native loop at 60/120 Hz with a synthetic existing clock; one loading update per frame; same-frame Present restoration; accumulator rebase after a simulated long final-loading Present; no replay of loading history on the first ordinary display-only frame; original file-service call count; enabled/disabled ordinary service order and timing comparisons for stage 65, gameplay, pause, and frontend modes.
- Desktop CTest `fast_load_config` (**44 checks passed**): production `Ini::load_text` parsing, missing key, modes 0/2, whitespace/comment/case handling, unsupported/malformed values, and preservation of unrelated Controls/Window values. It creates no settings file.

The F6/Wheelkit exposure passed its October 16 offline batch on native `d35ee48` and Wheelkit `c36aaeb`: incremental `OutRunDesktop` build, all four affected settings/catalog cases (one read/preservation fixture with 12 inputs, two explicit one-key/exact-backup writes, one catalog assertion; zero failed/skipped), and private @6 generation/parsing matching all 73 bundled OutRun rows. Existing 361/44 consumer checks were retained without rerunning.

Original logs, `result.json` and `test-results/affected.trx` are in portfolio hub `artifacts/outrun-fast-load-setting-build-20261016/build-01`. The private native app is 23,833,088 bytes, SHA-256 `B872B5742AA62478CC3B15092350F2074F48DB614922BAD84B9A7D08E18EC225`. The exact shared lease was released after owned-process cleanup and environment restoration, before documentation/integration. Lola owns Wheelkit docs/ordinary canonical integration; native stays private. No game, owner install, provider retry or public push occurred.

Rendered F6 controls and persistence remain unqualified. Graphics wait/Present execution, visible stage-65 transition, the 60/120 settings, loading abort/close, and ordinary gameplay pacing still require bounded runtime qualification. Source alone establishes no load-time improvement, VSync/scanout result, visual acceptance, input behavior, or physical feedback result. Existing frozen `55B9` packaging remains separate and unchanged.
