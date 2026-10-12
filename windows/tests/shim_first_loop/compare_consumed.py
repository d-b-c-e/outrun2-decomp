"""Consumed playback, the smallest proof (consumed_replay.hpp; sana 35159/35598): a reference ordinary recording against
the latched replay run that played its export, row by row (the replay row r is the run's row whose replay.tapeRow is r).

  py -3 -B compare_consumed.py <reference capture\\source.jsonl> <replay run capture\\source.jsonl>

Two verdicts, reported apart. "valid": the evidence is what it claims to be. Both streams end in a completed footer with
nothing dropped or in error; the reference has rows; every compared row has every GAME and final.* channel, present, numeric
and finite; the replay's tapeRow values are exact integers 0..N-1, in order, each once, with -1 (the seam's neutral) only
after the played tail; and all N reference rows were played. "followed": valid, and the game's channels agree on every
played row (speed within 0.01 km/h); otherwise the first divergence exactly (row, channel, both values). The fed final.*
words are compared too, as plumbing only. This is an input and startup proof: route fidelity needs pose and heading.
"""
import json, math, sys

GAME = ("gameInput.mode", "gameInput.gear", "gameInput.speed", "gameInput.consumedSteer", "gameInput.consumedAccel", "gameInput.consumedBrake")
TOL = {"gameInput.speed": 0.01}
COUNTS_ZERO = ("droppedSamples", "droppedMarkers", "queueFullCount", "invalidCount", "limitCount", "errorCount", "rejectedAfterStop")

def stream(path, problems, name):
    rows, footer = [], None
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f):
            try: j = json.loads(line)
            except ValueError: problems.append("%s: line %d is not JSON" % (name, n + 1)); continue
            if footer is not None: problems.append("%s: a record after the footer" % name); break
            if j.get("kind") == "sample": rows.append(j["sample"]["channels"])
            elif j.get("kind") == "footer": footer = j.get("footer") or {}
    if footer is None: problems.append("%s: no footer (the stream did not end)" % name)
    else:
        if footer.get("completed") is not True: problems.append("%s: the footer is not completed" % name)
        counts = footer.get("counts") or {}
        bad = {k: counts.get(k) for k in COUNTS_ZERO if counts.get(k) != 0}
        if bad: problems.append("%s: footer counts not zero: %s" % (name, json.dumps(bad)))
        if counts.get("writtenSamples") != len(rows): problems.append("%s: %s samples written, %d read" % (name, counts.get("writtenSamples"), len(rows)))
    return rows

def number(c, k):
    v = c.get(k)
    return v if isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v) else None

def modes(rows):
    seq = []
    for i, c in enumerate(rows):
        m = number(c, "gameInput.mode")
        if not seq or seq[-1][1] != m: seq.append((i, m))
    return seq

def compare(ref_path, rep_path):
    problems = []
    ref = stream(ref_path, problems, "reference"); rep = stream(rep_path, problems, "replay")
    if not ref: problems.append("the reference has no rows")
    fed = sorted(k for k in ref[0] if k.startswith("final.")) if ref else []
    if ref and "final.pcDrive" not in fed: problems.append("the reference has no final.pcDrive (recorded before the replay seam)")
    # tapeRow: exact integers 0..N-1 in order, each once; -1 (neutral) only after the last played row.
    played, tail, expected = [], False, 0
    for i, c in enumerate(rep):
        r = number(c, "replay.tapeRow")
        if r is None or r != int(r): problems.append("replay row %d: tapeRow %r is not an exact integer" % (i, c.get("replay.tapeRow"))); break
        r = int(r)
        if r == -1: tail = True; continue
        if tail: problems.append("replay row %d: tape row %d after the neutral tail began" % (i, r)); break
        if r != expected: problems.append("replay row %d: tape row %d, expected %d (in order, each once)" % (i, r, expected)); break
        played.append(c); expected += 1
    if ref and len(played) != len(ref): problems.append("%d of %d reference rows played" % (len(played), len(ref)))
    out = dict(referenceRows=len(ref), replayRows=len(rep), playedRows=len(played), fedChannels=len(fed), valid=False, problems=problems,
               fedEqualRows=0, fedFirstDifference=None, followed=False, firstDivergence=None,
               referenceModes=modes(ref), replayModes=modes(played))
    for r in range(min(len(ref), len(played))):
        a, b = ref[r], played[r]
        missing = [k for k in GAME + tuple(fed) if number(a, k) is None or number(b, k) is None]
        if missing: problems.append("row %d: missing or non-finite %s" % (r, ", ".join(missing[:4]))); break
        if all(a[k] == b[k] for k in fed): out["fedEqualRows"] += 1
        elif out["fedFirstDifference"] is None:
            k = next(k for k in fed if a[k] != b[k]); out["fedFirstDifference"] = dict(row=r, channel=k, reference=a[k], replay=b[k])
        if out["firstDivergence"] is None:
            for k in GAME:
                if abs(a[k] - b[k]) > TOL.get(k, 0):
                    out["firstDivergence"] = dict(row=r, channel=k, reference=a[k], replay=b[k], referenceMode=a["gameInput.mode"], replayMode=b["gameInput.mode"]); break
    out["valid"] = not problems
    out["followed"] = out["valid"] and out["firstDivergence"] is None
    race = lambda rows: next((i for i, c in enumerate(rows) if number(c, "gameInput.mode") in (16, 18)), None)
    out["referenceRaceRow"] = race(ref); out["replayRaceRow"] = race(played)
    return out

if __name__ == "__main__":
    print(json.dumps(compare(sys.argv[1], sys.argv[2]), indent=1))
    sys.exit(0)
