"""Cases for route_gate.py (sana 110457) against a simulated route: the game appends a state line when its route
signature changes, a press moves it to the next page only when the page has settled, and a stalled frontend keeps the
page for a while.

    py -3 -B test_route_gate.py
"""
import json, pathlib, sys, tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import route_gate as rg

FAIL = []
def check(v, what):
    if not v: FAIL.append(what); print("FAIL:", what)


def line(t, **f):
    v = dict(mode=32, child=0, pushes=0, confirms=0, index=0, start=0, go=0, pause=0); v.update(f)
    return "route t=%d frame=%d " % (t, t // 16) + " ".join("%s=%d" % (k, v[k]) for k in rg.FIELDS) + "\n"


class Game:
    """Pages: each Enter accepted on a settled page (ready_after seconds after arriving) advances to the next state."""
    def __init__(self, path, pages, ready_after=0.5, stall=(0, 0)):
        self.path, self.pages, self.ready_after, self.stall = path, pages, ready_after, stall
        self.t, self.page, self.arrived, self.posts, self.ignored = 0.0, 0, 0.0, [], 0
        self.write(self.pages[0])
    def write(self, f):
        with open(self.path, "a", encoding="ascii") as out: out.write(line(int(self.t * 1000), **f))
    def now(self): return self.t
    def sleep(self, dt): self.t += dt
    def alive(self): return True
    def post(self, key, up):
        self.posts.append((round(self.t, 3), key, up))
        if up: return
        stalled = self.stall[0] <= self.t < self.stall[0] + self.stall[1]
        if stalled or self.t - self.arrived < self.ready_after: self.ignored += 1; return
        nxt = self.pages[self.page].get("_next", {}).get(key)
        if nxt is None: return
        self.page = nxt; self.arrived = self.t
        self.write({k: v for k, v in self.pages[nxt].items() if not k.startswith("_")})


ROUTE = [
    dict(mode=32, child=0, _next={"enter": 1}),
    dict(mode=32, child=22, pushes=1, _next={"enter": 2}),
    dict(mode=32, child=58, pushes=2, _next={"down": 3}),
    dict(mode=32, child=58, pushes=2, index=1, _next={"enter": 4}),
    dict(mode=32, child=4, pushes=3, _next={"enter": 5}),
    dict(mode=13, child=4, pushes=3, start=2, _next={}),
]
PLAN = {"gated": True, "steps": [
    {"key": "enter", "when": {"mode": 32}, "settle": 1.0, "expect": {"changed": ["child"]}},
    {"key": "enter", "settle": 0.8, "expect": {"child": 58}},
    {"key": "down", "when": {"child": 58}, "settle": 0.8},
    {"key": "enter", "when": {"child": 58}, "settle": 0.8, "expect": {"child": 4}},
    {"key": "enter", "when": {"child": 4}, "settle": 0.8, "expect": {"mode": 13}},
]}


def run(tmp, pages, plan, **kw):
    path = pathlib.Path(tmp) / ("state-%d.txt" % len(list(pathlib.Path(tmp).iterdir())))
    clean = [{k: v for k, v in p.items()} for p in pages]
    g = Game(path, clean, **kw)
    g.pages = pages
    recs, err = rg.deliver(rg.load_gated(plan), g.post, g.now, g.sleep, rg.StateReader(path), g.alive)
    return g, recs, err


with tempfile.TemporaryDirectory() as tmp:
    # Parsing: complete lines only; a partial trailing line waits for its newline.
    p = pathlib.Path(tmp) / "partial.txt"
    p.write_text(line(5, child=3) + "route t=9 frame=0 mode=32 chi", encoding="ascii")
    r = rg.StateReader(p); got = r.poll()
    check(len(got) == 1 and r.state["child"] == 3, "a partial line is not parsed")
    with open(p, "a", encoding="ascii") as f: f.write("ld=7 pushes=0 confirms=0 index=0 start=0 go=0 pause=0\n")
    got = r.poll(); check(len(got) == 1 and r.state["child"] == 7, "the completed line is parsed on the next poll")
    check(rg.parse_line("route t=1 frame=1 mode=32") is None, "a short line is rejected")

    # The whole route on a settled frontend: every press lands, every key is released, no extra press.
    g, recs, err = run(tmp, ROUTE, PLAN)
    check(err is None and g.page == 5, "the gated route reaches START (%s)" % err)
    check(g.ignored == 0, "no press is ignored on a settled frontend")
    downs = [x for x in g.posts if not x[2]]; ups = [x for x in g.posts if x[2]]
    check(len(downs) == 5 and len(ups) == 5, "five presses, each released")

    # A stalled frontend (8 s) just after a page change drops the next press (nothing changes): without retries the
    # step reports it; with one retry the press is repeated once after the stall and the route completes.
    g, recs, err = run(tmp, ROUTE, PLAN, stall=(1.5, 8.0), ready_after=0.5)
    check(err is not None and "step 1" in err and "no expected effect" in err and g.ignored == 1,
          "a dropped press without retries is reported (%s)" % err)
    retry_plan = json.loads(json.dumps(PLAN))
    for s in retry_plan["steps"]:
        if "expect" in s: s["retries"] = 1
    g, recs, err = run(tmp, ROUTE, retry_plan, stall=(1.5, 8.0), ready_after=0.5)
    check(err is None and g.page == 5 and recs[1].get("retries") == 1, "a dropped press is re-pressed once and the route completes (%s)" % err)
    # A press that was taken (the page changed) but not to the expected page is never repeated.
    wrong = [dict(ROUTE[0]), dict(ROUTE[1], _next={"enter": 2}), dict(mode=32, child=59, pushes=2, _next={})] + ROUTE[3:]
    g, recs, err = run(tmp, wrong, retry_plan)
    check(err is not None and "child=59" in err and len([x for x in g.posts if not x[2] and x[1] == "enter"]) == 2,
          "a press that changed the state is reported, not retried (%s)" % err)

    # A press that is never accepted: the expect check fails with the last state, and no further key is posted.
    stuck = [dict(ROUTE[0]), dict(ROUTE[1], _next={})] + ROUTE[2:]
    g, recs, err = run(tmp, stuck, PLAN)
    check(err is not None and "step 1" in err and "child=22" in err, "an unaccepted press reports step and last state (%s)" % err)
    check(len([x for x in g.posts if not x[2]]) == 2, "no blind press after the failed step")
    check(all(any(u[1] == d[1] and u[2] for u in g.posts) for d in g.posts if not d[2]), "every pressed key was released")

    # A page that never arrives: the "when" wait times out with the condition and the last state.
    plan = {"gated": True, "steps": [{"key": "enter", "when": {"mode": 16, "go": 1}, "timeout": 5}]}
    g, recs, err = run(tmp, ROUTE, plan)
    check(err is not None and "waited 5 s" in err and "mode=32" in err and not g.posts, "a missing page times out without a press")

    # An interrupted hold (the harness stopped mid-press): the key is still released and the error propagates.
    class Interrupt(Exception): pass
    g = Game(pathlib.Path(tmp) / "interrupt.txt", [dict(p) for p in ROUTE])
    def interrupting_sleep(dt):
        if dt == 7.0: raise Interrupt()
        g.sleep(dt)
    plan = {"gated": True, "steps": [{"key": "up", "when": {"mode": 32}, "settle": 0, "hold": 7}]}
    try:
        rg.deliver(rg.load_gated(plan), g.post, g.now, interrupting_sleep, rg.StateReader(g.path), g.alive)
        check(False, "an interrupted hold propagates")
    except Interrupt: pass
    check([(k, u) for _, k, u in g.posts] == [("up", False), ("up", True)], "an interrupted hold still releases the key (%s)" % g.posts)

    # Plan validation.
    for bad in ({"gated": True, "steps": [{"key": "enter", "when": {"modee": 1}}]},
                {"gated": True, "steps": [{"key": "enter", "changed": ["x"]}]},
                {"gated": True, "steps": [{"key": "enter", "settle": 99}]}, {"steps": [{"key": "enter"}]}):
        try: rg.load_gated(bad); check(False, "rejects %s" % json.dumps(bad))
        except ValueError: pass

    # run_perf.py reads a gated plan through route_gate and still accepts its timed view (earliest press times).
    import run_perf
    gp = pathlib.Path(tmp) / "gated.json"
    gp.write_text(json.dumps(PLAN), encoding="utf-8")
    steps = run_perf.load_gated(gp)
    check(steps is not None and len(steps) == 5 and steps[2]["key"] == "down", "run_perf loads the gated steps")
    timed, measure_from, _ = run_perf.load_keys(gp)
    check(len(timed) == 5 and all(s["at"] == 0 for s in timed), "run_perf's timed view of a gated plan")
    gp.write_text(json.dumps({"gated": True, "steps": [{"key": "f1"}]}), encoding="utf-8")
    try: run_perf.load_gated(gp); check(False, "run_perf rejects an unknown gated key")
    except ValueError: pass
    tp = pathlib.Path(tmp) / "timed.json"; tp.write_text(json.dumps([{"key": "enter", "at": 1}]), encoding="utf-8")
    check(run_perf.load_gated(tp) is None, "a timed plan is not gated")

print("route_gate: %s" % ("FAIL (%d)" % len(FAIL) if FAIL else "all cases passed"))
sys.exit(1 if FAIL else 0)
