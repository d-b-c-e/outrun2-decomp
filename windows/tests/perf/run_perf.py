"""Repeatable OutRunDesktop performance run (zero 24036/24106): one muted, unlatched launch from a test copy, a timed key
script into a race, N measured seconds, a graceful close, and the evidence in a fresh result folder.

    py -3 -B run_perf.py --game <test copy> --keys <keys.json> --seconds 60 --lease-owner claude-zero --result <new dir>
                         [--etw [--etw-profile cadence.wprp]] [--env OR2_HOST_LIVE=1] [--check-only]

- No OS input and no focus change: keys are posted (PostMessage WM_KEYDOWN / WM_KEYUP, scan code in lParam) to the game's
  own top-level window, which fills desktop_keys() (window_desktop.cpp) as typing would. The game is closed with WM_CLOSE.
- No lease of its own: the caller holds the rig lease. This refuses unless %LOCALAPPDATA%\\dbce\\test-slot.txt is fresh
  (under 2 hours, as Stage-RigLease.ps1) and names --lease-owner.
- Unlatched: it refuses while %LOCALAPPDATA%\\dbce\\outrun2006-decomp\\inject.on exists (that would latch the process).
- Never the owner's state: it refuses the smoke copy, the save backups and Steam library folders.
- Evidence: the copy's runtime.log and OutRunXbox.log after the close (the frame profile of the race modes is written at
  exit, game_host.cpp / xbox/source/main.cpp), perf.json with the parsed profile and the run's timing, and with --etw a
  wpr CPU+GPU trace whose merge runs only after the game has closed. Keep a no-ETW run as the fps reference.

keys.json: a list of steps, each {"key": <name>, "at": <seconds after the window appears>, "hold": <seconds, default 0.1>},
or an object {"steps": [...], "measure_from": s, "requires_empty_savegame": true}; the last makes the preflight refuse a
test copy whose SaveGame folder is not empty (it is checked, never cleared).
Names: enter, esc, up, down, left, right, space, or a one-letter key a-z. "measure_from" (seconds, optional, top level
when keys.json is an object {"steps": [...], "measure_from": s}) marks where the race is expected to be running; the
measured window is --seconds from there.
"""
import argparse, ctypes, ctypes.wintypes as wt, datetime, hashlib, json, os, pathlib, re, shutil, subprocess, sys, time

user32 = ctypes.WinDLL("user32", use_last_error=True); kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
WM_KEYDOWN, WM_KEYUP, WM_CLOSE = 0x0100, 0x0101, 0x0010
SLOT = pathlib.Path(os.environ["LOCALAPPDATA"]) / "dbce" / "test-slot.txt"
INJECT_ON = pathlib.Path(os.environ["LOCALAPPDATA"]) / "dbce" / "outrun2006-decomp" / "inject.on"
REFUSED_PARTS = ("outrundecompsmoke", "savebackups", "steamapps")
# DirectInput scan codes (DIK); extended keys carry lParam bit 24 and land at DIK | 0x80 in desktop_keys().
KEYS = {"enter": (0x1c, False), "esc": (0x01, False), "space": (0x39, False),
        "up": (0x48, True), "down": (0x50, True), "left": (0x4b, True), "right": (0x4d, True)}
KEYS.update({c: (s, False) for c, s in zip("qwertyuiop", range(0x10, 0x1a))})
KEYS.update({c: (s, False) for c, s in zip("asdfghjkl", range(0x1e, 0x27))})
KEYS.update({c: (s, False) for c, s in zip("zxcvbnm", range(0x2c, 0x33))})
PROFILE_HEAD = re.compile(r"frame profile \(race modes\): frames=(\d+) avg=([\d.]+) ms \(([\d.]+) fps\) max=([\d.]+) ms")
PROFILE_ROW = re.compile(r"^\s{2}(.+?)\s+([\d.]+) ms/frame\s+max\s+([\d.]+) ms\s+calls (\d+)\s*$")
# zero 1d9ca5e: one unindented line after the buckets, the race's slow-frame counts.
PROFILE_SLOW = re.compile(r"frame profile slow race frames: >20 ms (\d+), >33 ms (\d+), >50 ms (\d+), >100 ms (\d+)")


def sha(p): return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()


def key_lparam(scan, extended, up):
    """WM_KEYDOWN / WM_KEYUP lParam: repeat 1, scan code in bits 16-23, extended in bit 24; key-up sets bits 30 and 31."""
    v = 1 | (scan << 16) | ((1 << 24) if extended else 0)
    if up: v |= (1 << 30) | (1 << 31)
    return v


def load_keys(path):
    doc = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    steps, measure_from = (doc.get("steps"), doc.get("measure_from")) if isinstance(doc, dict) else (doc, None)
    empty_savegame = bool(doc.get("requires_empty_savegame")) if isinstance(doc, dict) else False
    if not isinstance(steps, list) or not steps: raise ValueError("keys.json has no steps")
    out = []
    for s in steps:
        k = str(s.get("key", "")).lower()
        if k not in KEYS: raise ValueError("unknown key %r" % k)
        at, hold = float(s["at"]), float(s.get("hold", 0.1))
        if not (at >= 0 and 0 < hold <= 30): raise ValueError("bad timing in %r" % s)
        out.append(dict(key=k, at=at, hold=hold))
    out.sort(key=lambda s: s["at"])
    if measure_from is None: measure_from = max(s["at"] + s["hold"] for s in out)
    return out, float(measure_from), empty_savegame


def parse_profile(text):
    """The last frame profile in OutRunXbox.log: the header and its bucket rows (ms per frame, max, calls)."""
    lines = text.splitlines(); last = None
    for i, l in enumerate(lines):
        if PROFILE_HEAD.search(l): last = i
    if last is None: return None
    m = PROFILE_HEAD.search(lines[last])
    prof = dict(frames=int(m.group(1)), avgMs=float(m.group(2)), fps=float(m.group(3)), maxMs=float(m.group(4)), buckets=[])
    for l in lines[last + 1:]:
        r = PROFILE_ROW.match(l)
        if not r: break
        prof["buckets"].append(dict(name=r.group(1).strip(), msPerFrame=float(r.group(2)), maxMs=float(r.group(3)), calls=int(r.group(4))))
    slow = [PROFILE_SLOW.search(l) for l in lines[last + 1:]]
    slow = [m for m in slow if m]
    prof["slowFrames"] = dict(over20=int(slow[0].group(1)), over33=int(slow[0].group(2)), over50=int(slow[0].group(3)),
                              over100=int(slow[0].group(4))) if slow else None   # absent in builds before 1d9ca5e
    return prof


def lease_problem(owner):
    if not SLOT.exists(): return "no rig lease (%s)" % SLOT
    if time.time() - SLOT.stat().st_mtime > 2 * 3600: return "the rig lease is stale"
    holder = SLOT.read_text(encoding="utf-8", errors="replace").split()[:1]
    if holder != [owner]: return "the rig lease is held by %s, not %s" % (holder[0] if holder else "?", owner)
    return None


def windows_of(pid):
    found = []
    def cb(h, _):
        p = wt.DWORD(); user32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(h): found.append(h)
        return True
    user32.EnumWindows(EnumWindowsProc(cb), 0)
    return found


def post_key(hwnd, name, up):
    scan, ext = KEYS[name]
    if not user32.PostMessageW(hwnd, WM_KEYUP if up else WM_KEYDOWN, 0, key_lparam(scan, ext, up)):
        raise OSError("PostMessage failed: %d" % ctypes.get_last_error())


def process_times(handle):
    t = [wt.FILETIME() for _ in range(4)]
    if not kernel32.GetProcessTimes(wt.HANDLE(handle), *[ctypes.byref(x) for x in t]): return None
    f = lambda ft: ((ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 1e7
    return dict(kernelSeconds=f(t[2]), userSeconds=f(t[3]))


def wpr(*args):
    q = subprocess.run(["wpr", *args], capture_output=True, text=True, timeout=900)
    return dict(args=list(args), exit=q.returncode, output=(q.stdout + q.stderr).strip()[-400:])


def main():
    a = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument("--game", type=pathlib.Path, required=True, help="the test copy holding OutRunDesktop.exe and the game data")
    a.add_argument("--keys", type=pathlib.Path, required=True)
    a.add_argument("--seconds", type=float, default=60.0, help="measured seconds after measure_from")
    a.add_argument("--lease-owner", required=True)
    a.add_argument("--result", type=pathlib.Path, required=True)
    a.add_argument("--etw", action="store_true", help="wpr CPU+GPU over the run; merged after the close")
    a.add_argument("--etw-profile", type=pathlib.Path, help="with --etw: this .wprp instead of CPU+GPU (cadence.wprp: present and vsync only)")
    a.add_argument("--env", action="append", default=[], help="K=V for the game process")
    a.add_argument("--windowed", action="store_true", help="pass --windowed (1280x720) instead of the borderless window")
    a.add_argument("--startup-seconds", type=float, default=90.0, help="bound for the window to appear")
    a.add_argument("--check-only", action="store_true")
    o = a.parse_args()
    game = o.game.resolve(); out = o.result.resolve(); problems = []
    if any(p in str(game).lower() for p in REFUSED_PARTS): problems.append("refused folder (owner state or Steam library): %s" % game)
    exe = game / "OutRunDesktop.exe"
    if not exe.exists() or not (game / "OR2006C2C.exe").exists(): problems.append("no OutRunDesktop.exe and game data in %s" % game)
    if (game / "dinput8.dll").exists(): problems.append("dinput8.dll beside the exe would load instead of the system DirectInput")
    if INJECT_ON.exists(): problems.append("%s exists: the process would latch" % INJECT_ON)
    if out.exists(): problems.append("result folder exists")
    try: steps, measure_from, empty_savegame = load_keys(o.keys)
    except (OSError, ValueError, KeyError) as e: problems.append("keys: %s" % e); steps, measure_from, empty_savegame = [], 0.0, False
    # A script that starts from "create new license" needs an empty SaveGame; the harness checks, it never deletes.
    if empty_savegame and (not (game / "SaveGame").is_dir() or any((game / "SaveGame").iterdir())):
        problems.append("the key script needs an empty SaveGame folder in the test copy")
    if not (0 < o.seconds <= 1800): problems.append("--seconds out of range")
    if o.etw_profile and (not o.etw or o.etw_profile.suffix.lower() != ".wprp" or not o.etw_profile.is_file()):
        problems.append("--etw-profile needs --etw and an existing .wprp file")
    env = dict(os.environ)
    for kv in o.env:
        k, sep, v = kv.partition("=")
        if not sep or not re.fullmatch(r"OR2_[A-Z0-9_]+", k): problems.append("--env must be OR2_*=value: %r" % kv)
        else: env[k] = v
    running = subprocess.run(["tasklist", "/FI", "IMAGENAME eq OutRunDesktop.exe", "/NH"], capture_output=True, text=True).stdout
    if "OutRunDesktop.exe" in running: problems.append("OutRunDesktop is already running")
    lp = lease_problem(o.lease_owner)
    if lp: problems.append(lp)
    plan = dict(game=str(game), exeSha256=sha(exe) if exe.exists() else None, keys=str(o.keys), keysSha256=sha(o.keys) if o.keys.exists() else None,
                steps=len(steps), measureFrom=measure_from, seconds=o.seconds, etw=o.etw, env=o.env, windowed=o.windowed, problems=problems,
                etwProfile=str(o.etw_profile.resolve()) if o.etw_profile else None,
                etwProfileSha256=sha(o.etw_profile) if o.etw_profile and o.etw_profile.is_file() else None)
    if o.check_only or problems:
        print(json.dumps(plan, indent=1)); return 1 if problems else 0
    out.mkdir(parents=True)
    logs = {n: game / n for n in ("runtime.log", "OutRunXbox.log")}
    before = {n: (p.stat().st_mtime if p.exists() else None) for n, p in logs.items()}
    receipt = dict(plan=plan, started=datetime.datetime.now().astimezone().isoformat(), errors=[], keysSent=[], forced=False)
    proc = None; etw_on = False
    try:
        if o.etw:
            start = ("-start", str(o.etw_profile.resolve())) if o.etw_profile else ("-start", "CPU", "-start", "GPU")
            receipt["wprStart"] = wpr(*start, "-filemode"); etw_on = receipt["wprStart"]["exit"] == 0
            if not etw_on: raise RuntimeError("wpr -start failed: %s" % receipt["wprStart"]["output"])
        args = [str(exe)] + (["--windowed"] if o.windowed else [])
        launched = time.time(); proc = subprocess.Popen(args, cwd=str(game), env=env); receipt["pid"] = proc.pid
        hwnd = None
        while time.time() - launched < o.startup_seconds and proc.poll() is None:
            w = windows_of(proc.pid)
            if w: hwnd = w[0]; break
            time.sleep(0.1)
        if hwnd is None: raise RuntimeError("no game window within %.0f s" % o.startup_seconds)
        t0 = time.time(); receipt["windowSeconds"] = round(t0 - launched, 3)
        pending = [(s["at"], s["key"], False) for s in steps] + [(s["at"] + s["hold"], s["key"], True) for s in steps]
        pending.sort(key=lambda e: (e[0], e[2]))
        end = t0 + measure_from + o.seconds
        while time.time() < end and proc.poll() is None:
            now = time.time() - t0
            while pending and pending[0][0] <= now:
                at, name, up = pending.pop(0); post_key(hwnd, name, up)
                receipt["keysSent"].append(dict(at=round(now, 3), key=name, up=up))
            time.sleep(0.01)
        receipt["measuredUntil"] = round(time.time() - t0, 3)
        if proc.poll() is not None: receipt["errors"].append("the game exited by itself (code %s)" % proc.returncode)
        times = process_times(int(proc._handle))
        if times: receipt["processTimes"] = times
    except Exception as e:
        receipt["errors"].append(repr(e))
    finally:
        if proc and proc.poll() is None:
            for h in windows_of(proc.pid): user32.PostMessageW(h, WM_CLOSE, 0, 0)
            try: proc.wait(timeout=30)
            except subprocess.TimeoutExpired: receipt["forced"] = True; proc.kill(); proc.wait(timeout=15)
        receipt["exitCode"] = proc.returncode if proc else None
        if etw_on:   # merged only now, with the game closed
            receipt["wprStop"] = wpr("-stop", str(out / "trace.etl"))
    for n, p in logs.items():
        if p.exists() and p.stat().st_mtime != before[n]: shutil.copy2(p, out / n)
        else: receipt["errors"].append("%s not written by this run" % n)
    prof = parse_profile((out / "OutRunXbox.log").read_text(encoding="utf-8", errors="replace")) if (out / "OutRunXbox.log").exists() else None
    receipt["profile"] = prof
    if prof is None: receipt["errors"].append("no race-mode frame profile in OutRunXbox.log (did the race start?)")
    receipt["result"] = "fail" if receipt["errors"] or receipt["forced"] or receipt["exitCode"] != 0 else "pass"
    (out / "perf.json").write_text(json.dumps(receipt, indent=1), encoding="utf-8")
    print(json.dumps({k: receipt.get(k) for k in ("result", "errors", "exitCode", "forced", "windowSeconds", "measuredUntil")}, indent=1))
    if prof: print("race profile: %d frames, %.2f ms avg (%.1f fps), max %.2f ms" % (prof["frames"], prof["avgMs"], prof["fps"], prof["maxMs"]))
    return 0 if receipt["result"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
