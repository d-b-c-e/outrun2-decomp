"""Offline check of run.py's run timing (romi 23861): no game, device, lease or file. A fake clock drives the same
decisions the run loop uses: loop_deadline for the loop bound, replay_due for R3's end, and inject_expiry for inject.on.

    py -3 -B test_run_timing.py
"""
import math, pathlib, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import run

failures = []
def check(ok, what):
    if not ok: failures.append(what)


def simulate(startup, race_entry, replay_timeout, playback_end_at=None, step=0.2, horn=False):
    """The run loop's timing on a fake clock: the menus until race_entry, then the race phase. Returns (why the loop
    ended, the fake time it ended). R1 when replay_timeout is None: its steer holds end it, 3.5 s each; with horn, then
    the horn step (HORN_SECONDS)."""
    start = 1000.0; budget = run.race_budget(replay_timeout, horn); race_at = None; phase = "menus"; phase_at = 0.0; now = start
    while now < run.loop_deadline(start, startup, race_at, budget):
        now += step
        if phase == "menus":
            if now - start >= race_entry: race_at = now; phase, phase_at = ("replay" if replay_timeout is not None else "steer-right"), now
            continue
        if phase == "steer-right" and now - phase_at > 3.5: phase, phase_at = "steer-left", now
        elif phase == "steer-left" and now - phase_at > 3.5:
            if horn: phase, phase_at = "horn", now
            else: return "end", now - start
        elif phase == "horn" and now - phase_at > run.HORN_SECONDS: return "end", now - start
        elif phase == "replay" and run.replay_due(now, phase_at, replay_timeout,
                                                  playback_end_at is not None and now - race_at >= playback_end_at):
            return "end", now - start
    return "deadline", now - start


# romi's case: 180 s startup, a cold menu reaching the race at 165 s, R1 raceSeconds 7 -> a 44 s replay bound.
bound = run.replay_bound(7, 0)
check(bound == 44, "replay bound for raceSeconds 7 and no rows: %r" % bound)
why, at = simulate(180, 165, bound)
check(why == "end" and at > 165 + bound, "late race entry: R3 ends by its own bound, not the loop's (%s at %.1f)" % (why, at))
why, at = simulate(180, 165, bound, playback_end_at=12)
check(why == "end" and abs(at - (165 + 12)) < 0.5, "late race entry: the tape's end ends R3 (%s at %.1f)" % (why, at))
# Mutation control: the former single deadline (start + --seconds) ends that same R3 at 180 s with replay owed.
real = run.loop_deadline
run.loop_deadline = lambda start, startup, race_at, budget: start + startup
try:
    why, at = simulate(180, 165, bound)
    check(why == "deadline" and at < 165 + bound, "mutation control: the former deadline must truncate R3 (%s at %.1f)" % (why, at))
finally:
    run.loop_deadline = real
# R1 entering the race at the end of its startup budget still runs both steer holds.
why, at = simulate(180, 179, None)
check(why == "end" and at > 179 + 7, "late race entry: R1 completes its steer holds (%s at %.1f)" % (why, at))
# --horn (AllowHorn command path): R1's budget grows by the horn step, a late race entry still sends it, and the
# replay budgets do not change.
check(run.race_budget(None, True) == run.R1_RACE_SECONDS + run.HORN_SECONDS + run.RACE_MARGIN, "the horn step's budget")
check(run.race_budget(bound, True) == run.race_budget(bound), "the horn flag does not change a replay budget")
why, at = simulate(180, 179, None, horn=True)
check(why == "end" and at > 179 + 7 + run.HORN_SECONDS, "late race entry: R1 completes its steer holds and the horn step (%s at %.1f)" % (why, at))
check(run.inject_expiry(0, 180, run.race_budget(None, True)) >= run.loop_deadline(0, 180, 180, run.race_budget(None, True)) + run.INJECT_MARGIN,
      "inject.on outlives the horn run's latest bound")
# The horn run's INI: the exact test profile, then the horn on an otherwise unused R12 button, then [Audio] AllowHorn.
check(run.HORN_INI.startswith(run.PROFILE) and "button 27" not in run.PROFILE, "the horn button is unused by the test profile")
check(run.HORN_INI[len(run.PROFILE):] == "horn = button 27 %s\n[Audio]\nAllowHorn = true\n" % run.WHEEL, "the horn run's INI delta")
# The evidence (sana #132448): the LAST race sound line (native_race_sound_status's format); delivery is a positive PLAYED
# count for both the loop and the stop; refused, missing or x0 is a request only.
def sound(played="", refused="", missing=""):
    return ("race sound inits=1 controls=99 destroys=0 skipped=0 ics=0 clear_all=0 comm_trans=0 fade9560cc=1 count955c9c=0"
            " played:%s refused:%s missing:%s ics(ch/code=value): 1/2=3" % (played, refused, missing))
ev = run.horn_evidence("x\n%s\n%s\n" % (sound(" 51x2"), sound(" 51x3 9bcx40 81bcx1")))
check(ev["loop"] and ev["stop"] and ev["played"] == dict(loop=40, stop=1) and ev["refused"] == dict(loop=0, stop=0), "both played: %r" % ev)
ev = run.horn_evidence("%s\n%s\n" % (sound(" 9bcx40 81bcx1"), sound(" 51x3")))
check(not ev["loop"] and not ev["stop"], "only the LAST race sound line counts: %r" % ev)
ev = run.horn_evidence(sound(" 9bcx40", " 81bcx1"))
check(ev["loop"] and not ev["stop"] and ev["refused"]["stop"] == 1, "a refused stop is not delivered: %r" % ev)
ev = run.horn_evidence(sound("", " 9bcx3 81bcx1"))
check(not ev["loop"] and not ev["stop"] and ev["refused"] == dict(loop=3, stop=1), "request-only (both refused) is not delivered: %r" % ev)
ev = run.horn_evidence(sound("", "", " 9bcx1 81bcx1"))
check(not ev["loop"] and not ev["stop"] and ev["missing"] == dict(loop=1, stop=1), "missing effects are not delivered: %r" % ev)
ev = run.horn_evidence(sound(" 9bcx0 81bcx0"))
check(not ev["loop"] and not ev["stop"], "x0 counts are not delivered: %r" % ev)
ev = run.horn_evidence(sound(" 9bcx3"))
check(ev["loop"] and not ev["stop"], "a loop without its stop: %r" % ev)
ev = run.horn_evidence(sound(" 19bcx3 381bcx1"))
check(not ev["loop"] and not ev["stop"], "other ids that merely end in 9bc / 81bc are not the horn: %r" % ev)
ev = run.horn_evidence(sound(" 9bcx3 junk 81bcx1"))
check(ev.get("malformed") and not ev["loop"] and not ev["stop"], "an unreadable played section is no evidence: %r" % ev)
ev = run.horn_evidence("race sound inits=1 9bcx3 81bcx1")
check(not ev["loop"] and not ev["stop"] and ev["played"] is None, "a line without its sections is no evidence: %r" % ev)
check(run.horn_evidence("nothing")["line"] is None, "no race sound line: no evidence")
def horn_failures(**horn):
    return [f for f in run.verdict(dict(errors=[], phase="end", exitCode=0, horn=dict(sentAt=1.0, **horn))) if f.startswith("horn")]
check(horn_failures(**run.horn_evidence(sound(" 9bcx40 81bcx1"))) == [], "the verdict passes a played loop and stop")
f = horn_failures(**run.horn_evidence(sound("", " 9bcx3 81bcx1")))
check(len(f) == 2 and "9BC (loop) not played" in f[0] and "81BC (stop) not played" in f[1] and "refused {'loop': 3, 'stop': 1}" in f[0],
      "the verdict fails request-only, with the counts: %r" % f)
# An unresolved close on a --horn run (sana #132311): retained and reported, never killed; never sealed; the verdict fails.
class Running:
    pid = 4242
    def poll(self): return None
    def kill(self): raise AssertionError("a retained process is killed")
r = dict(creationTime=7, errors=[], forced=False)
run.retain(Running(), r)
check(r["retained"]["pid"] == 4242 and r["retained"]["creationTime"] == 7 and not r["forced"], "retain records pid and creation time: %r" % r)
check(any(f.startswith("process retained") for f in run.verdict(dict(r, phase="end", exitCode=None))), "the verdict fails a retained process")
import tempfile
with tempfile.TemporaryDirectory() as t:
    (pathlib.Path(t) / "run" / "capture").mkdir(parents=True)
    run.seal(pathlib.Path(t), Running(), 7, r, None)
    check(r["seal"] == "no observed exit" and not (pathlib.Path(t) / "run" / "capture" / "exit.json").exists(), "a retained process is not sealed: %r" % r["seal"])
# The --horn lease order (sana #132742), read from main's source: the early release (right after the cleanup) skips --horn;
# --horn releases in the finally of the try that holds the seal, the replay and the verdict, unless the process is retained.
import ast, inspect
def calls(node, name): return [n for n in ast.walk(node) if isinstance(n, ast.Call) and getattr(n.func, "id", None) == name]
tree = ast.parse(inspect.getsource(run.main))
parents = {c: p for p in ast.walk(tree) for c in ast.iter_child_nodes(p)}
exits = []   # each lease("Exit"): (the calls in the body of the try whose finally holds it, its nearest if-test)
for c in calls(tree, "lease"):
    if not (c.args and getattr(c.args[0], "value", None) == "Exit"): continue
    node, guard, owner = c, None, None
    while node in parents and owner is None:
        p = parents[node]
        if isinstance(p, ast.If) and guard is None: guard = ast.unparse(p.test)
        if isinstance(p, ast.Try) and node in p.finalbody: owner = p
        node = p
    body = ast.Module(body=owner.body, type_ignores=[]) if owner else None
    exits.append(({n for n in ("seal", "replay", "verdict") if body and calls(body, n)}, guard))
check(len(exits) == 2, "two lease releases in main: %r" % exits)
check((set(), "leased and (not o.horn) and (not o.lease_held)") in exits, "the early release (after the cleanup) skips --horn: %r" % exits)
check(({"seal", "replay", "verdict"}, "leased and o.horn and (not o.lease_held) and (not receipt.get('retained'))") in exits,
      "--horn releases in the finally after the seal, replay and verdict, unless retained: %r" % exits)
# --lease-held: the caller holds the lease through the evidence, so main neither takes nor releases it.
enters = [c for c in calls(tree, "lease") if isinstance(c.args[0], ast.Constant) and c.args[0].value == "Enter"]
check(len(enters) == 1 and ast.unparse(parents[enters[0]].test if isinstance(parents[enters[0]], ast.If) else parents[parents[enters[0]]].test) == "not o.lease_held",
      "--lease-held skips the Enter: %r" % [ast.unparse(parents[c]) for c in enters])
# No race: the startup budget alone bounds the run.
why, at = simulate(180, 10_000, bound)
check(why == "deadline" and 180 <= at < 180.5, "no race: the startup budget ends the run (%s at %.1f)" % (why, at))
# The loop bound, before and after race entry; the overall bound; inject.on outlives the latest loop bound.
for replay_timeout in (None, bound, run.replay_bound(60, 9000)):
    budget = run.race_budget(replay_timeout)
    check(run.loop_deadline(0, 180, None, budget) == 180, "before race entry the bound is the startup budget")
    latest = run.loop_deadline(0, 180, 180, budget)
    check(latest == 180 + budget, "the overall bound is startup + race budget")
    check(run.inject_expiry(0, 180, budget) >= latest + run.INJECT_MARGIN, "inject.on expires after the latest loop bound plus its margin")
    if replay_timeout is not None: check(budget > replay_timeout, "the race budget exceeds the replay bound it carries")
check(run.replay_bound(7, 9000) == 9000 / 30 + 15, "the former rows/30 + 15 floor stays the minimum")
# raceSeconds: finite positive numbers only (Python's json admits NaN and Infinity; a bool is an int).
for v in (7, 7.0, 0.5):
    check(run.valid_race_seconds(v), "valid raceSeconds %r refused" % (v,))
for v in (None, 0, -1, float("nan"), float("inf"), -math.inf, True, False, "7", [7]):
    check(not run.valid_race_seconds(v), "invalid raceSeconds %r accepted" % (v,))
import json
check(not run.valid_race_seconds(json.loads('{"raceSeconds": NaN}')["raceSeconds"]), "JSON NaN accepted")
check(not run.valid_race_seconds(json.loads('{"raceSeconds": Infinity}')["raceSeconds"]), "JSON Infinity accepted")

# The menu script: in order, one step per poll, only once due; then the former presses after the fallback delay.
script = run.MENU_SCRIPT
check(run.next_menu_action(script, None, 0, 99, 0) is None, "no press before the log shows the frontend (mode 32)")
check(run.next_menu_action(script, 5.9, 0, 99, 0) is None and run.next_menu_action(script, 6.0, 0, 99, 0) == "confirm", "the first step at its time")
check(run.next_menu_action(script, 30.0, 2, 0.4, 2) is None and run.next_menu_action(script, 30.0, 2, 2.0, 2) == "navDown",
      "a late step waits its scripted gap after the previous press, then sends the next unsent step, not a later one")
check(run.next_menu_action(script, 30.0, 3, 0.2, 3) is None, "a late scripted step still waits MENU_MIN_GAP after the last press")
# Late start: the three navDowns all due at once must still go out MENU_MIN_GAP apart, so each 200 ms hold is released.
sent_at, sent, last = [], 2, -99.0
for k in range(60):
    now = 30.0 + 0.2 * k
    a_ = run.next_menu_action(script, now, sent, now - last, sent)
    if a_ == "navDown": sent_at.append(now); sent += 1; last = now
    elif a_: break
gaps = [y - x for x, y in zip(sent_at, sent_at[1:])]
check(len(sent_at) == 3 and all(g >= run.MENU_MIN_GAP - 1e-9 and g - 0.2 >= 0.2 - 1e-9 for g in gaps), "late consecutive navDowns keep a release gap: %r" % gaps)
check(run.next_menu_action(script, 18.0, 7, 2.5, 7) == "navRight", "OUTRUN mode is a navRight")
check([a for _, a in script].count("navDown") == 3 and script[-1] == (28.5, "confirm"), "zero's mapped path: three downs, music confirm last")
done = len(script)
check(run.next_menu_action(script, script[-1][0] + run.MENU_FALLBACK_AFTER - 0.1, done, 99, done) is None, "no fallback press before its delay")
check(run.next_menu_action(script, script[-1][0] + run.MENU_FALLBACK_AFTER, done, 99, done) == "start", "the fallback starts with start")
check(run.next_menu_action(script, 60, done, 11.9, done) is None and run.next_menu_action(script, 60, done, 12.0, done) == "start",
      "a late script's last press still gets the whole fallback delay")
check(run.next_menu_action(script, 60, done, 1.0, done + 1) is None, "fallback presses stay 1.5 s apart")
check(run.next_menu_action(script, 60, done, 2.0, done + 1) == "confirm", "then confirm, as before")
check(run.next_menu_action(script, 60, done, 99, 40) is None, "40 presses in all")
check(all("%s = button" % a in run.PROFILE for a in ("navLeft", "navRight")), "the test profile binds navLeft/navRight")
# Route identity (romi 24552): R3 compares only on the first run's course and car.
log = "\n".join(["world source: E:\\Games\\X/Stage/BEAC/*.sz", "retail driving car=0 column=0 params=9880",
                  "course descriptors=66/77 records=15 depth=4 selected=0 copy=1 matrix=1"])
ident = run.route_identity(log)
check(ident == {"stage": "BEAC", "car": "0", "course": "0"}, "identity parsed: %r" % ident)
check(run.route_identity("nothing")["stage"] is None, "an unlogged identity is None")
scripted = dict(presses=[dict(action="confirm", scripted=True)] * 3)
fell_back = dict(presses=[dict(action="confirm", scripted=True), dict(action="start", scripted=False)])
r_a, r_b = run.route_of(scripted, log), run.route_of(scripted, log)
check(r_a["scriptedOnly"] and not run.route_of(fell_back, log)["scriptedOnly"] and not run.route_of(dict(presses=[]), log)["scriptedOnly"], "scriptedOnly")
check(run.same_route(r_a, r_b), "same script and identity: same route")
check(not run.same_route(r_a, run.route_of(fell_back, log)), "a fallback run is not the same route")
check(not run.same_route(r_a, run.route_of(scripted, log.replace("car=0", "car=3"))), "another car is not the same route")
check(not run.same_route(r_a, run.route_of(scripted, "nothing")), "an unlogged identity is not the same route")
check(not run.same_route(None, r_b), "no first route")

# --drive and --render (sana 215675): the race budget follows the drive; the throttle is held across steps within the
# command bounds; straight for 12 s, then right / straight / left / straight; render-test.txt from the spec, strictly.
check(run.race_budget(None, drive=40.0) == 40.0 + run.RACE_MARGIN and run.race_budget(None) == run.R1_RACE_SECONDS + run.RACE_MARGIN,
      "the drive sets the R1 race budget")
check(run.race_budget(25.0, drive=40.0) == 25.0 + run.RACE_MARGIN, "a replay bound is unchanged by --drive")
steps = [run.drive_batch(k) for k in range(14)]
check(all(s[0] == "inject action throttle 1 ms=2500" for s in steps), "the throttle is held across steps (2.5 s holds, 2 s apart)")
check(all(len(s) == 1 for s in steps[:6]), "straight for the first 12 s")
check(steps[6][1:] == ["inject action steer 0.5 ms=1500"] and len(steps[7]) == 1 and steps[8][1:] == ["inject action steer -0.5 ms=1500"]
      and len(steps[9]) == 1 and steps[10][1:] == ["inject action steer 0.5 ms=1500"], "then right, straight, left, straight")
check(all(50 <= int(c.rsplit("ms=", 1)[1]) <= 15000 for s in steps for c in s), "every hold within the 50..15000 ms grammar")
req = run.render_request("screens=separate,views=angled,frames=240,every=3,after=360,scale=2", "abcdefgh12345678")
check(req == b"outrun.render-test@1\nnonce=abcdefgh12345678\nscreens=separate\nviews=angled\nframes=240\nevery=3\nafter=360\nscale=2\n",
      "render-test.txt from the spec: %r" % req)
check(run.render_request("", "n") is None, "no spec: no render-test.txt")
for bad in ("screens=separate,screens=off", "nonce=x", "folder=C:\\x", "views", "frames=", "views=angled;x", "frames=1 0"):
    try: run.render_request(bad, "n"); check(False, "refused: %r" % bad)
    except ValueError: pass
check(run.render_span("screens=separate,views=angled") and run.render_span("frames=3,screens=surround") and not run.render_span("screens=off")
      and not run.render_span(""), "a span only for surround or separate")

print("run timing: %d failure(s)" % len(failures))
for f in failures: print("  " + f)
sys.exit(1 if failures else 0)
