# Optional highway animation sets

Source-only, unbuilt and unrun. No installed game, setting or save changed.
Owner source assignment169003; isolated cumulative base319f5ec.

```ini
[Misc]
RandomHighwayAnimSets = false
```

The default remains false, matching both the code and shipped INI in the local
Tweaks reference51acc201 (`src/hooks_misc.cpp`, RandomHighwayAnimSets,
`OutRun2006Tweaks.ini`). Tweaks replaces EAX with a value from0 through3 at image
offset0x4D429 / VA44D429 in GetBranchRenditionType44D410.

## Mode replacement, conditions and table

The native area selector uses that same local mode substitution before its C2C
branch, condition tests and existing animation table. It does not write the real
race-mode word780258 or change input, physics, cameras, geometry or rendering.

- A forced rendition6368C4 returns first and consumes no feature randomness.
- An eligible call chooses exactly one mode from0..3. Original mode3/4 is also
  replaced when enabled; the option does not skip C2C races.
- Chosen mode3 takes the original course-classification calls455B80/455BA0 and
  existing C2C table path, including the original missing-course refusal.
- Chosen mode0/1/2 uses that mode's row and its original event-condition column.
  All current native conditions and service calls remain in the same function.
- With the option off, the original mode and conditions remain unchanged.

`RandomHighwayAnimationRows` remains a separate default-off option. With Sets
off it keeps its existing row-only/native-condition behavior and C2C bypass.
If both are enabled, Sets takes precedence; the Rows stream does not advance on
those calls. No existing persisted key is renamed or migrated.

## Deterministic process stream

The mode replacement point/range and downstream behavior match Tweaks. Its
per-call random_device/mt19937 entropy is deliberately not reproduced: it would
introduce new nondeterminism into the retained recording path. Instead, the
source uses an independent version1 xorshift32(13,17,5), initial state0x4F523253
once per process, and the top two bits after each advance. It consumes no game
CRT RNG or row-only stream and is never reset by area/menu/settings reloads.
No time, device, hardware entropy or unbounded search is introduced.

The existing cold-start playback verifies the recording's executable and sealed
OutRunDesktop.ini, and loads its recorded settings before reading this Misc key
(`ordinary_recording.cpp`, `window_desktop.cpp`). Equal cold initial state and
selector call sequence produce equal choices. No new recording schema, writer,
input-channel change or arbitrary midprocess state restore is added. This is
source reasoning plus authored fixtures, not an observed cold-replay equality
or animation/render result; call-sequence/runtime acceptance remains pending.

## Focused qualification source

`test_highway_anim_sets` calls production44D410 with synthetic mapped tables,
cached request records and scripted native services. Authored cases cover:

- Every existing event-condition family, including NaN/infinity comparisons,
  across original modes0..6 and an outlying mode, with either Rows setting.
- Mode3 entry from non-C2C modes and mode0/1/2 entry from original C2C modes;
  exact service order/arguments and missing-course results.
- Actual-mode/mapped-byte preservation, single mode draw, Rows precedence and
  independent streams; original Rows-only compatibility control.
- Forced override with mode/car/table deliberately unmapped.
- Literal versioned state/mode sequence across fresh area/menu contexts,
  interleaved disabled/Rows/forced calls, and equal cold-start sequences.

The new fixture is C++17 and requests /W4 /WX for MSVC-compatible builds. It is
**unbuilt and unrun**. Existing `test_highway_animation_rows` is also unchanged
and should run once as the focused default-off/Rows regression control when a
named slot is available. Affected production units are race_area.cpp and
window_desktop.cpp; preserve Level Select/title/Shift cumulative source and use
the existing qualified partial-build/closure route when preparing the eventual
combined recipe. No build, fixture execution, package, install or game slot is
granted by this document.
