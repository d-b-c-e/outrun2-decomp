"""Export the existing Producer2 fixture and summarize comparable software requests.

Run from any directory. Outputs must be a new directory under work/ffb-normalization.
No game, device, settings, network or DLL is used.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
DATA = Path(__file__).resolve().parent
PINS = {
    "tools/replay/force_normalization.py": "35b86c60e2dc2880735b054752e73658d3db6617a97dd6aec6ec713f8fa5f91c",
    "tools/replay/replay_case.py": "f85bd591ea5565b4234ebdcab42222f07f52cd1485ddc9bac886f029f93e1b35",
    "native/forcemodel/force_model.h": "c7f3c3ff9c6d1475b88015643afb1b515d3f6f97bcc46e1fe2082f26ded1b16f",
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def entry(path, output):
    import os
    return {"path": Path(os.path.relpath(path, output)).as_posix(), "sha256": sha(path)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--toolkit", type=Path, default=Path("E:/Source/toolkits/dbce-wheel-mod-toolkit"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "work/ffb-normalization"):
        raise ValueError("output must be under repository work/ffb-normalization")
    for name, expected in PINS.items():
        if sha(args.toolkit / name) != expected:
            raise ValueError("toolkit bytes changed: " + name)
    model = ROOT / "windows/vendor/native/forcemodel/force_model.h"
    if sha(model) != PINS["native/forcemodel/force_model.h"]:
        raise ValueError("production vendored model changed")
    reference = DATA / "art-of-rally-50-v2.json"
    if sha(reference) != "54fcf9db577b559bdb26638b370f38958318f4a414dde6043e96aa8a22b23433":
        raise ValueError("fixed Art v2 bytes changed")
    profile = json.loads(reference.read_bytes())
    assert profile["sourceCommit"] == "8bc94b1f54cfb490fbbb37191727cdf7eee0edf8"
    assert profile["steering"]["referenceNewtons"] == 19166.666015625
    assert profile["steering"]["gainAt50"] == 1
    output.mkdir(parents=True, exist_ok=False)
    candidate = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    fixture = args.fixture.resolve()
    source = output / "production-requests.csv"
    result = subprocess.run([str(fixture), "--normalization-csv", str(source)],
                            cwd=ROOT, capture_output=True, text=True)
    (output / "fixture-original.txt").write_text(result.stdout + result.stderr, encoding="utf-8")
    result.check_returncode()
    with source.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    assert len(rows) == 41400 and len({r["scenario"] for r in rows}) == 100
    artifacts = [entry(ROOT / "windows/tests/test_ffb_producer2.cpp", output), entry(model, output),
                 entry(ROOT / "windows/source/ffb_producer2.cpp", output), entry(fixture, output)]
    manifest = {"schema": "dbce.ffb-normalization", "version": 1,
                "reference": entry(reference, output), "series": []}
    summary = []
    for steering in (0, 50, 100):
        for shift in (0, 50, 100):
            for event in (False, True):
                selected = [r for r in rows if int(r["steering_percent"]) == steering and
                            int(r["shift_percent"]) == shift and (int(r["scenario"]) % 2 == 0) == event]
                name = f"steering-{steering}-shift-{shift}-{'shift-holds' if event else 'steady'}"
                path = output / (name + ".csv")
                with path.open("w", newline="", encoding="utf-8") as stream:
                    writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                    writer.writeheader()
                    writer.writerows(selected)
                peak = max(abs(float(r["shaped_force"])) for r in selected)
                summary.append({"id": name, "rows": len(selected), "peak": peak,
                                "minimumHeadroom": 1 - peak,
                                "atUnitRows": sum(abs(float(r["shaped_force"])) >= .9999 for r in selected)})
                manifest["series"].append({
                    "id": name, "quantity": "constant-force-request", "evidence": "synthetic",
                    "game": "OutRun", "model": "outrun-producer@2", "format": "csv",
                    "settings": {"steeringPercent": steering, "shiftPercent": shift,
                                 "springAt50": .30, "springFullSpeedMps": 30, "shiftAt50": .05,
                                 "shiftHalfSeconds": .04, "shaperStrength": 50, "invert": False},
                    "limitations": "Synthetic production centering/shift holds at 60 and 120 Hz; no tyre force, load, slip, impacts, landing or route workload. Shift is a constant-request double pulse, not Art's periodic amplitude. Missing Art steering trace counterpart; no physical equality or gain recommendation.",
                    "validation": "Existing production fixture asserts signed/zero steering, independent gains, headroom, expiry, unavailable inputs and nominal DirectInput conversion; candidate " + candidate,
                    "artifacts": artifacts, "source": entry(path, output), "cap": 1,
                    "maxGapSeconds": .1,
                    "mapping": {"value": "shaped_force", "time": "time", "valid": "valid",
                                "epoch": "epoch", "speed": "hud_kmh_1f8"}})
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    provenance = {"candidateCommit": candidate, "toolkitObservedCommit": subprocess.check_output(
        ["git", "-C", str(args.toolkit), "rev-parse", "HEAD"], text=True).strip(),
        "vendoredModelOriginCommit": "fb710dffa29f707d296f1f579851e23d549a8b51",
        "toolkitSha256": PINS, "fixture": entry(fixture, output), "rawExport": entry(source, output),
        "historicalArtV1": entry(DATA / "art-of-rally-50-v1.json", output),
        "physicalAcceptance": False, "summary": summary}
    (output / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    result = subprocess.run([sys.executable, "-B", str(args.toolkit / "tools/replay/force_normalization.py"),
                             str(output / "manifest.json"), str(output / "analysis")], capture_output=True, text=True)
    (output / "analyzer-original.txt").write_text(result.stdout + result.stderr, encoding="utf-8")
    result.check_returncode()
    report = json.loads((output / "analysis/report.json").read_text(encoding="utf-8"))
    assert len(report["series"]) == 18 and report["physicalAcceptance"] is False
    for series in report["series"]:
        assert series["bands"]["all"]["atUnitFraction"] == 0
        assert all(series["bands"][band]["seconds"] > 0 for band in
                   ("0-5 km/h", "5-20 km/h", "20-60 km/h", "60+ km/h"))
    print(result.stdout.strip())
    print("PASS: production fixture, 18 request series, fixed Art v2 identity and four speed bands")


if __name__ == "__main__":
    main()
