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

The desktop uses Present interval 0 on its existing swap chain. A nonzero cap
uses nanosecond deadlines from the existing steady clock and platform_sleep_ns,
with fractional remainders retained; late frames rebase instead of generating
catch-up presentation bursts. This is a CPU pacing target, not a guarantee of
exact frame delivery or scanout. GPU, compositor and the existing frame-latency
wait can still throttle it. There is no new clock, timer thread, profiler,
busy-wait, tearing flag, refresh switch, VRR implementation or display change.
Loading-only fast load skips this cap and resets its deadline; ordinary pacing
resumes when the existing loading-state gate ends.

Simulation and interpolation reuse 417890/417C7B and the existing display builder
replays. Experimental mode permits zero-update displays at any cap, including 0;
slow frames can run multiple 60 Hz updates. Input, events and mode control remain
inside that existing update loop. No new simulation scheduler or input channel
is introduced. This is a general presentation adaptation, not exact parity with
Tweaks' separate efficient/accurate limiter or VSync settings; caps below 60 are
not offered by this experimental consumer.

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
