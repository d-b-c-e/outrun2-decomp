"""Measures the software force stream of a recorded session against its replay (romi 28613: measured, not assumed).

    py -3 -B compare_ffb_streams.py <r1 run folder> <r3 run folder> [--out stream-compare.json]

Each latched run writes ffb-trace.csv (source/ffb_live, trace v3) into its run folder. Rows are aligned by race epoch and
race tick (GAME ticks from the race start), the replay's tape starting at the first race frame. Reports, over the
ticks both runs have: how many have identical car inputs (the model's own input columns, as written at %.9g), how many
an identical held command and delivery intent, the first tick that differs and the largest differences. Exit 0 when
every common tick of the replayed span is identical, 1 otherwise, 2 on unusable input.
"""
import csv, json, pathlib, sys

INPUTS = ("speed_1c4", "steer_1d0", "steer_rate_1d4", "lateral_264", "lateral_268", "gear_208", "state_8", "pedal_34", "roughness", "water", "stage")


def load(folder):
    p = pathlib.Path(folder) / "ffb-trace.csv"
    lines = [l for l in p.read_text(encoding="utf-8").splitlines() if not l.startswith("#")]
    if not lines or not lines[0].startswith("mode,epoch,race_tick"): raise ValueError("%s: not a v3 force trace" % p)
    head = [l for l in p.read_text(encoding="utf-8").splitlines() if l.startswith("#")]
    rows = {}
    for r in csv.DictReader(lines):
        rows[(int(r["epoch"]), int(r["race_tick"]) )] = r
    return head, rows


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    out = sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else None
    if out in args: args.remove(out)
    if len(args) != 2: print(__doc__); return 2
    try: (h1, a), (h3, b) = load(args[0]), load(args[1])
    except (OSError, ValueError) as e: print("unusable: %s" % e); return 2
    # the race the replay drives is the first epoch in both (one race per first-loop run)
    common = sorted(k for k in a if k in b and k[0] == 1)
    if not common: print("no common race ticks"); return 2
    cols = [c for c in INPUTS if c in a[common[0]] and c in b[common[0]]]   # v4 adds the stage; a v3 pair has none
    same_in = same_held = same_intent = 0; first = None; worst = {}
    for k in common:
        x, y = a[k], b[k]
        ins = all(x[c] == y[c] for c in cols)
        same_in += ins; same_held += x["held"] == y["held"]; same_intent += x["intent"] == y["intent"]
        if (not ins or x["held"] != y["held"]) and first is None: first = dict(epoch=k[0], raceTick=k[1], r1=dict(x), r3=dict(y))
        for c in tuple(cols) + ("held", "intent"):
            try: d = abs(float(x[c]) - float(y[c]))
            except ValueError: continue
            worst[c] = max(worst.get(c, 0.0), d)
    report = dict(r1Rows=sum(1 for k in a if k[0] == 1), r3Rows=sum(1 for k in b if k[0] == 1), commonTicks=len(common),
                  identicalInputs=same_in, identicalHeld=same_held, identicalIntent=same_intent,
                  maxAbsDifference={c: v for c, v in worst.items() if v}, firstDifference=first,
                  sameSettings=[l for l in h1 if l.startswith("# FFB")] == [l for l in h3 if l.startswith("# FFB")])
    text = json.dumps(report, indent=1)
    if out: pathlib.Path(out).write_text(text, encoding="utf-8")
    print(text)
    return 0 if same_in == same_held == len(common) and report["sameSettings"] else 1


if __name__ == "__main__":
    sys.exit(main())
