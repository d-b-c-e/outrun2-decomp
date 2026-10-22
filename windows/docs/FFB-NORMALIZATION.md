# Producer2 comparison with the fixed Art v2 reference

This is a reproducible **software-only comparison packet**, not normalized or
physical acceptance. Default 50 alone does not establish a match. Production
`outrun-producer@2` and its vendored shared model are exercised by the existing
`test_ffb_producer2` fixture. No legacy Tweaks golden or second force model is
used for this comparison. Production tuning, polarity and delivery are unchanged.

## Identity and settings

The starting checkout was `4099259d07ab1704cf1e484520ce0edc7e68f769`.
Each packet's `provenance.json` records its exact candidate commit, fixture
executable hash, raw request hash, observed toolkit commit and model bytes;
`manifest.json` also binds the fixture source and production adapter. Source
completion is separate from runtime/device/attended acceptance.

The vendored `windows/vendor/native/forcemodel/force_model.h` comes from toolkit
`fb710dffa29f707d296f1f579851e23d549a8b51`, SHA-256
`c7f3c3ff9c6d1475b88015643afb1b515d3f6f97bcc46e1fe2082f26ded1b16f`.
The local shared analyzer and its replay helper are hash-pinned in
`windows/tests/data/normalization/qualify.py`; the toolkit checkout is read only.

The byte-preserved `art-of-rally-50-v2.json` in that directory has SHA-256
`54fcf9db577b559bdb26638b370f38958318f4a414dde6043e96aa8a22b23433`.
It pins Art `8bc94b1f54cfb490fbbb37191727cdf7eee0edf8`, model
`0.4.2-reference-v2`, `AxleForceCurve@1`, reference **19166.666015625 N**,
`gainAt50 = 1`, smoothing 0.2, invert false. The profile retains the historical
11,921-row new50/legacy30 qualification and its original source hashes. The
historical v1 profile is copied separately, unchanged, rather than overwritten
or used as the current reference. Original toolkit evidence remains untouched.

Producer2 uses provisional centering coefficient **0.30 at full speed 30 m/s
(108 km/h)** and provisional shift amplitude **0.05**, each at its own 50 gain.
The gains scale independently by percent/50; neither is a global legacy gain.
The shift model uses 0.04 s positive half duration, followed by a balancing
negative pulse; discrete tick counts depend on frame rate. Shaper strength 50
is unity, invert false, unit peak limit; deadzone, output deadband, smoothing,
soft saturation, slew, speed fade, ramp and additional shaping are off. Speed
fading is already in the centering model. Unmeasured model inputs remain absent.

The single nominal DirectInput conversion is exactly the selected-request
boundary in `ffb_live.cpp`: `lround(clamp(shaped_force,-1,1)*10000)`.
The export records that integer alongside the original model and shaped values.
Its focused checks cover zero, signs, clamping and half-away-from-zero rounding.
It does not apply a second gain, infer wheel torque, or claim delivery. Device
product direction and existing delivery/neutral gates happen downstream.

## Workload and comparison limits

The fixture writes signed consumed steering through production
`operation_input_49fad0` at -127/-64/0/64/127. Existing input-axis checks continue
to exercise the actual analog producer. A shift is a real production
`manual_transmission` change from gear 2 to 3, followed by steady gear. Each case
primes its own production stream before export. There are 100 synthetic cases:
0/12/40/108/180 km/h, five steering values, steady/shift, 60/120 Hz. Every case
runs all nine independent steering/shift combinations of 0/50/100, for 0.5 s
plus a terminal sample. That is 41,400 exported Computed rows. It is an evenly
weighted software grid, not an observed distribution of driving workloads.

The shared analyzer summarizes 18 series (nine gain pairs, steady and shift
holds separately) as **constant-force requests** with four speed bands,
duration-weighted RMS/percentiles/peaks, saturation and temporal metrics.
Case epochs break continuity; the last sample has no assumed hold duration.
Synthetic case-boundary sign changes are not corner-response measurements.
The 108 and 180 km/h cases separately assert the model plateau even though
both fall into the analyzer's 60+ band. The combined shift cases check peak
headroom; the zero-steering cases isolate the supported shift cue. There is no
second Art steering time series in this packet, so matched Art workload/RMS,
corner onset, and force-response ratios are **unavailable**.

| Quantity | Producer2 | Fixed Art v2 | Comparison boundary |
|---|---|---|---|
| Steering | Signed speed-scaled centering estimate | Tyre/axle-force curve, reference in N | Comparable software request range only; no conversion of centering into tyre force |
| Shift | Balanced constant-request double pulse, nominal +/-0.05 at gain 50 | Optional shift defaults off; synthetic enabled intensity 1 requests 25 Hz sine amplitude 0.05 for 120 ms | Same numerical peak is not equal waveform, duration or torque |
| Crash | Unavailable production signal | Optional, default off; synthetic constant pulse plus periodic rattle in profile | No OutRun counterpart/severity; unavailable |
| Landing | Unavailable | Enabled by default, periodic burst in profile | No OutRun counterpart; unavailable |
| Load/slip/damping/texture | Inputs unavailable; no invented force contributions | Art tyre input is a distinct quantity | Corresponding measured workload unavailable |

Art's synthetic intensity sweep is preserved inside its profile, including
periodic frequency, fade and duration. Periodic amplitudes are never fed into
the constant-force analyzer or added to its centering output. Art effect
strength 5 and Producer2 gain 50 are different controls. Art's crash/landing/
shift gains are independent of its steering Strength; this packet tests
Producer2's two supported gain controls only. Existing unrelated synthetic
impact checks in the fixture are regression checks, not Producer2 crash evidence.

## Reproduce offline

Use an x64 VS 2022 developer command prompt with the installed ClangCL, CMake
and Ninja. From the repository root, configure a fresh ignored build directory
using the existing project recipe and already installed dependencies:

```bat
cmake -S . -B build-ffb-normalization/ninja -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe" -DOR2_XBOX=ON -DZLIB_INCLUDE_DIR=E:/Source/research/outrun2-decomp/build-deps/zlib/include -DZLIB_LIBRARY=E:/Source/research/outrun2-decomp/build-deps/zlib/lib/zlibstatic.lib -DOR2_XBOX_FFMPEG_ROOT=E:/Source/research/outrun2-decomp/build-deps/ffmpeg -DOR2_XBOX_VORBIS_ROOT=E:/Source/research/outrun2-decomp/build-deps/vorbis
cmake --build build-ffb-normalization/ninja --target test_ffb_producer2 --parallel 2
python -B windows/tests/data/normalization/qualify.py --fixture build-ffb-normalization/ninja/windows/test_ffb_producer2.exe --output work/ffb-normalization/run-01
```

Build only the named fixture target. It links the existing driving library and
production Producer2/input sources; it builds no game or DLL and calls no device.
The adapter refuses an existing output directory or changed pinned toolkit/
reference bytes. Choose a new run directory for a justified rerun. It invokes
the fixture once, retains its original stdout/stderr and raw rows, splits those
rows without force conversion, invokes the existing shared analyzer once, and
asserts its 18 reports cover all four speed bands without unit saturation.
Outputs stay in the declared ignored `work/ffb-normalization` and
`build-ffb-normalization` directories. No packages, network, owner config or
global settings are needed. `build.cmd` and original configure/build diagnostics
for this attempt are retained under the work directory, including unsuccessful
VS compiler discovery and missing-local-ZLIB attempts.

## Attended check still required

Use the exact installed candidate/toolkit/settings tuple and a controlled,
owner-attended route. Confirm real device polarity, centering onset and effort
across speeds, neutral/zero-steer response, supported gear-change feel, and
headroom during combined steering and shifts. Compare with the fixed Art v2
settings on comparable maneuvers rather than whole-drive RMS. Establish actual
delivery, pause/F6/focus/recovery neutral/re-prime, emergency stop and release.
Decide whether provisional 0.30/0.05 need tuning only from that attended evidence.
Tyre-force/load/slip, impact severity and landing counterparts remain unavailable;
the software grid cannot close those gaps or prove physical equality.
