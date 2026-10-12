"""Measures the software force stream of a recorded session against its replay (romi 28613: measured, not assumed).

    py -3 -B compare_ffb_streams.py <r1 run folder> <r3 run folder> [--out stream-compare.json] [--partial]

Each latched run writes ffb-trace.csv (source/ffb_live) into its run folder. Rows are aligned by race epoch and race
tick (GAME ticks from the race start), the replay's tape starting at the first race frame. Reports, over the ticks both
runs have: how many have identical car inputs (the model's own input columns, as written at %.9g), how many an identical
held command and delivery intent, the first tick that differs in any of them and the largest differences.

Both traces must be the same trace version and carry every column that version defines (romi 29014): trace 3 has the
base inputs, trace 4 adds the stage. A missing column is refused, not skipped. A trace 3 / trace 4 pair is refused
unless --partial is given; it is then compared on the base inputs only, labelled partial in the report, and never passes.

Exit 0 when every common tick of the replayed span is identical in inputs, held and intent with the same settings;
1 when anything differs; 2 on unusable input; 3 for a --partial pair that is identical on what it could compare.
"""
import csv, json, pathlib, re, sys

KEYS = ("mode", "epoch", "race_tick", "held", "intent")
BASE = ("speed_1c4", "steer_1d0", "steer_rate_1d4", "lateral_264", "lateral_268", "gear_208", "state_8", "pedal_34", "roughness", "water")
INPUTS = {3: BASE, 4: BASE + ("stage",)}


def load(folder):
    p = pathlib.Path(folder) / "ffb-trace.csv"
    text = p.read_text(encoding="utf-8").splitlines()
    head = [l for l in text if l.startswith("#")]
    lines = [l for l in text if not l.startswith("#")]
    m = re.match(r"# dbce\.outrun-decomp\.ffb-trace,(\d+),", head[0]) if head else None
    if not m: raise ValueError("%s: no force trace header" % p)
    version = int(m.group(1))
    if version not in INPUTS: raise ValueError("%s: trace version %d is not one this tool knows (%s)" % (p, version, sorted(INPUTS)))
    if not lines: raise ValueError("%s: no column row" % p)
    columns = next(csv.reader([lines[0]]))
    missing = [c for c in KEYS + INPUTS[version] if c not in columns]
    if missing: raise ValueError("%s: trace %d without column(s) %s" % (p, version, ", ".join(missing)))
    rows = {}
    for r in csv.DictReader(lines):
        rows[(int(r["epoch"]), int(r["race_tick"]))] = r
    return version, head, rows


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    out = sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else None
    if out in args: args.remove(out)
    partial = "--partial" in sys.argv
    if len(args) != 2: print(__doc__); return 2
    try: (v1, h1, a), (v3, h3, b) = load(args[0]), load(args[1])
    except (OSError, ValueError) as e: print("unusable: %s" % e); return 2
    if v1 != v3 and not partial: print("unusable: trace %d against trace %d (--partial compares the base inputs only)" % (v1, v3)); return 2
    cols = INPUTS[min(v1, v3)]
    omitted = sorted(set(INPUTS[max(v1, v3)]) - set(cols))
    # the race the replay drives is the first epoch in both (one race per first-loop run)
    common = sorted(k for k in a if k in b and k[0] == 1)
    if not common: print("no common race ticks"); return 2
    same_in = same_held = same_intent = 0; first = None; worst = {}
    for k in common:
        x, y = a[k], b[k]
        ins = all(x[c] == y[c] for c in cols)
        held, intent = x["held"] == y["held"], x["intent"] == y["intent"]
        same_in += ins; same_held += held; same_intent += intent
        if not (ins and held and intent) and first is None:
            first = dict(epoch=k[0], raceTick=k[1], differs=[c for c in cols + ("held", "intent") if x[c] != y[c]], r1=dict(x), r3=dict(y))
        for c in cols + ("held", "intent"):
            try: d = abs(float(x[c]) - float(y[c]))
            except ValueError: continue
            worst[c] = max(worst.get(c, 0.0), d)
    report = dict(traceVersions=[v1, v3], partial=v1 != v3, omittedColumns=omitted,
                  r1Rows=sum(1 for k in a if k[0] == 1), r3Rows=sum(1 for k in b if k[0] == 1), commonTicks=len(common),
                  identicalInputs=same_in, identicalHeld=same_held, identicalIntent=same_intent,
                  maxAbsDifference={c: v for c, v in worst.items() if v}, firstDifference=first,
                  sameSettings=[l for l in h1 if l.startswith("# FFB")] == [l for l in h3 if l.startswith("# FFB")])
    text = json.dumps(report, indent=1)
    if out: pathlib.Path(out).write_text(text, encoding="utf-8")
    print(text)
    identical = same_in == same_held == same_intent == len(common) and report["sameSettings"]
    if not identical: return 1
    return 3 if report["partial"] else 0


if __name__ == "__main__":
    sys.exit(main())
