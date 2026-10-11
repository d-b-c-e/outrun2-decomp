"""R3 report (Astra 16754): a replay run's played rows against the first run's tape. Adapter/action replay at the adapter
boundary; no deterministic route claim.

    py -3 -B compare_replay.py <first run validated.tsv> <replay run validated.tsv>

The tape is selected by the same rule as source/outrun_replay.hpp: from the first row in gameplay with the run Running,
through the last consecutive Running row. Each replay row with replay.tapeRow >= 0 is compared with that tape row:
- intended: the adapter's record words (steer/accel/brake/buttons) the replay wrote against those of the first run;
- consumed: what the game consumed (the adapter's consumed words, one tick later) against the first run's, and within
  the replay run against what its adapter wrote the tick before;
- progression: race speed and modes over the played rows in both runs.
Prints one JSON object; exit 0 when the comparison could be made (the numbers are the result, not a gate).
"""
import json, sys

RUNNING = 3   # dbce::shim::Run::Running (None, Latched, Waiting, Running, ...)
WORDS = ("recordSteer", "recordAccel", "recordBrake", "recordButtons")
CONSUMED = ("consumedSteer", "consumedAccel", "consumedBrake")
KEEP = {"run", "replay.tapeRow", "adapter.inGame", "gameInput.speed", "gameInput.mode"} | {"adapter." + k for k in WORDS + CONSUMED}


def rows(path):
    """The validated export, streamed: per row only the channels this report reads."""
    with open(path, encoding="ascii") as f:
        row, left = None, 0
        for line in f:
            if left == 0:
                tag, n = line.split()
                if tag != "ROW": raise SystemExit("export row header expected: %r" % line[:40])
                row, left = {}, int(n)
                continue
            key, value = line.split()
            if key in KEEP: row[key] = float(value)
            left -= 1
            if left == 0: yield row
        if left: raise SystemExit("truncated export")


def tape(path):
    out, started = [], False
    for r in rows(path):
        running = r["run"] == RUNNING
        if not started:
            if not (r["adapter.inGame"] and running): continue
            started = True
        if not running: break
        out.append(r)
    return out


def main():
    first, replay = tape(sys.argv[1]), list(rows(sys.argv[2]))
    if not first: raise SystemExit("the first run has no tape (no race rows with the run Running)")
    played = [(i, r) for i, r in enumerate(replay) if r.get("replay.tapeRow", -1) >= 0]
    if not played: raise SystemExit("the replay run played no tape row")
    rows_seen = [int(r["replay.tapeRow"]) for _, r in played]
    report = {"tapeRows": len(first), "playedRows": len(played), "contiguous": rows_seen == list(range(len(played))),
              "intended": {}, "consumedVsFirst": {}, "consumedVsOwnRecord": {}}
    for w in WORDS:
        same = sum(1 for _, r in played if r["adapter." + w] == first[int(r["replay.tapeRow"])]["adapter." + w])
        report["intended"][w] = "%d/%d" % (same, len(played))
    # Consumed words describe the previous tick: row i+1's consumed against row i's record (both runs alike).
    for c, w in zip(CONSUMED, WORDS):
        pairs = [(i, r) for i, r in played if i + 1 < len(replay) and int(r["replay.tapeRow"]) + 1 < len(first)]
        vs_first = sum(1 for i, r in pairs if replay[i + 1]["adapter." + c] == first[int(r["replay.tapeRow"]) + 1]["adapter." + c])
        vs_own = sum(1 for i, r in pairs if replay[i + 1]["adapter." + c] == r["adapter." + w])
        report["consumedVsFirst"][c] = "%d/%d" % (vs_first, len(pairs))
        report["consumedVsOwnRecord"][c] = "%d/%d" % (vs_own, len(pairs))
    speeds = [r["gameInput.speed"] for _, r in played]
    first_speeds = [first[int(r["replay.tapeRow"])]["gameInput.speed"] for _, r in played]
    report["progression"] = {"replayMaxSpeed": max(speeds), "replayFinalSpeed": speeds[-1], "firstMaxSpeed": max(first_speeds),
                             "firstFinalSpeed": first_speeds[-1], "replayModes": sorted({int(r["gameInput.mode"]) for _, r in played}),
                             "replayInGameRows": sum(1 for _, r in played if r["adapter.inGame"])}
    print(json.dumps(report))


if __name__ == "__main__":
    main()
