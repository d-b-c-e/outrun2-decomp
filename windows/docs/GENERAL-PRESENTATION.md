# Experimental general presentation rate

The next-start native F6 Cameras Advanced controls are **Experimental
presentation rate** and **Presentation cap**. Both use the existing one-key
writer and failed-save rollback. Defaults preserve the existing 60/120 path.

```ini
[Performance]
FramerateUnlockExperimental = false
FramerateLimit = 0
```

When enabled in ordinary play, FramerateLimit accepts 0 (no CPU cap) or an integer
from 60 through 1000, including values not listed in F6. Invalid values refuse the
experimental mode and log the ordinary-policy fallback. F6 offers common caps
and retains a saved custom value until an explicit replacement. Tests, recording
and playback ignore the experimental option. Native F6 saving/rendering remains
unqualified; source exposure is backed by the implemented consumers below.

Off preserves the current Options frame-rate behavior and integer-vblank pacing,
including its existing limitations on displays not divisible by the chosen rate.
On overrides the launch presentation rate with Performance.FramerateLimit and
removes the misleading live FRAME RATE row. Other video options still work;
their writer preserves the owner's saved ordinary options.ini framerate rather
than overwriting it with the experimental cap. Turning the option off restores
the normal saved rate on the next start. No owner config is migrated.

By default, experimental presentation uses Present interval 0 on its existing swap chain. A nonzero cap
uses nanosecond deadlines from the existing steady clock and platform_sleep_ns,
with fractional remainders retained; late frames rebase instead of generating
catch-up presentation bursts. This is a CPU pacing target, not a guarantee of
exact frame delivery or scanout. GPU, compositor and the existing frame-latency
wait can still throttle it. There is no new clock, timer thread, profiler,
tearing flag, refresh switch, VRR implementation or display change. The separate
accurate limiter described below deliberately uses a processor-pause wait.
Loading-only fast load skips this cap and resets its deadline; ordinary pacing
resumes when the existing loading-state gate ends.

Simulation and interpolation reuse 417890/417C7B and the existing display builder
replays. Experimental mode permits zero-update displays at any cap, including 0;
slow frames can run multiple 60 Hz updates. Input, events and mode control remain
inside that existing update loop. No new simulation scheduler or input channel
is introduced. Caps below 60 are not offered by this experimental consumer.

The audio ownership gap is resolved for this opt-in path: 417C7B already emits
42F330 after 453BB0 inside each actual update. The real host now services PcSound
at that callback under its existing recursive audio mutex, with no presenter
dependency, and skips its old per-render service when enabled. A zero-tick
display therefore services it zero times and a multi-tick display services it
once per update. PcSound's original every-four-calls control/fade cadence now
belongs to simulation, not rendering. PCM mixing, movie audio, elapsed-time
fades and their existing sample clocks remain unchanged. Off retains the old
per-render sound cadence for compatibility with the accepted 60/120 candidate.
This does not establish audible speed, smoothness or stutter attribution.

The cached ClangCL Release desktop and both fixture targets compile at
203bcb8. The real host/PcSound fixture passes 244 checks; the general-presentation
fixture initially faulted because it destroyed a context still referenced by
the real process-wide network service. Fixture-only b62d5d3 preserves one host
context across its timelines; its focused continuation passes 5,309 checks
(one CTest, zero skipped). Both unique affected fixtures now pass, totaling
5,553 checks; the desktop and passing sound fixture were not repeated.
The first fixture uses the actual
native loop with synthetic clocks and service callbacks at different render
rates, including slow/multi-update and uncapped cases, along with policy,
deadline and options-preservation checks. The second uses the real host binding
and PcSound with in-memory silent PCM and fake presenter/output to distinguish
fixed-tick from per-render service, prove no double service and retain PCM
submission behavior. Original build/failure and cleanup evidence is retained in
hub artifacts/outrun-general-presentation-20261017/build-01; build-02 contains the
passing one-fixture continuation. Both batches closed their children, restored
their process environment and released the exact lease. The retained desktop
is 23,970,816 bytes, SHA-256
`9E853B202B25F47AA22C1A01CB0F2CF3DEFE4F52909A525A956D4EDA5FE9F3FA`.
No device, game assets, window or mixer worker is started.
Prior monitor 32, calibration 175, level-select 861, cache 351, extraction 782 and
CD/course results remain retained. Rendered F6 saving, actual pacing/scanout,
sound/input behavior and driving are later runtime qualifications.

The existing flip-model presentation semantics and remaining compositor limits
are described by [Microsoft's Present API](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present).

## Limiter mode and independent VSync successor

The separate successor on qualified `0551df8` adds two next-start ordinary F6
choices under Cameras Advanced: **Limiter mode** and **VSync**.

```ini
[Performance]
FramerateLimitMode = 0
; Empty or absent retains existing pacing. Explicit values: 0 off, 1 on, 2 half refresh.
VSync =
```

Limiter values match the [Tweaks contract](https://github.com/emoose/OutRun2006Tweaks/blob/master/src/hooks_framerate.cpp):
0 selects efficient sleep/yield waiting, while 1 selects accurate waiting with
processor pauses until the same deadline. The implementation reuses the existing
steady clock and host sleep. Efficient mode sleeps most of a wait longer than
2 ms, leaving 1 ms, and yields for short remainders; it does not introduce Tweaks'
high-resolution timer or process timer-resolution changes. Accurate mode can
occupy a CPU core while waiting. Neither setting promises accurate delivered frames.

VSync is independent of the CPU cap and requests DXGI Present intervals 0, 1 or 2.
The extra empty/absent default preserves this port's ordinary 60/120 integer-vblank
pacing, or interval 0 when general presentation is enabled. F6's **Existing pacing**
choice saves an empty value through the existing writer. With general presentation
off, an explicit VSync override keeps a CPU cap at the ordinary selected 60/120 rate;
it does not unlock simulation. With general presentation on, its existing cap applies,
including 0 for no CPU cap. The existing frame-latency wait and compositor remain.
This does not enable tearing, change monitor refresh or establish VRR/scanout behavior.

Strict parsing accepts only mode 0/1 and VSync empty/0/1/2. An invalid key logs and
uses its own default, preserving the other valid key. Tests, recording and playback
ignore both controls, as they ignore general presentation. Loading-only fast load
still skips/reset the CPU pacer and temporarily requests interval 0 through its
existing gate. Fixed-tick simulation and sound ownership are unchanged.

Pending qualification: affected cached Release desktop build and only the new
`presentation_controls` CTest in the existing target. It uses synthetic clocks,
sleep and pause callbacks to exercise the actual wait loop, mode/sync policy,
invalid/default values, exclusions, uncapped behavior and late-frame/reset behavior.
The qualified desktop, general-presentation 5,309 and sound 244 checks at `0551df8`
remain retained. Rendered F6 save, physical CPU/pacing and output behavior remain open.
