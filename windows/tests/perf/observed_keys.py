"""Per-press observed key delivery (sana 36424/36438): run_perf --observed-keys. Each scripted key edge is posted, then
waited for until the game echoes it: a line "<row>\t<key>\tdown|up" that the game appends to keys-seen.tsv for each
keyboard edge its input tick samples (windows/source/key_echo.hpp; recording sessions only). A press posted into a
stall is therefore held until the game has seen it, instead of being lost (pose proof Run A1: a 552 ms stall swallowed
a 100 ms Enter).

Rules:
- An event is due at its scripted time plus the lateness accumulated so far: how much later than its scripted time
  (beyond `slack`, the normal echo latency of a tick or two) any earlier event was echoed. A press is also due no
  sooner than `gap` after the previous echo; a release keeps its scripted hold. Echo lines read before an event's
  post can never satisfy it (they are drained first).
- It is satisfied only by a NEW line, appended after its post, for the same key and edge, at a row no lower than the
  previous match.
- No echo within `timeout`: FAIL. Every key still held is released, and nothing more is posted (no retry).
- Any other echo line is a failure too (a key or edge nobody posted: the drive would not be the script's), as is a
  malformed line.
The capture and its seal stay the evidence; the echo is diagnostic only.
"""

ECHO_KEYS = ("enter", "esc", "up", "down", "left", "right")   # key_echo.hpp's set, in its order


class EchoError(Exception):
    pass


class EchoReader:
    """keys-seen.tsv as the game appends it: complete lines only, each read once. A missing file reads as no lines yet."""

    def __init__(self, path, opener=open):
        self.path, self.opener, self.pos, self.partial = path, opener, 0, b""

    def poll(self):
        try:
            with self.opener(self.path, "rb") as f:
                f.seek(self.pos); data = f.read()
        except FileNotFoundError:
            return []
        self.pos += len(data)
        *complete, self.partial = (self.partial + data).split(b"\n")
        out = []
        for raw in complete:
            parts = raw.decode("ascii", "replace").rstrip("\r").split("\t")
            if len(parts) != 3 or not parts[0].isdigit() or parts[1] not in ECHO_KEYS or parts[2] not in ("down", "up"):
                raise EchoError("malformed echo line %r" % raw)
            out.append((int(parts[0]), parts[1], parts[2]))
        return out


def events_of(steps):
    """The script's steps as edges (at, key, up), in time order; at equal times a release before a press."""
    ev = [(s["at"], s["key"], False) for s in steps] + [(s["at"] + s["hold"], s["key"], True) for s in steps]
    ev.sort(key=lambda e: (e[0], not e[2]))
    for _, key, _ in ev:
        if key not in ECHO_KEYS: raise EchoError("key %r has no echo (observed keys: %s)" % (key, ", ".join(ECHO_KEYS)))
    return ev


def deliver(events, post, now, sleep, echo, timeout=3.0, gap=0.25, slack=0.05, alive=lambda: True, poll_every=0.005):
    """Posts every event and waits for its echo. Returns (records, error). records: one per posted event with its
    scripted, posted and echo times (seconds on `now`'s clock) and the echo row; error: None, or why delivery stopped
    (every held key released by then)."""
    records, held, shift, last_echo, last_row = [], set(), 0.0, None, -1

    def release_all():
        for key in sorted(held):
            try: post(key, True)
            except Exception: pass
        held.clear()

    try:
        for at, key, up in events:
            due = at + shift if up or last_echo is None else max(at + shift, last_echo + gap)
            while now() < due:
                if not alive(): raise EchoError("the game exited before the %s %s at %.3f" % (key, "up" if up else "down", at))
                for line in echo.poll(): raise EchoError("an echo nobody posted: %r" % (line,))
                sleep(poll_every)
            for line in echo.poll(): raise EchoError("an echo nobody posted: %r" % (line,))
            if not alive(): raise EchoError("the game exited before the %s %s at %.3f" % (key, "up" if up else "down", at))
            posted = now(); post(key, up)
            (held.discard if up else held.add)(key)
            edge = "up" if up else "down"
            rec = dict(key=key, up=up, at=round(at, 3), posted=round(posted, 3), echo=None, row=None)
            records.append(rec)
            while rec["echo"] is None:
                for row, k, e in echo.poll():
                    if k == key and e == edge and row >= last_row and rec["echo"] is None:
                        rec["echo"], rec["row"] = round(now(), 3), row
                    else: raise EchoError("an echo nobody posted: %r (waiting for %s %s)" % ((row, k, e), key, edge))
                if rec["echo"] is not None: break
                if now() - posted > timeout: raise EchoError("no echo of %s %s within %.1f s of its post at %.3f" % (key, edge, timeout, posted))
                if not alive(): raise EchoError("the game exited while %s %s was unechoed" % (key, edge))
                sleep(poll_every)
            last_echo, last_row = rec["echo"], rec["row"]
            shift = max(shift, rec["echo"] - at - slack)
        return records, None
    except EchoError as e:
        release_all()
        return records, str(e)
