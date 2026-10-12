"""Synthetic cases for compare_consumed.py (sana 35598): each defect a malformed stream could hide is reported as invalid
evidence, never as a pass; a real divergence is valid evidence that did not follow.

    py -3 -B test_compare_consumed.py
"""
import copy, json, math, pathlib, sys, tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import compare_consumed as cc

FAIL = []
def check(ok, what):
    if not ok: FAIL.append(what); print("FAIL:", what)

def row(i, played=None):
    c = {"gameInput.mode": 32 if i < 3 else 16, "gameInput.gear": 1 + i % 3, "gameInput.speed": 10.0 * i, "gameInput.consumedSteer": i - 2,
         "gameInput.consumedAccel": 3 * i, "gameInput.consumedBrake": 0, "final.steering": i - 5, "final.pcDrive": i % 2, "final.menuConfirm": int(i == 1)}
    if played is not None: c["replay.tapeRow"] = played
    return c

def write(path, rows, completed=True, dropped=0, accepted_extra=0, contention=0):
    with open(path, "w", encoding="utf-8") as f:
        f.write(json.dumps({"kind": "metadata"}) + "\n")
        for c in rows: f.write(json.dumps({"kind": "sample", "sample": {"channels": c}}) + "\n")
        counts = {k: 0 for k in cc.COUNTS_ZERO}; counts["droppedSamples"] = dropped; counts["writtenSamples"] = len(rows)
        counts["acceptedSamples"] = len(rows) + accepted_extra; counts["contentionCount"] = contention
        f.write(json.dumps({"kind": "footer", "footer": {"completed": completed, "counts": counts}}) + "\n")

def run(ref, rep, **kw):
    with tempfile.TemporaryDirectory() as t:
        a, b = pathlib.Path(t) / "ref.jsonl", pathlib.Path(t) / "rep.jsonl"
        write(a, ref, **kw.get("ref_footer", {})); write(b, rep, **kw.get("rep_footer", {}))
        return cc.compare(a, b)

N = 8
REF = [row(i) for i in range(N)]
REP = [row(i, i) for i in range(N)] + [dict(row(N - 1), **{"replay.tapeRow": -1}) for _ in range(3)]   # then the neutral tail

r = run(REF, REP)
check(r["valid"] and r["followed"] and r["playedRows"] == N and r["fedEqualRows"] == N and r["referenceRaceRow"] == 3, "a faithful replay: valid and followed (%s)" % r["problems"])

def invalid(name, ref, rep, **kw):
    r = run(ref, rep, **kw)
    check(not r["valid"] and not r["followed"], "%s: invalid evidence, not a pass (%s)" % (name, r["problems"]))

rep = copy.deepcopy(REP); del rep[4]["gameInput.speed"]; invalid("a missing game channel", REF, rep)
rep = copy.deepcopy(REP); rep[4]["gameInput.speed"] = None; invalid("a null game channel", REF, rep)
rep = copy.deepcopy(REP); rep[4]["gameInput.speed"] = math.nan; invalid("a NaN game channel", REF, rep)
rep = copy.deepcopy(REP); rep[4]["gameInput.gear"] = "3"; invalid("a string game channel", REF, rep)
ref = copy.deepcopy(REF); del ref[2]["final.steering"]; invalid("a missing fed channel in the reference", ref, REP)
rep = copy.deepcopy(REP); rep.insert(3, row(2, 2)); invalid("a duplicate tapeRow", REF, rep)
rep = copy.deepcopy(REP); rep[3]["replay.tapeRow"] = 3.5; invalid("a fractional tapeRow", REF, rep)
rep = copy.deepcopy(REP); rep[3]["replay.tapeRow"] = True; invalid("a boolean tapeRow", REF, rep)
rep = copy.deepcopy(REP); rep.insert(2, dict(row(1), **{"replay.tapeRow": -1})); invalid("a neutral row before the played tail", REF, rep)
rep = copy.deepcopy(REP); del rep[5]; invalid("a skipped tape row", REF, rep)
invalid("not all rows played", REF, REP[:N - 2])
invalid("an empty reference", [], REP)
invalid("empty streams", [], [])
invalid("an incomplete replay footer", REF, REP, rep_footer={"completed": False})
invalid("a dropped reference sample", REF, REP, ref_footer={"dropped": 1})
invalid("a footer contention count", REF, REP, rep_footer={"contention": 1})
invalid("accepted samples not all written", REF, REP, ref_footer={"accepted_extra": 1})
ref = [{k: v for k, v in c.items() if k != "final.pcDrive"} for c in REF]; rep = [{k: v for k, v in c.items() if k != "final.pcDrive"} for c in REP]
invalid("a reference recorded before the replay seam", ref, rep)

# A real divergence: valid evidence that did not follow, named exactly.
rep = copy.deepcopy(REP); rep[5]["gameInput.speed"] += 1.0
r = run(REF, rep)
check(r["valid"] and not r["followed"] and r["firstDivergence"]["row"] == 5 and r["firstDivergence"]["channel"] == "gameInput.speed", "a speed divergence at row 5 (%s)" % r["firstDivergence"])
rep = copy.deepcopy(REP); rep[6]["gameInput.speed"] += 0.005
check(run(REF, rep)["followed"], "a speed difference inside 0.01 km/h still follows")
rep = copy.deepcopy(REP); rep[2]["final.steering"] = 99
r = run(REF, rep)
check(r["valid"] and r["fedEqualRows"] == N - 1 and r["fedFirstDifference"]["row"] == 2, "a fed-word difference is reported as plumbing (%s)" % r["fedFirstDifference"])

# The pose observer: compared when the reference has it, with admission (racing rows, motion); input-only otherwise.
check(not run(REF, REP)["pose"]["compared"] and "input and startup only" in run(REF, REP)["pose"]["scope"], "a reference without pose stays input-only")
def posed(c, i):
    c = dict(c); racing = c["gameInput.mode"] == 16
    c.update({"gameInput.poseValid": int(racing), "gameInput.posX": 100.0 + 2.5 * i if racing else 0.0, "gameInput.posY": 3.0 if racing else 0.0,
              "gameInput.posZ": -7.0 * i if racing else 0.0, "gameInput.vec20X": 0.1 * i, "gameInput.vec20Y": 0.0, "gameInput.vec20Z": 1.0,
              "gameInput.carModel": 4 if racing else 0, "gameInput.stageKey": 17 if racing else 0, "gameInput.courseGate": i // 4})
    for r in range(3):
        for k in range(3): c["gameInput.rot%d%d" % (r, k)] = (1.0 if r == k else 0.0) if racing else 0.0
    return c
PREF = [posed(c, i) for i, c in enumerate(REF)]; PREP = [posed(c, min(i, N - 1)) for i, c in enumerate(REP)]
r = run(PREF, PREP)
check(r["valid"] and r["followed"] and r["pose"]["compared"] and r["pose"]["racingRows"] == N - 3 and r["pose"]["pathLength"] > 0, "a posed replay: valid, followed, pose compared (%s)" % r["pose"])
rep = copy.deepcopy(PREP); rep[6]["gameInput.posX"] += 0.01
r = run(PREF, rep); check(r["valid"] and not r["followed"] and r["firstDivergence"]["channel"] == "gameInput.posX" and r["firstDivergence"]["row"] == 6, "a position divergence is named (%s)" % r["firstDivergence"])
rep = copy.deepcopy(PREP); rep[7]["gameInput.rot01"] = 0.001
r = run(PREF, rep); check(not r["followed"] and r["firstDivergence"]["channel"] == "gameInput.rot01", "a rotation divergence is named")
rep = copy.deepcopy(PREP); rep[4]["gameInput.stageKey"] = 18
r = run(PREF, rep); check(not r["followed"] and r["firstDivergence"]["channel"] == "gameInput.stageKey", "another stage key is a divergence")
rep = copy.deepcopy(PREP); del rep[5]["gameInput.vec20Z"]; invalid("a missing pose channel in the replay", PREF, rep)
invalid("no racing rows with a valid pose", [dict(c, **{"gameInput.poseValid": 0}) for c in PREF], [dict(c, **{"gameInput.poseValid": 0}) for c in PREP])
still = [dict(c, **{"gameInput.posX": 1.0, "gameInput.posZ": 2.0}) for c in PREF]
invalid("a car that never moved", still, [dict(c, **{"gameInput.posX": 1.0, "gameInput.posZ": 2.0}) for c in PREP])

print("compare_consumed: %d failure(s)" % len(FAIL))
sys.exit(1 if FAIL else 0)
