# Experimental CD catalog (source slice)

Off unless `[CDSwitcher] SwitcherEnable = true`. Ordinary play only: test,
recording and playback processes keep it off. No settings or Controls schema
is written. This source slice is not yet compiled or qualified in a game.

```ini
[CDSwitcher]
SwitcherEnable = true
TrackNext = Back
TrackPrevious = RS+Back

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

Partial Tweaks parity: title text is stored separately but no title overlay is
drawn; startup shuffle, default track catalog and analog-direction aliases are
not implemented. There is no rendered, audible, runtime or physical acceptance
from this source slice.
