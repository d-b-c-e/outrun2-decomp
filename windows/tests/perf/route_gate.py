"""State-gated key delivery for run_perf.py (sana 110457): each key waits for the frontend/race state it belongs to.

The game (OR2_ROUTE_STATE=<file>, game_host.cpp route_state_note) appends one line whenever its route signature changes:

    route t=<ms> frame=<n> mode=<78026C> child=<state2 page key> pushes=<frontend stack pushes> confirms=<PC confirms>
          index=<menu cursor> start=<START stage> go=<1 when GAME and 8367BC <= 60> pause=<[780248]>

A gated plan is an object {"gated": true, "steps": [...]} whose steps may carry:
    "when":    {field: value, ...}  the state needed before the press (all fields equal)
    "changed": [field, ...]         and these fields differ from their values when the previous step was posted
    "settle":  seconds the state must stay unchanged before the press (default 0.8)
    "expect":  {field: value, ...} or {"changed": [field, ...]}: checked after the release, within "expect_timeout"
    "timeout": seconds to wait for "when"/"changed"/"settle" (default 30); "expect_timeout" (default 10)
    "at":      the earliest press time, seconds after the window appears (default 0)
    "hold":    seconds between key down and key up (default 0.1)
    "retries": with an "expect": re-press (at most 2) only when no state field changed at all since the press
Every press is released. A timeout stops the delivery with the step, the condition and the last state; nothing is
pressed blindly afterwards (the caller closes the game).
"""
import pathlib, re

FIELDS = ("mode", "child", "pushes", "confirms", "index", "start", "go", "pause")
# Old trace lines remain readable. These observations exist only in the extended
# native trace; their absence must never masquerade as selected row/course zero.
MENU_FIELDS = ("widgets", "alternate", "course", "source", "secondary")
LINE = re.compile(r"^route t=(\d+) frame=(\d+) " + " ".join(r"%s=(-?\d+)" % f for f in FIELDS)
                  + r"(?: " + " ".join(r"%s=(-?\d+)" % f for f in MENU_FIELDS) + r")?\s*$")


class GateError(Exception):
    pass


def parse_line(line):
    m = LINE.match(line.strip())
    if not m: return None
    names = ("t", "frame") + FIELDS + MENU_FIELDS
    return {name: int(value) for name, value in zip(names, m.groups()) if value is not None}


class StateReader:
    """Reads only complete appended lines; keeps the latest state and when (caller clock) it was first seen."""
    def __init__(self, path):
        self.path, self.offset, self.partial, self.state, self.lines = pathlib.Path(path), 0, "", None, 0
    def poll(self):
        """New complete states since the last poll, in file order."""
        if not self.path.exists(): return []
        with open(self.path, "rb") as f:
            f.seek(self.offset); data = f.read()
        self.offset += len(data)
        text = self.partial + data.decode("ascii", errors="replace")
        *done, self.partial = text.split("\n")
        out = [s for s in (parse_line(l) for l in done) if s]
        self.lines += len(out)
        if out: self.state = out[-1]
        return out


def load_gated(doc):
    if not isinstance(doc, dict) or not doc.get("gated"): raise ValueError("not a gated plan")
    steps = doc.get("steps")
    if not isinstance(steps, list) or not steps: raise ValueError("gated plan has no steps")
    out = []
    for i, s in enumerate(steps):
        when, changed, expect = s.get("when", {}), s.get("changed", []), s.get("expect", {})
        for d in (when, {k: v for k, v in expect.items() if k != "changed"}):
            bad = [k for k in d if k not in FIELDS]
            if bad: raise ValueError("step %d: unknown state field(s) %s" % (i, bad))
        for f in list(changed) + list(expect.get("changed", [])):
            if f not in FIELDS: raise ValueError("step %d: unknown changed field %r" % (i, f))
        hold, settle = float(s.get("hold", 0.1)), float(s.get("settle", 0.8))
        timeout, expect_timeout, at = float(s.get("timeout", 30)), float(s.get("expect_timeout", 10)), float(s.get("at", 0))
        retries = int(s.get("retries", 0))
        if not (0 < hold <= 60 and 0 <= settle <= 10 and 0 < timeout <= 300 and 0 < expect_timeout <= 120 and at >= 0):
            raise ValueError("step %d: bad timing" % i)
        if not 0 <= retries <= 2 or (retries and not expect): raise ValueError("step %d: retries need an expect (0..2)" % i)
        out.append(dict(key=str(s["key"]).lower(), when=dict(when), changed=list(changed), expect=dict(expect),
                        hold=hold, settle=settle, timeout=timeout, expect_timeout=expect_timeout, at=at, retries=retries))
    return out


def _fmt(state):
    return "none" if state is None else " ".join("%s=%d" % (k, state[k]) for k in FIELDS)


def _holds(state, when, changed, ref):
    if state is None: return False
    if any(state[k] != v for k, v in when.items()): return False
    return not changed or ref is None or any(state[k] != ref[k] for k in changed)


def _press(post, sleep, key, hold):
    """Key down, the hold, key up; the release runs even if the hold or a later callback is interrupted."""
    post(key, False)
    try: sleep(hold)
    finally: post(key, True)


def deliver(steps, post, now, sleep, reader, alive, poll=0.05):
    """Posts each step when its state holds and has settled; returns (records, error or None)."""
    recs, ref, changed_at = [], None, now()
    for i, s in enumerate(steps):
        start = now(); last = reader.state
        while True:
            if not alive(): return recs, "step %d (%s): the game exited; last state: %s" % (i, s["key"], _fmt(reader.state))
            if reader.poll(): changed_at = now()
            st = reader.state
            ok = _holds(st, s["when"], s["changed"], ref) and now() - changed_at >= s["settle"] and now() >= s["at"]
            if ok: break
            if now() - start > s["timeout"]:
                return recs, "step %d (%s): waited %.0f s for when=%s changed=%s settle=%.1f; last state: %s" % (
                    i, s["key"], s["timeout"], s["when"], s["changed"], s["settle"], _fmt(st))
            sleep(poll)
        ref = dict(st); posted = now()
        _press(post, sleep, s["key"], s["hold"])
        changed_at = now()   # the next step settles from the later of a state change and this release
        rec = dict(step=i, key=s["key"], posted=round(posted, 3), hold=s["hold"], state=_fmt(st))
        recs.append(rec)
        if s["expect"]:
            exp_when = {k: v for k, v in s["expect"].items() if k != "changed"}
            exp_changed = s["expect"].get("changed", [])
            t, retries = now(), s["retries"]
            while True:
                if reader.poll(): changed_at = now()
                if _holds(reader.state, exp_when, exp_changed, ref): break
                if not alive(): return recs, "step %d (%s): the game exited; last state: %s" % (i, s["key"], _fmt(reader.state))
                if now() - t > s["expect_timeout"]:
                    # Retried only when nothing at all changed since the press (a dropped press, e.g. a stalled
                    # frontend); any other state means the press was taken, and is reported instead.
                    if retries and reader.state == ref:
                        retries -= 1; rec["retries"] = rec.get("retries", 0) + 1
                        _press(post, sleep, s["key"], s["hold"]); t = now(); continue
                    return recs, "step %d (%s): no expected effect %s within %.0f s; last state: %s" % (
                        i, s["key"], s["expect"], s["expect_timeout"], _fmt(reader.state))
                sleep(poll)
            rec["effect"] = _fmt(reader.state)
    return recs, None
