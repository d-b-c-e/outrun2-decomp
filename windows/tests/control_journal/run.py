"""Offline control-journal check (CONTROL-JOURNAL.md section 8): build, write every case into a fresh folder, read
each journal with the strict reader, compare with the case's expectation. Disposable files only; no game or device."""
import argparse, hashlib, json, pathlib, subprocess, sys
p = argparse.ArgumentParser(description=__doc__); p.add_argument("result", type=pathlib.Path); args = p.parse_args()
here = pathlib.Path(__file__).resolve().parent; repo = here.parents[2]; out = args.result.resolve()
if out.exists(): raise SystemExit("Use a fresh result directory")
sys.path.insert(0, str(here)); import read_journal
def git(*a): return subprocess.check_output(["git", "-C", str(repo), *a], text=True).strip()
source = dict(commit=git("rev-parse", "HEAD"), dirty=bool(git("status", "--porcelain")))
out.mkdir(parents=True)
def run(name, cmd):
    q = subprocess.run(list(map(str, cmd)), capture_output=True, text=True, timeout=300)
    (out / (name + ".log")).write_text(q.stdout + q.stderr)
    if q.returncode != 0: raise RuntimeError(name + " failed: " + (q.stdout + q.stderr)[-1800:])
    return q
run("configure", ["cmake", "-S", here, "-B", out / "build", "-G", "Visual Studio 17 2022", "-A", "x64", "-T", "ClangCL"])
run("build", ["cmake", "--build", out / "build", "--config", "Release"])
proof = run("cases", [out / "build/Release/control_journal.exe", out / "files"])
cases = json.loads((out / "files/cases.json").read_text())
results = []
for c in cases:
    v = read_journal.read(c["file"]); bad = []
    for k in ("qualifies", "terminal", "devices", "incomplete", "torn", "endReason", "terminalDetail", "events", "statuses", "emptySkipped"):
        if k in c and v.get(k) != c[k]: bad.append("%s: expected %r, read %r" % (k, c[k], v.get(k)))
    if "reasons" in c and v["reasons"][:len(c["reasons"])] != c["reasons"] and v["reasons"][-len(c["reasons"]):] != c["reasons"]:
        if not all(r in v["reasons"] for r in c["reasons"]): bad.append("escaped strings did not round trip")
    if "deviceName" in c and v["deviceNames"][:1] != [c["deviceName"]]: bad.append("device name did not round trip")
    if c.get("qualifies") and v["errors"]: bad.append("reader errors: %s" % v["errors"])
    if not c.get("incomplete") and c["case"] not in ("escapes", "escapes-name", "full-queue", "empty", "tail") and v["errors"]: bad.append("reader errors: %s" % v["errors"])
    if c["case"] == "roundtrip" and "recorderOverruns" not in v["unavailable"]: bad.append("unavailable counters not reported")
    results.append(dict(case=c["case"], verdict=v, problems=bad))
# The reader itself refuses a drifted identity and a seq gap (copies of the round trip).
rt = next(c["file"] for c in cases if c["case"] == "roundtrip"); text = pathlib.Path(rt).read_bytes().split(b"\n")
drift = out / "drift.jsonl"; seqgap = out / "seqgap.jsonl"
d = json.loads(text[2]); d["pid"] = d["pid"] + 1; drift.write_bytes(b"\n".join(text[:2] + [json.dumps(d).encode()] + text[3:]))
seqgap.write_bytes(b"\n".join(text[:2] + text[3:]))
# An event with an epoch that never began (Astra 16397): inserted before the end, seq and end counts made valid.
rows = [json.loads(x) for x in text if x]; ev = dict(next(x for x in rows if x.get("type") == "event")); end = rows[-1]
ev.update(kindName="epoch-end", kind=1, device=0, epoch=999, detail="unknown epoch"); body = rows[:-1] + [ev]
for i, x in enumerate(body): x["seq"] = i
lines = [json.dumps(x, separators=(",", ":")).encode() for x in body]
end = dict(end, seq=len(body), lines=len(body) + 1, bytes=sum(len(x) + 1 for x in lines))
unknown = out / "unknown-event-epoch.jsonl"; unknown.write_bytes(b"\n".join(lines + [json.dumps(end, separators=(",", ":")).encode()]) + b"\n")
for name, path, want in (("identity-drift", drift, "identity differs"), ("seq-gap", seqgap, "seq gap"), ("unknown-event-epoch", unknown, "event epoch 999")):
    v = read_journal.read(str(path)); ok = (not v["qualifies"]) and any(want in e for e in v["errors"])
    results.append(dict(case=name, verdict=v, problems=[] if ok else ["reader accepted a %s" % name]))
# Optional copied-provider identity is immutable on every row; ordinary v1
# journals keep their original bytes and remain accepted without this field.
for name, physical_source, drift in (("external-source", "external-physical-input@1", False),
                            ("external-source-unknown", "unqualified-source", False),
                            ("external-source-drift", "external-physical-input@1", True)):
    body = [dict(json.loads(x), inputSource=physical_source) for x in text if x]
    if drift: del body[1]["inputSource"]
    encoded = [json.dumps(x, separators=(",", ":")).encode() for x in body[:-1]]
    body[-1]["bytes"] = sum(len(x) + 1 for x in encoded)
    path = out / (name + ".jsonl")
    path.write_bytes(b"\n".join(encoded + [json.dumps(body[-1], separators=(",", ":")).encode()]) + b"\n")
    v = read_journal.read(str(path)); expected = name == "external-source"
    results.append(dict(case=name, verdict=v, problems=[] if v["qualifies"] == expected else ["external source identity verdict differs"]))
problems = [(r["case"], r["problems"]) for r in results if r["problems"]]
(out / "receipt.json").write_text(json.dumps(dict(source=source, passed=not problems, proof=proof.stdout.strip(), cases=results), indent=2))
if problems: raise SystemExit("FAIL " + json.dumps(problems, indent=1))
print(proof.stdout.strip()); print("PASS %d reader cases; %s" % (len(results), out / "receipt.json"))
