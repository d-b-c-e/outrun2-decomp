"""The input shim's first in-game loop: one latched OutRunDesktop run driven through inject.txt.

Live, on the rig, only when coordinated (Astra/owner). It reuses the existing protections: the hub's rig lease and
owner-idle gate (tools/Rig-Lease.ps1), the test fence (inject.on latches; the game folder is only read; saves,
options and logs go to the run folder), and this build's absence of any force feedback. It never touches the owner's
game folder: it runs from a dedicated test copy (`--prepare` builds it from a source folder). It sends no OS input
and never foregrounds anything: commands go through inject.txt, and the game is closed with WM_CLOSE to its own
window. inject.on / inject.txt are removed on every exit path.

Workload: confirm/start presses until the game reaches a race (mode 16/18, read from the shim's runtime.log lines),
then steer and throttle holds, then `end`. Evidence: the run folder's runtime.log (the shim's record/consumed lines),
controls-journal.jsonl (the strict reader) and host-shutdown.json. The capture (the run folder's capture subfolder) is sealed in the
evidence copy by the reviewed P21 finalizer (--finalizer): the final host report is copied into it and this runner writes
exit.json only for an exit it observed, with the process identity it held from the spawn.
"""
import argparse, ctypes, ctypes.wintypes as wt, hashlib, json, math, os, pathlib, random, re, shutil, string, subprocess, sys, tempfile, time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "control_journal")); import read_journal
sys.path.insert(0, str(HERE)); import compare_replay
DBCE = pathlib.Path(os.environ["LOCALAPPDATA"]) / "dbce" / "outrun2006-decomp"
HUB_LEASE = pathlib.Path(r"E:\Source\dbce-project-mgmt\tools\Rig-Lease.ps1")
# Test-only bindings on the rig's real DirectInput identities (readonly-provider-live-03): the axes as the owner's
# profile has them, the menu actions on otherwise unused R12 buttons. Physical presses of these buttons are not
# admitted objects during the run (latched: physical input stays test-neutral).
WHEEL = "dev={d71b8350-61b7-11f1-8001-444553540000} prod={0006346e-0000-0000-0000-504944564944}"
PROFILE = "\n".join([
    "[Controls]", "Schema = 1", "Profile = ShimFirstLoop", "Revision = t1", "Transmission = Sequential",
    "steer = axis 0 %s range=0..65535 rest=32768 travel=+1" % WHEEL,
    "throttle = axis 2 %s range=0..65535 rest=0 travel=+1" % WHEEL,
    "brake = axis 5 %s range=0..65535 rest=0 travel=+1" % WHEEL,
    "confirm = button 20 %s" % WHEEL, "start = button 21 %s" % WHEEL, "back = button 22 %s" % WHEEL,
    "navUp = button 23 %s" % WHEEL, "navDown = button 24 %s" % WHEEL, ""])
REPLAY_END = re.compile(r"input shim: replay end at live frame (\d+): (\d+) of (\d+) tape rows played \(([^)]*)\)")
SIGNAL = re.compile(r"input shim: frame (\d+) run (\w+) mode (\d+)( race)? record steer (-?\d+) accel (\d+) brake (\d+) "
                    r"buttons ([0-9a-f]+) consumed steer (-?\d+) accel (-?\d+) brake (-?\d+)")

user32 = ctypes.WinDLL("user32", use_last_error=True); kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


def sha(p): return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()


# Run timing (romi 23861). --seconds is the startup budget, from launch to race entry. From race entry the race has its
# own budget, so a late race entry never truncates the race phase: R1's two 3.5 s steer holds, or R3's replay bound.
# The whole run is bounded by startup + race budget, and inject.on expires a cleanup margin after that.
R1_RACE_SECONDS = 7.0          # steer-right then steer-left, 3.5 s each (the R1 workload in main)
RACE_MARGIN = 15.0             # the end batch, log polling and a cold shader cache at race entry
INJECT_MARGIN = 120            # inject.on outlives the run's own bound by this much


def valid_race_seconds(v):
    """A first run's raceSeconds as an R3 bound: a finite positive number. JSON in Python admits NaN and Infinity, and a
    bool is an int."""
    return isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v) and v > 0


def replay_bound(race_seconds, tape_rows):
    """R3's wall-clock bound from race entry: twice the first run's race plus 30 s, never below the former rows / 30 + 15."""
    return max(tape_rows / 30 + 15, 2 * race_seconds + 30)


def race_budget(replay_timeout):
    """Seconds the race phase may use after race entry: R3 its replay bound, R1 its steer holds; plus the margin."""
    return (replay_timeout if replay_timeout is not None else R1_RACE_SECONDS) + RACE_MARGIN


def loop_deadline(start, startup_seconds, race_at, budget):
    """The run loop's bound: the startup budget until race entry, then race entry plus the race budget."""
    return start + startup_seconds if race_at is None else race_at + budget


def inject_expiry(start, startup_seconds, budget):
    """inject.on's expiry: after the latest possible loop bound (a race entered at the end of the startup budget)."""
    return int(math.ceil(start + startup_seconds + budget)) + INJECT_MARGIN


def replay_due(now, phase_at, replay_timeout, playback_end):
    """R3's own end decision: the shim reported the end of the tape, or the replay bound passed."""
    return bool(playback_end) or now - phase_at > replay_timeout


def replace_file(path, data):
    """Atomic publish of inject files: a sibling temp, then ReplaceFileW (works while the shim holds the old handle
    with FILE_SHARE_DELETE; MoveFileEx(REPLACE_EXISTING) is refused then, Astra 15795). New file: a plain rename."""
    path = pathlib.Path(path); tmp = path.with_name(path.name + ".tmp-" + "".join(random.choices(string.ascii_lowercase, k=8)))
    tmp.write_bytes(data)
    if path.exists():
        for _ in range(20):
            if kernel32.ReplaceFileW(str(path), str(tmp), None, 0, None, None): return
            time.sleep(0.05)
        tmp.unlink(missing_ok=True); raise OSError("ReplaceFileW failed: %d" % ctypes.get_last_error())
    os.rename(tmp, path)


def windows_of(pid):
    found = []
    def cb(h, _):
        p = wt.DWORD(); user32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(h): found.append(h)
        return True
    user32.EnumWindows(EnumWindowsProc(cb), 0)
    return found


def creation_time(handle):
    times = [wt.FILETIME() for _ in range(4)]
    if not kernel32.GetProcessTimes(wt.HANDLE(handle), *[ctypes.byref(t) for t in times]): return 0
    return (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime


def seal(out, proc, created, receipt, finalizer):
    """P21: the worker's report (in the capture), the host's final report and an observed normal exit by the same
    process. A missing capture or an exit this runner did not observe is reported, never sealed."""
    cap = out / "run" / "capture"
    if not cap.exists(): receipt["seal"] = "no capture"; return
    if proc is None or proc.poll() is None or not created: receipt["seal"] = "no observed exit"; return
    host = (out / "run" / "host-shutdown.json").read_bytes()
    with open(cap / "host-shutdown.json", "xb") as f: f.write(host)
    with open(cap / "exit.json", "xb") as f:
        f.write(json.dumps(dict(pid=proc.pid, creationTime=created, exitCode=proc.returncode & 0xFFFFFFFF, forced=receipt["forced"])).encode())
    q = subprocess.run(["dotnet", str(finalizer), "--seal-export", str(cap), str(out / "validated.tsv")], capture_output=True, text=True, timeout=300)
    receipt["seal"] = dict(exit=q.returncode, output=(q.stdout + q.stderr).strip()[-600:])


def replay(out, receipt, tool):
    """R2: the adapter/action replay of the sealed capture (capture_replay): every recorded Frame and GameView through a
    fresh Adapter must give the recorded signals. Only a sealed capture is replayed."""
    if not (isinstance(receipt.get("seal"), dict) and receipt["seal"].get("exit") == 0): receipt["replay"] = "not sealed"; return
    q = subprocess.run([str(tool), str(out / "run" / "capture"), str(out / "validated.tsv")], capture_output=True, text=True, timeout=600)
    receipt["replay"] = dict(exit=q.returncode, output=(q.stdout + q.stderr).strip()[-600:])


def read_export(finalizer, capture, dest):
    """The finalizer verifies a sealed capture again (seal inventory, hashes, host and exit, stream) and writes a fresh
    export (Astra 16889): a reused capture's tape always comes from its sealed source."""
    q = subprocess.run(["dotnet", str(finalizer), "--read-export", str(capture), str(dest)], capture_output=True, text=True, timeout=600)
    if q.returncode != 0: raise ValueError("the sealed capture does not verify: %s" % ((q.stdout + q.stderr).strip().splitlines() or [""])[0][:300])


def verdict(receipt):
    """Astra 16766: diagnostics are saved either way; any unmet condition fails the run."""
    failures = ["error: %s" % e for e in receipt["errors"]]
    if receipt.get("phase") != "end": failures.append("workload unfinished (phase %s)" % receipt.get("phase"))
    if receipt.get("forced") or receipt.get("exitCode") != 0:
        failures.append("exit not normal (code %r, forced %r)" % (receipt.get("exitCode"), receipt.get("forced")))
    if (receipt.get("host") or {}).get("state") != "resolved": failures.append("host shutdown not resolved")
    if not (receipt.get("journal") or {}).get("qualifies"): failures.append("control journal does not qualify")
    seal_result = receipt.get("seal")
    if not (isinstance(seal_result, dict) and seal_result.get("exit") == 0): failures.append("capture not sealed: %s" % str(seal_result)[:200])
    r3 = receipt.get("r3")
    if r3 is not None:   # an R3 replay run: the whole tape played, and the race progressed while it did
        if not r3.get("playbackComplete"): failures.append("replay playback incomplete: %s" % r3.get("playbackEnd"))
        cmp = r3.get("compare")
        if not isinstance(cmp, dict): failures.append("no R3 comparison: %s" % str(cmp)[:200])
        else:   # Astra 16889: ordered complete coverage and every intended word equal; consumed words stay diagnostic
            if not (cmp.get("contiguous") and cmp.get("playedRows") == cmp.get("tapeRows")):
                failures.append("tape not covered in order: %s of %s rows, contiguous %s" % (cmp.get("playedRows"), cmp.get("tapeRows"), cmp.get("contiguous")))
            bad = {w: v for w, v in (cmp.get("intended") or {}).items() if v.split("/")[0] != v.split("/")[1]}
            if bad or not cmp.get("intended"): failures.append("intended words differ from the first run: %s" % json.dumps(bad))
            if not (cmp["progression"]["replayInGameRows"] == cmp["playedRows"] and cmp["progression"]["replayMaxSpeed"] > 0):
                failures.append("no race progression during playback: %s" % json.dumps(cmp["progression"]))
    replay_result = receipt.get("replay")
    if not (isinstance(replay_result, dict) and replay_result.get("exit") == 0): failures.append("adapter replay did not reconstruct: %s" % str(replay_result)[:300])
    receipt["result"] = "fail" if failures else "pass"; receipt["failures"] = failures
    return failures


def lease(action, purpose=""):
    cmd = ["pwsh", "-NoProfile", "-File", str(HUB_LEASE), "-" + action]
    if action == "Enter": cmd += ["-Purpose", purpose, "-IdleMinutes", "5"]
    q = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
    if q.returncode != 0: raise RuntimeError("rig lease %s refused: %s" % (action, (q.stdout + q.stderr).strip()[-400:]))


def prepare(source, game, candidate):
    if game.exists(): raise SystemExit("the test folder already exists: %s" % game)
    shutil.copytree(source, game)
    shutil.copy2(candidate, game / "OutRunDesktop.exe")
    (game / "OutRunDesktop.ini").write_text(PROFILE, encoding="utf-8", newline="\n")
    print("prepared %s (exe %s, profile %s)" % (game, sha(game / "OutRunDesktop.exe")[:16], sha(game / "OutRunDesktop.ini")[:16]))


def main():
    a = argparse.ArgumentParser(description=__doc__)
    a.add_argument("--game", type=pathlib.Path, default=pathlib.Path(r"E:\Games\OutRunShimTest"))
    a.add_argument("--candidate", type=pathlib.Path, required=True)
    a.add_argument("--prepare-from", type=pathlib.Path, help="build --game as a copy of this game folder first")
    a.add_argument("--result", type=pathlib.Path, help="a fresh result folder (runs and preflight)")
    a.add_argument("--finalizer", type=pathlib.Path, help="the reviewed P21Finalizer.dll (p21_recorder_integration/managed)")
    a.add_argument("--replay-tool", type=pathlib.Path, help="capture_replay.exe (R2), built with the candidate")
    a.add_argument("--replay-of", type=pathlib.Path, help="R3: a passing first-loop result folder whose sealed capture is played")
    a.add_argument("--seconds", type=int, default=180)
    a.add_argument("--check-only", action="store_true", help="preflight only: no lease, no files, no launch")
    o = a.parse_args()
    if o.prepare_from: prepare(o.prepare_from, o.game, o.candidate); return
    if not o.result: a.error("--result is required for a run or a preflight")
    out = o.result.resolve()
    problems = []
    if out.exists(): problems.append("result folder exists")
    if not (o.game / "OR2006C2C.exe").exists(): problems.append("no game data in %s (use --prepare-from)" % o.game)
    if not (o.game / "OutRunDesktop.exe").exists() or sha(o.game / "OutRunDesktop.exe") != sha(o.candidate):
        problems.append("the test folder's OutRunDesktop.exe is not the candidate")
    # The x64 exe imports DINPUT8.dll statically and the loader searches its own folder first: any dinput8.dll there
    # (the copied Tweaks proxy is x86) would be loaded instead of the system one (Astra 17238, zero 17223).
    proxy = o.game / "dinput8.dll"
    if proxy.exists():
        head = proxy.read_bytes()[:4096]; pe = int.from_bytes(head[60:64], "little") if len(head) >= 64 else 0
        machine = int.from_bytes(head[pe + 4:pe + 6], "little") if head[:2] == b"MZ" and head[pe:pe + 4] == b"PE\0\0" else None
        problems.append("dinput8.dll beside the exe (machine %s, sha %s) would load instead of the system DirectInput: move it out of the test copy"
                        % ({0x14c: "x86", 0x8664: "x64"}.get(machine, machine), sha(proxy)[:16]))
    ini = o.game / "OutRunDesktop.ini"
    if not ini.exists() or ini.read_text(encoding="utf-8") != PROFILE:
        problems.append("the test folder's OutRunDesktop.ini is not the test profile")
    if not o.finalizer or not o.finalizer.exists(): problems.append("no --finalizer P21Finalizer.dll to seal the capture")
    if not o.replay_tool or not o.replay_tool.exists(): problems.append("no --replay-tool capture_replay.exe to replay the capture")
    tape = None; tape_rows = 0; tape_source = None; replay_timeout = None
    if o.replay_of:   # R3: only a passing first run whose sealed capture verifies again is played
        first = o.replay_of.resolve(); tape_source = first / "run" / "capture"; tape = out / "tape.tsv"
        try:
            r1 = json.loads((first / "receipt.json").read_text())
            if r1.get("result") != "pass": problems.append("--replay-of is not a passing run")
            # The same candidate plays its own tape (nene audit): another build may consume the same words differently.
            r1_sha = (r1.get("plan") or {}).get("candidateSha256")
            if not o.candidate.exists() or r1_sha != sha(o.candidate):
                problems.append("--replay-of was recorded by another candidate (%s, this run %s)" % (str(r1_sha)[:16], sha(o.candidate)[:16] if o.candidate.exists() else None))
            # Wall-clock bound from the first run's own race (nene audit): a test run's shader cache is always cold, so a
            # fixed frame rate is not assumed. The former floor, tape rows at 30 per second plus 15 s, stays the minimum.
            race_seconds = r1.get("raceSeconds")
            if not valid_race_seconds(race_seconds): problems.append("--replay-of has no finite positive race duration (raceSeconds %r)" % (race_seconds,))
            else: replay_timeout = race_seconds
            if not o.finalizer or not o.finalizer.exists(): raise ValueError("no finalizer to verify it")
            with tempfile.TemporaryDirectory() as t:   # the preflight writes nothing it keeps
                read_export(o.finalizer, tape_source, pathlib.Path(t) / "tape.tsv"); tape_rows = len(compare_replay.tape(str(pathlib.Path(t) / "tape.tsv")))
            if not tape_rows: problems.append("--replay-of has no race rows with the run Running")
            if replay_timeout is not None: replay_timeout = replay_bound(replay_timeout, tape_rows)
        except (OSError, ValueError, SystemExit) as e: problems.append("--replay-of unusable: %s" % e)
    for f in ("inject.on", "inject.txt"):
        if (DBCE / f).exists(): problems.append("leftover %s in %s" % (f, DBCE))
    running = subprocess.run(["tasklist", "/FI", "IMAGENAME eq OutRunDesktop.exe", "/NH"], capture_output=True, text=True).stdout
    if "OutRunDesktop.exe" in running: problems.append("OutRunDesktop is already running")
    plan = dict(game=str(o.game), candidate=str(o.candidate), candidateSha256=sha(o.candidate) if o.candidate.exists() else None,
                finalizer=str(o.finalizer), finalizerSha256=sha(o.finalizer) if o.finalizer and o.finalizer.exists() else None,
                replayTool=str(o.replay_tool), replayToolSha256=sha(o.replay_tool) if o.replay_tool and o.replay_tool.exists() else None,
                replayOf=str(o.replay_of) if o.replay_of else None, tapeSource=str(tape_source) if tape_source else None, tapeRows=tape_rows,
                replayTimeoutSeconds=replay_timeout, raceBudgetSeconds=race_budget(replay_timeout) if not o.replay_of or replay_timeout is not None else None,
                dbce=str(DBCE), seconds=o.seconds, problems=problems)
    if o.check_only or problems:
        print(json.dumps(plan, indent=1)); sys.exit(1 if problems else 0)
    out.mkdir(parents=True)
    if tape:   # the run's own fresh export of the verified sealed source: the tape the game plays and the report compares
        read_export(o.finalizer, tape_source, tape); plan["tapeSha256"] = sha(tape)
        if len(compare_replay.tape(str(tape))) != tape_rows: raise SystemExit("the sealed source changed after the preflight")
    receipt = dict(plan=plan, started=time.strftime("%Y-%m-%dT%H:%M:%S%z"), presses=[], batches=[], forced=False, errors=[],
                   injection="inject action <id> <n>: the shim resolves each action through the test profile's declared binding "
                             "inverse into a raw sample of that bound R12 object; not a physical hardware trial (Astra 16520)")
    if tape:
        receipt["r3"] = dict(playbackComplete=False, playbackEnd=None, compare=None,
            label="menus: injected presses as above; race: the first run's recorded normalized Frame actions, played at the adapter "
                  "boundary while the live latched session is Running (Astra 16754): not physical input, not a raw-to-Core replay, no route claim")
    nonce = "".join(random.choices(string.ascii_letters + string.digits, k=16))
    proc = None; run_root = None; leased = False; created = 0; wrote_ini = False
    published = {}                               # only files this invocation wrote, with their last bytes
    def publish(name, data):
        replace_file(DBCE / name, data); published[name] = data
    try:
        lease("Enter", "OutRun shim first loop (latched, no force)"); leased = True
        if tape:   # the test copy's ini names the tape for this run only; restored to the exact profile bytes after it
            wrote_ini = True; ini.write_bytes((PROFILE + "[ShimReplay]\nTape = %s\n" % tape).encode("utf-8"))
        DBCE.mkdir(parents=True, exist_ok=True)
        start = time.time(); budget = race_budget(replay_timeout)
        publish("inject.on",("nonce=%s\nexpires=%d\n" % (nonce, inject_expiry(start, o.seconds, budget))).encode())
        proc = subprocess.Popen([str(o.game / "OutRunDesktop.exe"), "--windowed"], cwd=str(o.game))
        receipt["pid"] = proc.pid; created = creation_time(int(proc._handle)); receipt["creationTime"] = created
        run_root = DBCE / ("run-%s-%d" % (nonce, proc.pid)); log = run_root / "runtime.log"
        seen = 0; mode = None; race = False; n = 0; last_press = 0; lines = []
        def batch(commands):
            nonlocal n
            n += 1; body = "nonce=%s\nbatch=%d\n%s\n" % (nonce, n, "\n".join(commands))
            publish("inject.txt", body.encode()); receipt["batches"].append(dict(n=n, commands=commands, at=time.time()))
        phase = "menus"; phase_at = 0; race_at = None
        while time.time() < loop_deadline(start, o.seconds, race_at, budget) and proc.poll() is None:
            time.sleep(0.2)
            if log.exists():
                text = log.read_text(encoding="utf-8", errors="replace"); new = text.splitlines()[seen:]; seen += len(new); lines += new
                for l in new:
                    m = SIGNAL.search(l)
                    if m: mode = int(m.group(3)); race = m.group(4) is not None
                    e = REPLAY_END.search(l)
                    if e and tape:
                        receipt["r3"]["playbackEnd"] = l.strip()
                        receipt["r3"]["playbackComplete"] = e.group(2) == e.group(3) and int(e.group(3)) == tape_rows and e.group(4) == "tape complete"
            if phase == "menus":
                if race: race_at = time.time()
                if race and tape: phase, phase_at = "replay", time.time(); continue   # the shim plays the tape from the race entry
                if race: phase, phase_at = "steer-right", time.time(); batch(["inject action steer 0.6 ms=3000", "inject action throttle 1 ms=3000"]); continue
                if time.time() - last_press > 1.5 and len(receipt["presses"]) < 40:
                    action = "start" if len(receipt["presses"]) % 3 == 0 else "confirm"
                    batch(["inject action %s 1 ms=200" % action]); receipt["presses"].append(dict(action=action, mode=mode)); last_press = time.time()
            elif phase == "steer-right" and time.time() - phase_at > 3.5:
                phase, phase_at = "steer-left", time.time(); batch(["inject action steer -0.6 ms=3000", "inject action throttle 1 ms=3000"])
            elif phase == "steer-left" and time.time() - phase_at > 3.5:
                phase = "end"; batch(["end"]); break
            elif phase == "replay" and replay_due(time.time(), phase_at, replay_timeout, receipt["r3"]["playbackEnd"]):
                phase = "end"; batch(["end"]); break
        receipt["phase"] = phase; receipt["race"] = race
        # Wall-clock seconds from race entry to the end batch: an R3 of this run derives its timeout from it.
        receipt["raceSeconds"] = round(time.time() - race_at, 3) if race_at is not None and phase == "end" else None
        time.sleep(1.0)
        if proc.poll() is None:
            for h in windows_of(proc.pid): user32.PostMessageW(h, 0x0010, 0, 0)   # WM_CLOSE: the game's own close path
            try: proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                # A latched process whose stop did not resolve parks for its supervisor (P21): this runner ends it.
                receipt["forced"] = True; proc.kill(); proc.wait(timeout=15)
        receipt["exitCode"] = proc.returncode
    except Exception as e:
        receipt["errors"].append(repr(e))
        if proc and proc.poll() is None:
            for h in windows_of(proc.pid): user32.PostMessageW(h, 0x0010, 0, 0)
            try: proc.wait(timeout=30)
            except subprocess.TimeoutExpired: receipt["forced"] = True; proc.kill()
    finally:
        try:
            try:
                # Only while our lease is held, and only the files this invocation published with exactly our bytes: a
                # refused lease or another caller's files are never touched (Astra 16520).
                if leased:
                    for f, data in published.items():
                        try:
                            if (DBCE / f).read_bytes() == data: (DBCE / f).unlink()
                            else: receipt["errors"].append("cleanup %s: not our bytes, left in place" % f)
                        except FileNotFoundError: pass
                        except OSError as e: receipt["errors"].append("cleanup %s: %r" % (f, e))
            finally:
                if wrote_ini:
                    try:
                        ini.write_bytes(PROFILE.encode("utf-8"))
                        if ini.read_bytes() != PROFILE.encode("utf-8"): receipt["errors"].append("test profile not restored")
                    except OSError as e: receipt["errors"].append("test profile restore: %r" % e)
                try:
                    if run_root and run_root.exists(): shutil.copytree(run_root, out / "run", dirs_exist_ok=True)
                except Exception as e: receipt["errors"].append("evidence copy: %r" % e)
        finally:
            if leased:
                try: lease("Exit")
                except Exception as e: receipt["errors"].append(repr(e))
        try: seal(out, proc, created, receipt, o.finalizer)
        except Exception as e: receipt["errors"].append("seal: %r" % e)
        try: replay(out, receipt, o.replay_tool)
        except Exception as e: receipt["errors"].append("replay: %r" % e)
        if tape and isinstance(receipt.get("replay"), dict) and receipt["replay"].get("exit") == 0:
            q = subprocess.run([sys.executable, "-B", str(HERE / "compare_replay.py"), str(tape), str(out / "validated.tsv")],
                               capture_output=True, text=True, timeout=600)
            try: receipt["r3"]["compare"] = json.loads(q.stdout) if q.returncode == 0 else (q.stdout + q.stderr).strip()[-400:]
            except ValueError: receipt["r3"]["compare"] = (q.stdout + q.stderr).strip()[-400:]
        try:
            if (out / "run" / "controls-journal.jsonl").exists(): receipt["journal"] = read_journal.read(str(out / "run" / "controls-journal.jsonl"))
            if (out / "run" / "host-shutdown.json").exists(): receipt["host"] = json.loads((out / "run" / "host-shutdown.json").read_text())
        except Exception as e: receipt["errors"].append("evidence read: %r" % e)
        sig = [SIGNAL.search(l) for l in (out / "run" / "runtime.log").read_text(encoding="utf-8", errors="replace").splitlines()] if (out / "run" / "runtime.log").exists() else []
        sig = [m for m in sig if m]
        receipt["modes"] = sorted({int(m.group(3)) for m in sig})
        receipt["maxRecordSteer"] = max([abs(int(m.group(5))) for m in sig], default=0)
        receipt["maxConsumedSteer"] = max([abs(int(m.group(9))) for m in sig], default=0)
        receipt["maxConsumedAccel"] = max([int(m.group(10)) for m in sig], default=0)
        failures = verdict(receipt)
        (out / "receipt.json").write_text(json.dumps(receipt, indent=1, default=str))
        print(json.dumps({k: receipt.get(k) for k in ("result", "failures", "pid", "exitCode", "forced", "phase", "race", "modes", "maxRecordSteer",
                                                     "maxConsumedSteer", "maxConsumedAccel", "seal", "replay", "r3")}, indent=1))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
