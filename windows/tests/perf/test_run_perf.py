"""Offline checks of run_perf.py (no game, window, lease or file outside a temp folder): the key messages' lParam, the
key script reader, the frame-profile parser on a V0 run 6 excerpt, and the folder refusals.

    py -3 -B test_run_perf.py
"""
import json, pathlib, sys, tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import run_perf

failures = []
def check(ok, what):
    if not ok: failures.append(what)

# lParam: Enter down = repeat 1 | scan 0x1c << 16; Up arrow up = 0x48 << 16 | extended | bits 30, 31.
check(run_perf.key_lparam(0x1c, False, False) == 0x001c0001, "Enter key-down lParam")
check(run_perf.key_lparam(0x48, True, True) == (0x00480001 | 1 << 24 | 1 << 30 | 1 << 31), "Up key-up lParam")
# desktop_keys index (window_desktop.cpp): scan | 0x80 for extended -> Up = 0xc8, as wheel_input.cpp's menu keys read.
scan, ext = run_perf.KEYS["up"]; check((scan | (0x80 if ext else 0)) == 0xc8, "Up lands at DIK 0xc8")
scan, ext = run_perf.KEYS["enter"]; check((scan | (0x80 if ext else 0)) == 0x1c, "Enter lands at DIK 0x1c")
check(run_perf.KEYS["a"] == (0x1e, False) and run_perf.KEYS["z"] == (0x2c, False) and run_perf.KEYS["q"] == (0x10, False), "letter scan codes")

with tempfile.TemporaryDirectory() as t:
    t = pathlib.Path(t)
    k = t / "keys.json"
    k.write_text(json.dumps({"steps": [{"key": "enter", "at": 5}, {"key": "UP", "at": 2, "hold": 0.5}], "measure_from": 20}))
    steps, mf, es = run_perf.load_keys(k)
    check([s["key"] for s in steps] == ["up", "enter"] and mf == 20.0 and es is False, "steps sorted, names lower-cased, measure_from kept")
    k.write_text(json.dumps([{"key": "enter", "at": 3, "hold": 2}]))
    steps, mf, es = run_perf.load_keys(k)
    check(mf == 5.0, "measure_from defaults to the last key's release")
    for bad in ([], [{"key": "f13", "at": 1}], [{"key": "enter", "at": -1}], [{"key": "enter", "at": 1, "hold": 0}]):
        k.write_text(json.dumps(bad))
        try: run_perf.load_keys(k); failures.append("bad key script accepted: %r" % (bad,))
        except (ValueError, KeyError): pass

    shipped = pathlib.Path(run_perf.__file__).with_name("keys-c2c-outrun-new-license.json")
    steps, mf, es = run_perf.load_keys(shipped)
    check(len(steps) == 13 and mf == 40.0 and es is True and steps[0]["key"] == "enter" and steps[-1]["at"] == 28.5, "the shipped C2C key script")

# The V0 run 6 profile shape (artifacts/outrun-v0-20261011/v0-run6-OutRunXbox.log).
log = "\n".join([
    "frontend overlay token=00000000 draws=0 frames=0",
    "frame profile (race modes): frames=1448 avg=45.87 ms (21.8 fps) max=187.65 ms",
    "  renderer draw total                 44.964 ms/frame  max 186.578 ms  calls 1449",
    "  display   pc 405830                       24.971 ms/frame  max 131.223 ms  calls 5796",
    "  disp event   0 pc 4879b0                 4.177 ms/frame  max  51.088 ms  calls 2898",
    "other output"])
p = run_perf.parse_profile(log)
check(p and p["frames"] == 1448 and p["avgMs"] == 45.87 and p["fps"] == 21.8 and p["maxMs"] == 187.65, "profile header")
check(p and [b["name"] for b in p["buckets"]] == ["renderer draw total", "display   pc 405830", "disp event   0 pc 4879b0"], "profile rows end at the first other line")
check(p and p["buckets"][1]["msPerFrame"] == 24.971 and p["buckets"][1]["calls"] == 5796, "row values")
check(run_perf.parse_profile("no profile here") is None, "no profile -> None")
check(p["slowFrames"] is None, "no slow-frame line: None (builds before 1d9ca5e)")
slow = log + "\nframe profile slow race frames: >20 ms 15, >33 ms 1, >50 ms 0, >100 ms 0\n"
ps = run_perf.parse_profile(slow)
check(ps["slowFrames"] == dict(over20=15, over33=1, over50=0, over100=0) and len(ps["buckets"]) == 3, "the slow-frame line is captured; buckets unchanged")
two = log + "\nframe profile (race modes): frames=10 avg=16.67 ms (60.0 fps) max=20.00 ms\n  present  1.000 ms/frame  max 2.000 ms  calls 10\n"
check(run_perf.parse_profile(two)["frames"] == 10, "the last profile wins")
for part in run_perf.REFUSED_PARTS:
    check(part in r"e:\games\outrundecompsmoke e:\games\savebackups\outrun d:\steam\steamapps\common".lower(), "refused part %s" % part)

# --etw-profile: preflight only (no lease, no launch: the folder does not exist, so the plan is printed and refused).
import subprocess, tempfile, xml.etree.ElementTree as ET
here = pathlib.Path(__file__).resolve().parent
def plan(*extra):
    with tempfile.TemporaryDirectory() as t:
        keys = pathlib.Path(t) / "k.json"; keys.write_text('[{"key": "enter", "at": 1}]', encoding="utf-8")
        q = subprocess.run([sys.executable, "-B", str(here / "run_perf.py"), "--game", str(pathlib.Path(t) / "none"), "--keys", str(keys),
                            "--lease-owner", "test", "--result", str(pathlib.Path(t) / "out"), "--check-only", *extra], capture_output=True, text=True)
        return json.loads(q.stdout)
wprp = here / "cadence.wprp"
check(not any("etw-profile" in p for p in plan("--etw", "--etw-profile", str(wprp))["problems"]), "cadence.wprp with --etw is accepted")
check(any("etw-profile" in p for p in plan("--etw-profile", str(wprp))["problems"]), "--etw-profile without --etw is refused")
check(any("etw-profile" in p for p in plan("--etw", "--etw-profile", str(here / "missing.wprp"))["problems"]), "a missing profile is refused")
check(plan("--etw", "--etw-profile", str(wprp))["etwProfileSha256"] == run_perf.sha(wprp), "the plan records the profile's hash")
root = ET.parse(wprp).getroot()
check([p.get("Id") for p in root.iter("Profile")] == ["Cadence.Verbose.File"], "cadence.wprp: one file-mode profile")
check(sorted(p.get("Name") for p in root.iter("EventProvider")) == ["Microsoft-Windows-DXGI", "Microsoft-Windows-DxgKrnl"], "cadence.wprp: DxgKrnl and DXGI only")

print("run_perf: %d failure(s)" % len(failures))
for f in failures: print("  " + f)
sys.exit(1 if failures else 0)
