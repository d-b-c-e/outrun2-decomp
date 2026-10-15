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
# The evidence: the LAST race sound line, the loop and the stop read separately (refused counts as the command path too).
log = "x\nrace sound inits=1 controls=9 played: 51x2 refused:\nrace sound inits=1 controls=99 played: 51x3 9bcx40 refused: 81bcx1\n"
ev = run.horn_evidence(log)
check(ev["loop"] and ev["stop"] and ev["line"].endswith("81bcx1"), "the last race sound line names both: %r" % ev)
ev = run.horn_evidence("race sound inits=1 played: 9bcx3 refused:\n")
check(ev["loop"] and not ev["stop"], "a loop without its stop: %r" % ev)
ev = run.horn_evidence("race sound inits=1 played: 19bcx3 381bcx1\n")
check(not ev["loop"] and not ev["stop"], "other ids that merely end in 9bc / 81bc are not the horn: %r" % ev)
check(run.horn_evidence("nothing")["line"] is None, "no race sound line: no evidence")
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
check(run.next_menu_action(script, None, 0, 99, 0) is None, "no press before the game window appears")
check(run.next_menu_action(script, 5.9, 0, 99, 0) is None and run.next_menu_action(script, 6.0, 0, 99, 0) == "confirm", "the first step at its time")
check(run.next_menu_action(script, 30.0, 2, 0.4, 2) == "navDown", "a late poll sends the next unsent step, not a later one")
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
check(run.next_menu_action(script, 18.0, 7, 0.5, 7) == "navRight", "OUTRUN mode is a navRight")
check([a for _, a in script].count("navDown") == 3 and script[-1] == (28.5, "confirm"), "zero's mapped path: three downs, music confirm last")
done = len(script)
check(run.next_menu_action(script, script[-1][0] + run.MENU_FALLBACK_AFTER - 0.1, done, 99, done) is None, "no fallback press before its delay")
check(run.next_menu_action(script, script[-1][0] + run.MENU_FALLBACK_AFTER, done, 99, done) == "start", "the fallback starts with start")
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

print("run timing: %d failure(s)" % len(failures))
for f in failures: print("  " + f)
sys.exit(1 if failures else 0)
