# Experimental CD catalog (source slice)

Off unless `[CDSwitcher] SwitcherEnable = true`. Ordinary play only: test,
recording and playback processes keep it off. No settings or Controls schema
is written. The prior db91 source/candidate60C4 passed scoped default-off and
request/open/current-track runtime checks; the title overlay below is a new
source change, not compiled or rendered-qualified yet.

```ini
[CDSwitcher]
SwitcherEnable = true
TrackNext = Back
TrackPrevious = RS+Back
SwitcherDisplayTitle = false

[CDTracks]
01_Splash_wave.ogg = Splash Wave
C:\Music\My song.flac = My displayed title
```

The path is left of the first `=`; the title is the remaining text. Unquoted
paths, original order, case and duplicate entries are retained. Comments begin
with `#` or `;` after whitespace. Empty titles are allowed. An invalid section
disables the catalog: at most 256 entries, 4096 lines, 1 MiB of section text,
4095 bytes per path and 512 bytes per title. No catalog means no override.

Files resolve first as written (relative to the process working directory or
absolute), then in the retail `Sound` folder. Existing enabled same-stem FLAC
has priority over WAV, then the listed file. Explicit WAV/FLAC entries also
require `[Audio] AllowWAV`/`AllowFLAC`, each off by default. Unsupported formats
are refused. A selected file that fails reading, validation or decoding never
tries a different format. The existing 64 MiB input, metadata/decoder limits,
MenuAudio mixer and optional FLAC tag validation still apply. Playback uses
`loop=false`: a validated FLAC plays the full file once, including when loop
tags are present.

Only the two recovered race script music calls at 48687F/4868D3 can choose an
initial catalog track, and only in GAME mode 16. An out-of-range incoming index
selects entry zero. A refused initial catalog attempt falls through to the
original caller's channel, track and loop. Frontend previews, start loading,
pause, goal and ending music retain their existing calls.

During GAME, press Z for previous or X for next on the game's window. Holding
a key does not repeat. A pad combination switches when it ceases to be fully
held. If both pad combinations release together, the one with more buttons
wins; a tie favors Next. Simultaneous keyboard edges apply previous then next,
with one final playback. Selection wraps; failed playback preserves the cursor.
Keyboard and pad inputs still reach the game, even on a successful switch.
Race entry, panel opening and keyboard focus loss disarm switches until their
keys/buttons are released. Pad disconnect is not a switch.

Digital pad names: A/B/X/Y, LS/RS, LB/RB, LT/RT, Start/Back and
Left/Up/Right/Down, joined with `+`, case insensitive. These use the existing
native gamepad sample, normalized from its actual DualSense raw-bit layout to
the host pad layout used by combination names. The pad connection is captured
before wheel/shim filters update their own connection state, so a wheel cannot
turn a pad disconnect into a track switch. Wheel Controls actions are not new switcher bindings.
Analog stick direction aliases are refused. A bad combination disables this
slice instead of silently changing its meaning.

## Optional current-track title (new source)

`SwitcherDisplayTitle = true` opts into the existing game-font/HUD renderer.
It defaults to false here (Tweaks defaults it to true while its whole switcher
is off). The whole switcher must also be enabled in an ordinary process.
Only a successful manual next/previous change starts the display; a failed
open, initial race selection or original menu/start music does not. The line
is `#NN. Title`, from the actual current catalog cursor, never the audio path.

Like Tweaks, it lasts 120 game updates, full opacity until 45 remain and fading
during those last 45. Existing display-only 120 Hz frames do not age it, and
multiple queue flushes in one rendered frame do not duplicate it. It clears on
leaving GAME16, opening F6, keyboard focus clear, race entry blocking, native
HUD hide or host teardown; it does not carry a hidden title into a menu/race.
There is no new input/audio path, separate font loader, HUD style mutation or
global hook. The existing HUD queue retains ownership of its lazy font bank.

The existing Tweaks keys are accepted, in original 640x480 UI coordinates:

```ini
[CDSwitcher]
SwitcherDisplayTitle = true
SwitcherTitleFont = 2
SwitcherTitleFontSizeX = 0.3
SwitcherTitleFontSizeY = 0.5
SwitcherTitlePositionX = 375
SwitcherTitlePositionY = 450
```

Font is clamped to 0..9, scale to 0.05..2 (nonfinite values use the defaults),
and position to x0..639/y0..479. The recovered renderer keeps its clipping and
font glyph rules. Display is one literal line with at most 96 title bytes;
longer titles end in `...`, control/non-ASCII bytes display as `?`. The catalog's
512-byte title and file path remain unchanged. Missing fonts/glyphs submit
nothing while the timer expires; no fallback renderer or font resource is made.

The title also fits the available horizontal HUD space using the recovered
font producer's actual advances, bearings, spacing and kerning. It preserves
the track prefix and shortens the title with three complete dots before the
renderer would clip a source cell or its scaled extent at x0/640. If the prefix
and dots cannot fit at the configured position and scale, it submits nothing
while the timer ages. It does not shrink text, move the position, change the
shared font renderer or edit the catalog. Vertical placement remains configured.

Partial Tweaks parity remains: startup shuffle, default track catalog and
analog-direction aliases are not implemented. Scoped CD request/open/current
evidence does not establish audible/physical quality. The base title source
passed its affected compile/link and41 synthetic checks; this width-aware
follow-up is not compiled or run yet. Rendered/visual acceptance remains open.
