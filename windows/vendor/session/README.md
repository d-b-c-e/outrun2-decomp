# Vendored: wheel toolkit native session writer

From [d-b-c-e/dbce-wheel-mod-toolkit](https://github.com/d-b-c-e/dbce-wheel-mod-toolkit)
`native/session/` at `85147b87bd7b746cecacc3858425723f4f6b2955` (2026-10-07), byte for byte, MIT
(`LICENSE` here). Do not edit; re-vendor from the toolkit and update this line and the pins together.

| File | SHA-256 | Role |
| --- | --- | --- |
| `session_writer.h` | `a9de5d49c55b9b4be464fb173178d20a97972eacff500aee0267e30fb44d19db` | the reviewed P20 native writer (`dbce::session::Writer`), used by `windows/host/recorder` |

The recorder fixtures (`recorder_lifecycle_spike`, `queued_frames_spike`, `p21_recorder_integration`) pin this same
hash. The managed reader and seal (`Dbce.Wheel.Recording`, `ArtifactSeal`) stay in the toolkit and are built from it
by the fixtures and the finalizer.
