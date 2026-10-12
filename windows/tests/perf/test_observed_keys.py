"""Cases for observed_keys.py (sana 36438) against a simulated game: a 60 Hz input tick that samples the posted key
state (as desktop_keys() does) and echoes each edge it samples, with stalls during which nothing is sampled.

    py -3 -B test_observed_keys.py
"""
import os, pathlib, sys, tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import observed_keys as ok

FAIL = []
def check(v, what):
    if not v: FAIL.append(what); print("FAIL:", what)


class Game:
    """The posted state is what the window procedure has applied; a tick samples it, and only a tick sees an edge."""
    def __init__(self, stalls=(), echo_from=0.0, echo_until=None, tick=1 / 60):
        self.t, self.tick, self.next_tick, self.stalls = 0.0, tick, 0.0, list(stalls)
        self.posted, self.sampled, self.row, self.lines, self.posts = set(), set(), 0, [], []
        self.echo_from, self.echo_until, self.exited, self.seen_presses = echo_from, echo_until, None, []
    def post(self, key, up):
        self.posts.append((round(self.t, 3), key, up)); (self.posted.discard if up else self.posted.add)(key)
    def now(self): return self.t
    def sleep(self, dt):
        end = self.t + dt
        while self.next_tick <= end and self.alive_at(self.next_tick):
            self.t = self.next_tick
            stall = next((s for s in self.stalls if s[0] <= self.t < s[0] + s[1]), None)
            if stall: self.next_tick = stall[0] + stall[1]; continue   # frozen: no sample until the stall ends
            for k in ok.ECHO_KEYS:
                if (k in self.posted) != (k in self.sampled):
                    down = k in self.posted
                    if down: self.seen_presses.append(k)
                    if self.t >= self.echo_from and (self.echo_until is None or self.t < self.echo_until):
                        self.lines.append((self.row, k, "down" if down else "up"))
            self.sampled = set(self.posted); self.row += 1; self.next_tick = self.t + self.tick
        self.t = end
    def alive_at(self, t): return self.exited is None or t < self.exited
    def alive(self): return self.alive_at(self.t)


class Reader:
    def __init__(self, game): self.g, self.n = game, 0
    def poll(self):
        out = self.g.lines[self.n:]; self.n = len(self.g.lines); return out


def run(steps, game, **kw):
    return ok.deliver(ok.events_of(steps), game.post, game.now, game.sleep, Reader(game), **kw)


MENU = [dict(key="enter", at=1.0, hold=0.1), dict(key="down", at=1.5, hold=0.1), dict(key="enter", at=2.2, hold=0.1),
        dict(key="enter", at=3.0, hold=0.1)]

# A plain run: every edge echoed, nothing late.
g = Game(); recs, err = run(MENU, g)
check(err is None and len(recs) == 8 and all(r["echo"] is not None for r in recs), "every edge echoed (%s)" % err)
check(g.seen_presses == ["enter", "down", "enter", "enter"], "the game saw every press once: %s" % g.seen_presses)
check(all(r["echo"] - r["posted"] < 0.05 for r in recs), "echoes within a few ticks")
check(max(r["posted"] - r["at"] for r in recs) < 0.3, "no lateness without a stall")

# The A1 case: a 552 ms stall over the third press. Timed delivery loses it; observed delivery holds it until seen.
stall = [(2.15, 0.552)]
g = Game(stalls=stall); t = 0.0
for at, key, up in ok.events_of(MENU):
    g.sleep(at - g.t); g.post(key, up)
g.sleep(1.0)
check(g.seen_presses == ["enter", "down", "enter"], "timed delivery loses the press inside the stall (the model reproduces A1): %s" % g.seen_presses)
g = Game(stalls=stall); recs, err = run(MENU, g)
check(err is None and g.seen_presses == ["enter", "down", "enter", "enter"], "observed delivery: every press seen (%s, %s)" % (err, g.seen_presses))
third = [r for r in recs if r["key"] == "enter" and not r["up"]][1]
check(third["echo"] >= 2.15 + 0.552 - 1e-9, "the stalled press is echoed only after the stall (%s)" % third)
last = recs[-2]
check(last["posted"] - last["at"] >= 0.45, "later events keep their spacing after the stall (late by %.3f)" % (last["posted"] - last["at"]))
check([r["row"] for r in recs] == sorted(r["row"] for r in recs), "echo rows in order")

# Overlapping holds in a race: accelerate held while steering taps.
RACE = [dict(key="up", at=1.0, hold=4.0), dict(key="left", at=2.0, hold=1.5), dict(key="right", at=3.6, hold=1.0)]
g = Game(stalls=[(2.0, 0.3)]); recs, err = run(RACE, g)
check(err is None and g.seen_presses == ["up", "left", "right"] and not g.posted, "overlapping holds delivered, all released (%s)" % err)

# The echo stops (a failed writer): FAIL within the timeout, every held key released, nothing more posted.
g = Game(echo_until=1.2); recs, err = run(RACE, g, timeout=1.0)
check(err is not None and "no echo of left down" in err, "a missing echo fails: %s" % err)
check(not g.posted and g.posts[-1][1:] in (("up", True), ("left", True)), "every held key released on failure: %s" % g.posts[-3:])
check(not any(p[1] == "right" for p in g.posts), "nothing posted after the failure")

# A stale or foreign echo never satisfies an event, and fails the run.
g = Game(); g.lines.append((0, "enter", "down"))   # before anything was posted
recs, err = run(MENU, g)
check(err is not None and "nobody posted" in err and not recs, "an echo before the post fails, not satisfies: %s" % err)
class Foreign(Game):
    def sleep(self, dt):
        before = len(self.lines); super().sleep(dt)
        if self.t > 1.7 and not getattr(self, "done", False): self.done = True; self.lines.append((self.row, "esc", "down"))
g = Foreign(); recs, err = run(MENU, g)
check(err is not None and "esc" in err and not g.posted, "a key nobody posted fails the run, held keys released: %s" % err)
class Lower(Game):   # a matching edge at a row below the previous match
    def sleep(self, dt):
        super().sleep(dt)
        if self.lines and self.lines[-1][1:] == ("down", "down") and not getattr(self, "done", False):
            self.done = True; r = self.lines.pop(); self.lines.append((0, "down", "down"))
g = Lower(); recs, err = run(MENU, g)
check(err is not None and not g.posted, "a matching edge at an older row fails: %s" % err)

class WrongEdge(Game):   # the posted key, but the other edge
    def sleep(self, dt):
        super().sleep(dt)
        if self.lines and self.lines[-1][1:] == ("down", "down") and not getattr(self, "done", False):
            self.done = True; r = self.lines.pop(); self.lines.append((r[0], "down", "up"))
g = WrongEdge(); recs, err = run(MENU, g)
check(err is not None and "'down', 'up')" in err and not g.posted, "the posted key's other edge fails: %s" % err)

# The game exits mid-script.
g = Game(); g.exited = 1.6; recs, err = ok.deliver(ok.events_of(MENU), g.post, g.now, g.sleep, Reader(g), alive=g.alive)
check(err is not None and "exited" in err and not g.posted and not any(p[0] >= 1.6 and not p[2] for p in g.posts),
      "the game's exit stops delivery, nothing pressed after it, keys released: %s %s" % (err, g.posts[-2:]))

# sana 36766: a failed post or echo read never escapes with keys held; an interrupt releases them and propagates.
class PostFails(Game):
    def post(self, key, up):
        if len(self.posts) == 1 and not up: self.posts.append((round(self.t, 3), key, "refused")); raise OSError(1400, "PostMessage failed")
        super().post(key, up)
g = PostFails(); recs, err = run(RACE, g)
check(err is not None and "OSError" in err and not g.posted and g.posts[-1][1:] == ("up", True), "a failed post: a failure, the held key released (%s, %s)" % (err, g.posts))
class ReadFails(Reader):
    def poll(self):
        if self.g.posts: raise PermissionError(32, "sharing violation")
        return super().poll()
g = Game(); recs, err = ok.deliver(ok.events_of(RACE), g.post, g.now, g.sleep, ReadFails(g))
check(err is not None and "PermissionError" in err and not g.posted, "a failed echo read: a failure, held keys released (%s)" % err)
class Interrupted(Game):
    def sleep(self, dt):
        super().sleep(dt)
        if self.t > 2.5: raise KeyboardInterrupt
g = Interrupted()
try: run(RACE, g); check(False, "an interrupt propagates")
except KeyboardInterrupt: check(not g.posted and any(p[1] == "left" and p[2] for p in g.posts), "an interrupt releases every held key first: %s" % g.posts[-3:])

# Keys without an echo are refused up front; equal times release before press.
try: ok.events_of([dict(key="space", at=1.0, hold=0.1)]); check(False, "a key without an echo is refused")
except ok.EchoError: pass
ev = ok.events_of([dict(key="left", at=1.0, hold=1.0), dict(key="right", at=2.0, hold=0.5)])
check(ev[1] == (2.0, "left", True) and ev[2] == (2.0, "right", False), "a release before a press at the same time: %s" % ev)

# EchoReader: complete lines only, each once; a missing file is no lines; malformed lines fail.
with tempfile.TemporaryDirectory() as t:
    p = os.path.join(t, "keys-seen.tsv"); r = ok.EchoReader(p)
    check(r.poll() == [], "no file yet: no lines")
    with open(p, "wb") as f: f.write(b"12\tenter\tdown\n13\tent")
    check(r.poll() == [(12, "enter", "down")], "a partial line waits")
    with open(p, "ab") as f: f.write(b"er\tup\r\n")
    check(r.poll() == [(13, "enter", "up")] and r.poll() == [], "the rest of the line, once")
    with open(p, "ab") as f: f.write(b"14\tspace\tdown\n")
    try: r.poll(); check(False, "a malformed line fails")
    except ok.EchoError: pass

print("observed_keys: %d failure(s)" % len(FAIL))
sys.exit(1 if FAIL else 0)
