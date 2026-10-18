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
import argparse, configparser, ctypes, ctypes.wintypes as wt, hashlib, json, math, os, pathlib, random, re, shutil, string, subprocess, sys, tempfile, time

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
    "navUp = button 23 %s" % WHEEL, "navDown = button 24 %s" % WHEEL,
    "navLeft = button 25 %s" % WHEEL, "navRight = button 26 %s" % WHEEL, ""])
# --horn (R1 only; AllowHorn command path, sana #132035): this run's INI adds horn on an otherwise unused R12 button and
# [Audio] AllowHorn = true; the copy's PROFILE is restored after it, as for a tape run.
HORN_INI = PROFILE + "horn = button 27 %s\n[Audio]\nAllowHorn = true\n" % WHEEL
# The menus from launch to a race (zero's mapped path, perf runs 1-3, hcom 24236), in seconds after the game window
# appears. A latched process keeps its saves in the fresh run folder, so every run starts at "create new license":
# title, CREATE NEW LICENSE, three downs to DONE, SINGLE PLAYER, OUTRUN mode, the OUTRUN2 course, the first car,
# automatic, the first music. After the script, the former confirm/start presses continue until the race (cap 40).
MENU_SCRIPT = [(6.0, "confirm"), (9.0, "confirm"), (11.0, "navDown"), (11.5, "navDown"), (12.0, "navDown"), (12.8, "confirm"),
               (15.5, "confirm"), (18.0, "navRight"), (18.8, "confirm"), (21.5, "confirm"), (24.0, "confirm"), (26.0, "confirm"),
               (28.5, "confirm")]
# Existing bank: title, single player, OutRun, course, car, transmission, music, START.
# Same choices as the observed existing-bank route, with conservative animation gaps.
# No editor/name/save actions or fallback presses. Runtime must still witness race entry.
EXISTING_LICENSE_SCRIPT = [(6.0, "confirm"), (9.0, "confirm"), (12.0, "navRight"), (13.0, "confirm"),
                           (16.0, "confirm"), (19.0, "confirm"), (22.0, "confirm"), (25.0, "confirm")]
MENU_FALLBACK_AFTER = 12.0     # seconds after the script's last step with no race before the confirm/start presses resume
MENU_MIN_GAP = 0.4             # seconds between any two presses: each is a 200 ms hold, so the game samples a release
# The race's identity from the run's runtime.log exit summary (romi 24552): the stage the world loads, the retail car
# and the selected course descriptor. R3 compares a first run's tape only on the same route.
ROUTE_LINES = {"stage": re.compile(r"world source: .*[\\/]Stage[\\/]([^\\/]+)[\\/]"),
               "car": re.compile(r"retail driving car=(\d+)"), "course": re.compile(r"course descriptors=\S+ records=\d+ depth=\d+ selected=(\d+)")}
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
HORN_SECONDS = 2.0             # --horn: one 1.5 s horn hold after the steer holds, then its release


def valid_race_seconds(v):
    """A first run's raceSeconds as an R3 bound: a finite positive number. JSON in Python admits NaN and Infinity, and a
    bool is an int."""
    return isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v) and v > 0


def replay_bound(race_seconds, tape_rows):
    """R3's wall-clock bound from race entry: twice the first run's race plus 30 s, never below the former rows / 30 + 15."""
    return max(tape_rows / 30 + 15, 2 * race_seconds + 30)


def race_budget(replay_timeout, horn=False, drive=0.0):
    """Seconds the race phase may use after race entry: R3 its replay bound, R1 its steer holds (or its --drive) and the
    horn step with --horn; plus the margin."""
    return (replay_timeout if replay_timeout is not None else (drive or R1_RACE_SECONDS) + (HORN_SECONDS if horn else 0.0)) + RACE_MARGIN


# --drive (R1 only; sana 215675, the rendering test's workload): from race entry, one batch every DRIVE_STEP seconds for
# the given seconds. The throttle is held across steps (each hold outlasts its step); the steering is straight for
# DRIVE_STRAIGHT seconds, then right, straight, left, straight in turn, so traffic passes on both sides.
DRIVE_STEP = 2.0
DRIVE_STRAIGHT = 12.0


def drive_batch(step):
    """--drive: the commands of step `step` (0 at race entry)."""
    commands = ["inject action throttle 1 ms=%d" % int((DRIVE_STEP + 0.5) * 1000)]
    k = step - int(DRIVE_STRAIGHT / DRIVE_STEP)
    if k >= 0 and k % 2 == 0: commands.append("inject action steer %s ms=1500" % ("0.5" if k % 4 == 0 else "-0.5"))
    return commands


# --render (R1 only): the game's render-test.txt beside inject.on (windows/source/render_test.hpp), from "key=value,..."
# over these keys. The game parses it strictly and refuses the run on anything it does not admit.
RENDER_KEYS = ("screens", "views", "frames", "every", "after", "scale")


def render_request(spec, nonce):
    """--render: render-test.txt's bytes for this session, or None without a spec. ValueError for a malformed spec."""
    if not spec: return None
    lines, seen = ["outrun.render-test@1", "nonce=%s" % nonce], set()
    for part in spec.split(","):
        key, sep, value = part.partition("=")
        if not sep or key not in RENDER_KEYS or key in seen or not value or not value.isalnum():
            raise ValueError("--render: %r is not one of %s=<value>, each once" % (part, "/".join(RENDER_KEYS)))
        seen.add(key); lines.append("%s=%s" % (key, value))
    return ("\n".join(lines) + "\n").encode("ascii")


def render_span(spec):
    """Whether --render asks for a span (screens other than off): the game then runs borderless, not --windowed."""
    return any(part in ("screens=surround", "screens=separate") for part in (spec or "").split(","))


HORN_LOOP, HORN_STOP = 0x9BC, 0x81BC   # 42F0D0 commands: 0x1BC | SND_LOOP and 0x1BC | SND_STOP
# native_race_sound_status: "... played: <hex>x<n> ... refused: ... missing: ... ics(ch/code=value): ..."
SOUND_SECTIONS = re.compile(r"\bplayed:(.*?)\brefused:(.*?)\bmissing:(.*?)(?:\bics\(|$)")
SOUND_COUNT = re.compile(r"([0-9a-f]+)x(\d+)")


def sound_counts(section):
    """One section of the race sound status ("51x2 9bcx40"): command -> count; None when a token is not <hex>x<n>."""
    counts = {}
    for tok in section.split():
        m = SOUND_COUNT.fullmatch(tok)
        if not m: return None
        counts[int(m.group(1), 16)] = counts.get(int(m.group(1), 16), 0) + int(m.group(2))
    return counts


def horn_evidence(log_text):
    """--horn: the last race sound status in the run's log (the exit result log's line). 42F0D0 (pc_42f0d0) counts the
    commands it played under "played:" and the ones it failed under "refused:"; "missing:" names absent effects. The
    horn was delivered only with a positive PLAYED count for both the 0x9BC loop and the 0x81BC stop: a refused or
    missing command was only requested (sana #132448). The command path, not a claim that the horn was heard."""
    lines = [l.strip() for l in log_text.splitlines() if "race sound inits=" in l]
    ev = dict(line=lines[-1] if lines else None, played=None, refused=None, missing=None, loop=False, stop=False)
    m = SOUND_SECTIONS.search(ev["line"] or "")
    if not m: return ev
    sections = [sound_counts(s) for s in m.groups()]
    if None in sections: ev["malformed"] = True; return ev
    for name, counts in zip(("played", "refused", "missing"), sections):
        ev[name] = dict(loop=counts.get(HORN_LOOP, 0), stop=counts.get(HORN_STOP, 0))
    ev["loop"] = ev["played"]["loop"] > 0; ev["stop"] = ev["played"]["stop"] > 0
    return ev


def loop_deadline(start, startup_seconds, race_at, budget):
    """The run loop's bound: the startup budget until race entry, then race entry plus the race budget."""
    return start + startup_seconds if race_at is None else race_at + budget


def inject_expiry(start, startup_seconds, budget):
    """inject.on's expiry: after the latest possible loop bound (a race entered at the end of the startup budget)."""
    return int(math.ceil(start + startup_seconds + budget)) + INJECT_MARGIN


def next_menu_action(script, since_menu, sent, last_press_age, presses, allow_fallback=True):
    """The menu press due now, or None: the next script step once its time has come (one per poll, in order), then,
    MENU_FALLBACK_AFTER seconds past the script's last press, the former start/confirm presses 1.5 s apart, 40 presses in all.
    The clock starts when the log first shows the frontend (mode 32), not at the window: render-01 (three angled views at
    7680x1440) first showed it about 12 s after its window, and the first four steps went to mode 0. A late step also waits
    its scripted gap after the previous press, so a stall does not send the steps after it back to back."""
    if since_menu is None or presses >= 40 or last_press_age < MENU_MIN_GAP: return None
    if sent < len(script):
        gap = script[sent][0] - script[sent - 1][0] if sent else 0.0
        return script[sent][1] if since_menu >= script[sent][0] and last_press_age >= gap else None
    if not allow_fallback: return None
    if (since_menu < script[-1][0] + MENU_FALLBACK_AFTER or last_press_age <= 1.5 or
            presses == len(script) and last_press_age < MENU_FALLBACK_AFTER): return None
    return "start" if (presses - len(script)) % 3 == 0 else "confirm"


def route_identity(log_text):
    """The last stage, car and course the run's runtime.log names (an empty value: not logged)."""
    out = {}
    for key, rx in ROUTE_LINES.items():
        found = rx.findall(log_text)
        out[key] = found[-1] if found else None
    return out


def route_of(receipt, log_text):
    """How this run reached its race: by the menu script alone (no fallback press), and the identity the log names."""
    presses = receipt.get("presses") or []
    return dict(scriptedOnly=bool(presses) and all(p.get("scripted") for p in presses),
                script=(receipt.get("plan") or {}).get("menuScript", [list(step) for step in MENU_SCRIPT]), identity=route_identity(log_text))


def render_presentation(text):
    """Only four UI presentation keys; never input, extraction/cache, arbitrary paths or scene assets."""
    if len(text.encode("utf-8")) > 4096: raise ValueError("render presentation exceeds 4096 bytes")
    cfg = configparser.ConfigParser(interpolation=None, strict=True)
    cfg.optionxform = str
    try: cfg.read_string(text)
    except configparser.Error as error: raise ValueError("invalid render presentation: %s" % error) from error
    keys = {"UITextureReplacement", "SceneTextureReplacement", "TextureBaseFolder", "UIScalingMode"}
    if cfg.defaults() or cfg.sections() != ["Graphics"] or set(cfg["Graphics"]) != keys:
        raise ValueError("render presentation requires exactly four whitelisted [Graphics] keys")
    values = dict(cfg["Graphics"])
    if (values["UITextureReplacement"] != "true" or values["SceneTextureReplacement"] != "false" or
            values["UIScalingMode"] != "2"):
        raise ValueError("render presentation requires UI=true, scene=false and UIScalingMode=2")
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,120}", values["TextureBaseFolder"]):
        raise ValueError("render presentation texture folder must be one local directory name")
    return "\n[Graphics]\n" + "".join("%s = %s\n" % (k, values[k]) for k in
                                      ("UITextureReplacement", "SceneTextureReplacement", "TextureBaseFolder", "UIScalingMode"))


def existing_license_problem(game):
    save = game / "SaveGame"
    files = set(p.name for p in save.iterdir()) if save.is_dir() else set()
    if files != {"License1.dat", "common.dat"} or any(not (save / n).is_file() or (save / n).stat().st_size == 0 for n in files):
        return "--existing-license requires exactly two nonempty bank files (License1.dat and common.dat)"
    return None


def same_route(first, second):
    """R3 compares only on the first run's route: both by the script alone, the same script, the same logged identity."""
    if not (first and second and first.get("scriptedOnly") and second.get("scriptedOnly")): return False
    if first.get("script") != second.get("script"): return False
    a, b = first.get("identity") or {}, second.get("identity") or {}
    return all(a.get(k) is not None and a.get(k) == b.get(k) for k in ROUTE_LINES)


def replay_due(now, phase_at, replay_timeout, playback_end):
    """R3's own end decision: the shim reported the end of the tape, or the replay bound passed."""
    return bool(playback_end) or now - phase_at > replay_timeout


def publisher_tool(explicit=None, candidate=None):
    """Deterministic known locations only; no PATH search or unsafe publication fallback."""
    if explicit is not None:
        return pathlib.Path(explicit).resolve()
    choices = ([pathlib.Path(candidate).resolve().parent / "control_publish.exe"] if candidate else []) + [
        HERE / "control_publish.exe", HERE.parents[2] / "build-xbox-pc/windows/Release/control_publish.exe",
        HERE.parents[2] / "build-control-files/Release/control_publish.exe"]
    return next((p for p in choices if p.is_file()), choices[0])


def replace_file(path, data, publisher):
    """The shared native whole-file publisher: no target write handle, no ReplaceFileW retry."""
    q = subprocess.run([str(publisher), str(pathlib.Path(path).resolve())], input=data,
                       capture_output=True, timeout=30)
    if q.returncode:
        raise OSError("control publisher refused: " + q.stderr.decode("utf-8", errors="replace").strip())


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


CONSUMED_END = re.compile(r"input shim: consumed replay end at input tick (\d+) \(mode (\d+)\): (\d+) of (\d+) rows played")


def export_recording(finalizer, capture, dest):
    """A saved ordinary recording's fresh export (P21Finalizer --export-recording, sana aefe991): its recording seal,
    stream, report and the proof's own identity verified again; then every row has the consumed-playback channels."""
    proof = json.loads((capture / "recorder-complete.json").read_text())
    q = subprocess.run(["dotnet", str(finalizer), "--export-recording", str(capture), proof["requestId"], proof["exeSha256"],
                        str(proof["processId"]), str(proof["processCreationFileTime"]), str(dest)], capture_output=True, text=True, timeout=600)
    if q.returncode != 0: raise ValueError("the saved recording does not verify: %s" % ((q.stdout + q.stderr).strip().splitlines() or [""])[0][:300])
    rows = 0; keys = set()
    for line in dest.read_text(encoding="utf-8").splitlines():
        if line.startswith("ROW "):
            if rows and "final.pcDrive" not in keys: raise ValueError("export row %d has no final.pcDrive (recorded before the replay seam)" % (rows - 1))
            rows += 1; keys = set()
        else: keys.add(line.split(" ", 1)[0])
    if not rows or "final.pcDrive" not in keys: raise ValueError("the export has no rows with final.pcDrive")
    return rows


def verdict(receipt):
    """Astra 16766: diagnostics are saved either way; any unmet condition fails the run."""
    failures = ["error: %s" % e for e in receipt["errors"]]
    if receipt.get("phase") != "end": failures.append("workload unfinished (phase %s)" % receipt.get("phase"))
    if receipt.get("retained"):
        failures.append("process retained: WM_CLOSE did not end pid %s; not killed, still running" % receipt["retained"].get("pid"))
    if receipt.get("forced") or receipt.get("exitCode") != 0:
        failures.append("exit not normal (code %r, forced %r)" % (receipt.get("exitCode"), receipt.get("forced")))
    if (receipt.get("host") or {}).get("state") != "resolved": failures.append("host shutdown not resolved")
    if not (receipt.get("journal") or {}).get("qualifies"): failures.append("control journal does not qualify")
    c = receipt.get("consumed")
    if c is not None:   # a consumed playback run: the whole tape played, and the game followed the recording (sana 35159)
        if not c.get("playbackComplete"): failures.append("consumed playback incomplete: %s" % c.get("playbackEnd"))
        cmp = c.get("compare")
        if not isinstance(cmp, dict): failures.append("no consumed comparison: %s" % str(cmp)[:200])
        elif not cmp.get("valid"): failures.append("the comparison evidence is not valid: %s" % "; ".join(cmp.get("problems") or [])[:400])
        else:
            if cmp.get("fedEqualRows") != cmp.get("playedRows"): failures.append("the fed words differ from the recording: %s" % json.dumps(cmp.get("fedFirstDifference")))
            if not cmp.get("followed"): failures.append("the game did not follow the recording: first divergence %s" % json.dumps(cmp.get("firstDivergence")))
        receipt["result"] = "fail" if failures else "pass"; receipt["failures"] = failures
        return failures
    seal_result = receipt.get("seal")
    if not (isinstance(seal_result, dict) and seal_result.get("exit") == 0): failures.append("capture not sealed: %s" % str(seal_result)[:200])
    r3 = receipt.get("r3")
    if r3 is not None and not same_route(r3.get("firstRoute"), receipt.get("route")):
        failures.append("R3 did not race the first run's route: %s vs %s" % (json.dumps(r3.get("firstRoute")), json.dumps(receipt.get("route"))))
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
    horn = receipt.get("horn")
    if horn is not None:   # --horn: the hold was sent, and the result log shows 42F0D0 PLAYED both the LOOP and the STOP
        if not horn.get("sentAt"): failures.append("horn: the hold was not sent")
        counts = "played %s, refused %s, missing %s%s; line %s" % (horn.get("played"), horn.get("refused"), horn.get("missing"),
                                                                 ", malformed" if horn.get("malformed") else "", horn.get("line"))
        if not horn.get("loop"): failures.append("horn: 9BC (loop) not played: %s" % counts)
        if not horn.get("stop"): failures.append("horn: 81BC (stop) not played: %s" % counts)
    receipt["result"] = "fail" if failures else "pass"; receipt["failures"] = failures
    return failures


def retain(proc, receipt):
    """--horn (sana #132311): a process that WM_CLOSE did not end by its deadline is retained and reported, never
    killed: its pid and creation time go in the receipt, the rig lease stays held, and the verdict fails."""
    receipt["retained"] = dict(pid=proc.pid, creationTime=receipt.get("creationTime"),
                               why="WM_CLOSE did not end the process within its deadline; not killed (--horn run)")


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
    a.add_argument("--publisher-tool", type=pathlib.Path, help="control_publish.exe (default: candidate sibling, runner sibling, standard build paths)")
    a.add_argument("--prepare-from", type=pathlib.Path, help="build --game as a copy of this game folder first")
    a.add_argument("--result", type=pathlib.Path, help="a fresh result folder (runs and preflight)")
    a.add_argument("--finalizer", type=pathlib.Path, help="the reviewed P21Finalizer.dll (p21_recorder_integration/managed)")
    a.add_argument("--replay-tool", type=pathlib.Path, help="capture_replay.exe (R2), built with the candidate")
    a.add_argument("--replay-of", type=pathlib.Path, help="R3: a passing first-loop result folder whose sealed capture is played")
    a.add_argument("--consumed-replay", type=pathlib.Path,
                   help="consumed playback (consumed_replay.hpp): a saved ordinary recording of this candidate (the folder holding its "
                        "sealed capture\\); its fresh --export-recording is played at the 453BB0 seam from the first input tick, no presses")
    a.add_argument("--seconds", type=int, default=180)
    a.add_argument("--drive", type=float, default=0.0,
                   help="R1 only: drive this many seconds (15..120) from race entry instead of the two steer holds (drive_batch)")
    a.add_argument("--render", default="",
                   help="R1 only: a rendering test, e.g. screens=separate,views=angled,frames=240,every=3,after=360,scale=2 "
                        "(render-test.txt; frames land in the run folder's frames)")
    a.add_argument("--render-presentation", type=pathlib.Path,
                   help="R1 --render only: four-key [Graphics] UI presentation file; appended temporarily to the exact test profile")
    a.add_argument("--existing-license", action="store_true",
                   help="R1 --render only: existing two-file bank; bounded script, no fresh-license actions or fallback")
    a.add_argument("--check-only", action="store_true", help="preflight only: no lease, no files, no launch")
    a.add_argument("--lease-held", action="store_true", help="the caller holds the shared rig lease through this run and its "
                   "evidence (seal, replay, frame conversion) and releases it; run.py neither takes nor releases it")
    a.add_argument("--horn", action="store_true", help="R1 only: this run binds horn (R12 button 27) with [Audio] AllowHorn = true "
                   "and holds it 1.5 s after the steer holds; the exit result log's race sound line must show the 9BC loop and the "
                   "81BC stop as played (refused or missing is a request only; the command path, not an audible check)")
    o = a.parse_args()
    if o.prepare_from and (o.render_presentation or o.existing_license):
        a.error("prepare the unchanged test profile first; render flags are for run/preflight only")
    if o.prepare_from: prepare(o.prepare_from, o.game, o.candidate); return
    if not o.result: a.error("--result is required for a run or a preflight")
    out = o.result.resolve()
    problems = []
    publisher = publisher_tool(o.publisher_tool, o.candidate)
    if not publisher.is_file(): problems.append("no control publisher at %s (build control_publish or supply --publisher-tool)" % publisher)
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
    consumed = o.consumed_replay is not None
    if consumed and o.replay_of: problems.append("--consumed-replay and --replay-of are exclusive")
    if o.horn and (consumed or o.replay_of): problems.append("--horn is an R1 step only (no --replay-of or --consumed-replay)")
    if (o.drive or o.render) and (consumed or o.replay_of or o.horn): problems.append("--drive and --render are R1 only (no replay, no --horn)")
    presentation = ""; presentation_raw = b""
    if o.render_presentation or o.existing_license:
        if not o.render or consumed or o.replay_of or o.horn:
            problems.append("--render-presentation and --existing-license require R1 --render (no replay or --horn)")
    if o.render_presentation:
        try:
            if o.render_presentation.stat().st_size > 4096: raise ValueError("render presentation exceeds 4096 bytes")
            presentation_raw = o.render_presentation.read_bytes()
            presentation = render_presentation(presentation_raw.decode("utf-8"))
            folder = re.search(r"^TextureBaseFolder = (.+)$", presentation, re.M).group(1)
            if not (o.game / folder / "load").is_dir(): problems.append("render presentation pack folder absent")
        except (OSError, UnicodeError, ValueError) as error: problems.append(str(error))
    if o.existing_license:
        try:
            why = existing_license_problem(o.game)
            if why: problems.append(why)
        except OSError as error: problems.append("cannot inspect existing license: %s" % error)
    menu_script = EXISTING_LICENSE_SCRIPT if o.existing_license else MENU_SCRIPT
    if o.drive and not 15 <= o.drive <= 120: problems.append("--drive is 15..120 seconds")
    try: render_request(o.render, "preflight")
    except ValueError as e: problems.append(str(e))
    if not consumed and (not o.replay_tool or not o.replay_tool.exists()): problems.append("no --replay-tool capture_replay.exe to replay the capture")
    tape = None; tape_rows = 0; tape_source = None; replay_timeout = None; reference = None
    if consumed:   # the same candidate plays its own saved recording, exported fresh from its recording seal (sana 35159)
        reference = o.consumed_replay.resolve(); tape = out / "tape.tsv"
        try:
            proof = json.loads((reference / "capture" / "recorder-complete.json").read_text())
            if not (reference / "capture" / "recording-complete.tsv").exists(): problems.append("--consumed-replay has no recording seal")
            if not o.candidate.exists() or proof.get("exeSha256") != sha(o.candidate):
                problems.append("--consumed-replay was recorded by another build (%s, this run %s)" % (str(proof.get("exeSha256"))[:16], sha(o.candidate)[:16] if o.candidate.exists() else None))
            with tempfile.TemporaryDirectory() as t:   # the preflight writes nothing it keeps
                tape_rows = export_recording(o.finalizer, reference / "capture", pathlib.Path(t) / "tape.tsv")
            replay_timeout = tape_rows / 30.0 + 60.0   # a cold cache's slow frames: at least half the recorded rate
        except (OSError, ValueError, KeyError) as e: problems.append("--consumed-replay unusable: %s" % e)
    if o.replay_of:   # R3: only a passing first run whose sealed capture verifies again is played
        first = o.replay_of.resolve(); tape_source = first / "run" / "capture"; tape = out / "tape.tsv"
        try:
            r1 = json.loads((first / "receipt.json").read_text())
            if r1.get("result") != "pass": problems.append("--replay-of is not a passing run")
            r1_route = r1.get("route") or {}
            if not r1_route.get("scriptedOnly"): problems.append("--replay-of reached its race through fallback presses: its course and car are not established")
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
    for f in ("inject.on", "inject.txt", "render-test.txt"):
        if (DBCE / f).exists(): problems.append("leftover %s in %s" % (f, DBCE))
    running = subprocess.run(["tasklist", "/FI", "IMAGENAME eq OutRunDesktop.exe", "/NH"], capture_output=True, text=True).stdout
    if "OutRunDesktop.exe" in running: problems.append("OutRunDesktop is already running")
    plan = dict(game=str(o.game), candidate=str(o.candidate), candidateSha256=sha(o.candidate) if o.candidate.exists() else None,
                publisherTool=str(publisher), publisherSha256=sha(publisher) if publisher.is_file() else None,
                finalizer=str(o.finalizer), finalizerSha256=sha(o.finalizer) if o.finalizer and o.finalizer.exists() else None,
                replayTool=str(o.replay_tool), replayToolSha256=sha(o.replay_tool) if o.replay_tool and o.replay_tool.exists() else None,
                replayOf=str(o.replay_of) if o.replay_of else None, tapeSource=str(tape_source) if tape_source else None, tapeRows=tape_rows,
                replayTimeoutSeconds=replay_timeout, raceBudgetSeconds=race_budget(replay_timeout, o.horn, o.drive) if not o.replay_of or replay_timeout is not None else None,
                horn=o.horn, drive=o.drive, render=o.render, dbce=str(DBCE), seconds=o.seconds,
                renderPresentation=str(o.render_presentation) if o.render_presentation else None,
                renderPresentationSha256=hashlib.sha256(presentation_raw).hexdigest() if presentation else None,
                effectiveProfileSha256=hashlib.sha256((PROFILE + presentation).encode("utf-8")).hexdigest() if presentation else None,
                menuScript=[list(step) for step in menu_script], existingLicense=o.existing_license, problems=problems)
    if o.check_only or problems:
        print(json.dumps(plan, indent=1)); sys.exit(1 if problems else 0)
    out.mkdir(parents=True)
    if tape and consumed:   # the recording's fresh export, verified again: the tape the seam plays
        if export_recording(o.finalizer, reference / "capture", tape) != tape_rows: raise SystemExit("the saved recording changed after the preflight")
        plan["tapeSha256"] = sha(tape); plan["consumedReplay"] = str(reference)
    elif tape:   # the run's own fresh export of the verified sealed source: the tape the game plays and the report compares
        read_export(o.finalizer, tape_source, tape); plan["tapeSha256"] = sha(tape)
        if len(compare_replay.tape(str(tape))) != tape_rows: raise SystemExit("the sealed source changed after the preflight")
    receipt = dict(plan=plan, started=time.strftime("%Y-%m-%dT%H:%M:%S%z"), presses=[], batches=[], forced=False, errors=[],
                   injection="inject action <id> <n>: the shim resolves each action through the test profile's declared binding "
                             "inverse into a raw sample of that bound R12 object; not a physical hardware trial (Astra 16520)")
    if consumed:
        receipt["consumed"] = dict(playbackComplete=False, playbackEnd=None, compare=None, reference=str(reference),
            label="consumed playback: the saved recording's final.* words put back at 453BB0 from the first input tick (no presses, "
                  "no live input); its scope is the comparison's (compare.pose.scope): input and startup, and the player car's "
                  "pose when the recording carries it")
    elif tape:
        receipt["r3"] = dict(playbackComplete=False, playbackEnd=None, compare=None, firstRoute=r1.get("route"),
            label="menus: injected presses as above; race: the first run's recorded normalized Frame actions, played at the adapter "
                  "boundary while the live latched session is Running (Astra 16754): not physical input, not a raw-to-Core replay, no route claim")
    nonce = "".join(random.choices(string.ascii_letters + string.digits, k=16))
    proc = None; run_root = None; leased = False; created = 0; wrote_ini = False
    published = {}                               # only files this invocation wrote, with their last bytes
    def publish(name, data):
        if sha(publisher) != plan["publisherSha256"]: raise RuntimeError("control publisher changed after preflight")
        replace_file(DBCE / name, data, publisher); published[name] = data
    try:
        if not o.lease_held: lease("Enter", "OutRun shim first loop (latched, no force)")
        leased = True
        if tape:   # the test copy's ini names the tape for this run only; restored to the exact profile bytes after it
            wrote_ini = True
            ini.write_bytes((PROFILE + "[ShimReplay]\nTape = %s\n%s" % (tape, "Mode = consumed\n" if consumed else "")).encode("utf-8"))
        elif o.horn:   # the horn binding and AllowHorn for this run only; restored to the exact profile bytes after it
            wrote_ini = True
            ini.write_bytes(HORN_INI.encode("utf-8"))
            receipt["horn"] = dict(sentAt=None, line=None, played=None, refused=None, missing=None, loop=False, stop=False,
                label="AllowHorn command path: one 'inject action horn 1 ms=1500' after the steer holds; the exit result log's race "
                      "sound line (42F0D0 played/refused/missing per command) must show the 9BC loop and the 81BC stop played; "
                      "refused or missing is a request only; not an audible check")
        elif presentation:
            # Same temporary-profile/restore boundary as --horn; no binding or fence change.
            if sha(o.render_presentation) != plan["renderPresentationSha256"]:
                raise RuntimeError("render presentation changed after preflight")
            wrote_ini = True
            ini.write_bytes((PROFILE + presentation).encode("utf-8"))
            if sha(ini) != plan["effectiveProfileSha256"]: raise RuntimeError("render profile write mismatch")
        DBCE.mkdir(parents=True, exist_ok=True)
        start = time.time(); budget = race_budget(replay_timeout, o.horn, o.drive)
        if o.render:   # before inject.on, so a latched start always finds it; the game refuses the run if it does not admit it
            publish("render-test.txt", render_request(o.render, nonce))
            receipt["render"] = dict(request=o.render, frames=None,
                label="synthetic input (inject action, the test profile's bindings), not owner play or a physical trial; the frames "
                      "are the game's own back buffer, written by the latched run into its run folder")
        publish("inject.on",("nonce=%s\nexpires=%d\n" % (nonce, inject_expiry(start, o.seconds, budget))).encode())
        proc = subprocess.Popen([str(o.game / "OutRunDesktop.exe")] + ([] if render_span(o.render) else ["--windowed"]), cwd=str(o.game))
        receipt["pid"] = proc.pid; created = creation_time(int(proc._handle)); receipt["creationTime"] = created
        run_root = DBCE / ("run-%s-%d" % (nonce, proc.pid)); log = run_root / "runtime.log"
        seen = 0; mode = None; race = False; n = 0; last_press = 0; lines = []
        def batch(commands):
            nonlocal n
            n += 1; body = "nonce=%s\nbatch=%d\n%s\n" % (nonce, n, "\n".join(commands))
            publish("inject.txt", body.encode()); receipt["batches"].append(dict(n=n, commands=commands, at=time.time()))
        phase = "consumed" if consumed else "menus"; phase_at = 0; race_at = None; window_at = None; menu_at = None; scripted = 0
        consumed_deadline = start + o.seconds + (replay_timeout or 0)
        while time.time() < (consumed_deadline if consumed else loop_deadline(start, o.seconds, race_at, budget)) and proc.poll() is None:
            time.sleep(0.2)
            if log.exists():
                text = log.read_text(encoding="utf-8", errors="replace"); new = text.splitlines()[seen:]; seen += len(new); lines += new
                for l in new:
                    m = SIGNAL.search(l)
                    if m: mode = int(m.group(3)); race = m.group(4) is not None
                    ce = CONSUMED_END.search(l)
                    if ce and consumed:
                        receipt["consumed"]["playbackEnd"] = l.strip(); phase_at = phase_at or time.time()
                        receipt["consumed"]["playbackComplete"] = ce.group(3) == ce.group(4) and int(ce.group(4)) == tape_rows
                    e = REPLAY_END.search(l)
                    if e and tape and not consumed:
                        receipt["r3"]["playbackEnd"] = l.strip()
                        receipt["r3"]["playbackComplete"] = e.group(2) == e.group(3) and int(e.group(3)) == tape_rows and e.group(4) == "tape complete"
            if phase == "consumed":   # no presses: the tape drives from the first tick; end a moment after its last row
                if race and race_at is None: race_at = time.time()
                if window_at is None and windows_of(proc.pid): window_at = time.time(); receipt["windowSeconds"] = round(window_at - start, 3)
                if receipt["consumed"]["playbackEnd"] and time.time() - phase_at > 2.0:
                    phase = "end"; batch(["end"]); break
                continue
            if phase == "menus":
                if race: race_at = time.time()
                if race and tape: phase, phase_at = "replay", time.time(); continue   # the shim plays the tape from the race entry
                if race and o.drive: phase, phase_at, drive_step = "drive", time.time(), 0; batch(drive_batch(0)); continue
                if race: phase, phase_at = "steer-right", time.time(); batch(["inject action steer 0.6 ms=3000", "inject action throttle 1 ms=3000"]); continue
                if window_at is None and windows_of(proc.pid): window_at = time.time(); receipt["windowSeconds"] = round(window_at - start, 3)
                if menu_at is None and window_at is not None and mode == 32: menu_at = time.time(); receipt["menuSeconds"] = round(menu_at - start, 3)
                if (o.existing_license and menu_at is not None and scripted == len(menu_script) and
                        time.time() - menu_at > menu_script[-1][0] + MENU_FALLBACK_AFTER):
                    raise RuntimeError("existing-license script did not reach a race; no fallback presses")
                action = next_menu_action(menu_script, None if menu_at is None else time.time() - menu_at, scripted,
                                          time.time() - last_press, len(receipt["presses"]), allow_fallback=not o.existing_license)
                if action:
                    batch(["inject action %s 1 ms=200" % action]); last_press = time.time()
                    receipt["presses"].append(dict(action=action, mode=mode, at=round(last_press - window_at, 3), scripted=scripted < len(menu_script)))
                    if scripted < len(menu_script): scripted += 1
            elif phase == "drive":
                if time.time() - phase_at >= o.drive: phase = "end"; batch(["end"]); break
                if int((time.time() - phase_at) / DRIVE_STEP) > drive_step:
                    drive_step = int((time.time() - phase_at) / DRIVE_STEP); batch(drive_batch(drive_step))
            elif phase == "steer-right" and time.time() - phase_at > 3.5:
                phase, phase_at = "steer-left", time.time(); batch(["inject action steer -0.6 ms=3000", "inject action throttle 1 ms=3000"])
            elif phase == "steer-left" and time.time() - phase_at > 3.5:
                if o.horn:
                    phase, phase_at = "horn", time.time(); batch(["inject action horn 1 ms=1500"]); receipt["horn"]["sentAt"] = phase_at
                else: phase = "end"; batch(["end"]); break
            elif phase == "horn" and time.time() - phase_at > HORN_SECONDS:
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
                if o.horn: retain(proc, receipt)
                else:
                    # A latched process whose stop did not resolve parks for its supervisor (P21): this runner ends it.
                    receipt["forced"] = True; proc.kill(); proc.wait(timeout=15)
        receipt["exitCode"] = proc.returncode
    except Exception as e:
        receipt["errors"].append(repr(e))
        if proc and proc.poll() is None:
            for h in windows_of(proc.pid): user32.PostMessageW(h, 0x0010, 0, 0)
            try: proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                if o.horn: retain(proc, receipt)
                else: receipt["forced"] = True; proc.kill()
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
            if leased and receipt.get("retained"):   # --horn: the retained process still runs on the rig; the lease stays with it
                receipt["retained"]["lease"] = "kept: Rig-Lease -Exit after the process is resolved"
            elif leased and not o.horn and not o.lease_held:
                try: lease("Exit")
                except Exception as e: receipt["errors"].append(repr(e))
        try:
            if consumed:   # the run's own @2 capture is evidence, compared with the recording (not P21-sealed: no recording seal)
                receipt["seal"] = "not sealed (consumed playback run: its @2 capture is compared, not sealed)"
                try:
                    q = subprocess.run([sys.executable, "-B", str(HERE / "compare_consumed.py"), str(reference / "capture" / "source.jsonl"),
                                        str(out / "run" / "capture" / "source.jsonl")], capture_output=True, text=True, timeout=600)
                    receipt["consumed"]["compare"] = json.loads(q.stdout) if q.returncode == 0 else (q.stdout + q.stderr).strip()[-400:]
                except Exception as e: receipt["errors"].append("consumed compare: %r" % e)
            else:
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
            receipt["route"] = route_of(receipt, (out / "run" / "runtime.log").read_text(encoding="utf-8", errors="replace") if (out / "run" / "runtime.log").exists() else "")
            if receipt.get("render") is not None:   # what the game wrote; the pictures are judged by eye, not here
                frames = out / "run" / "frames"
                receipt["render"]["frames"] = len(list(frames.glob("frame-*.ppm"))) if frames.is_dir() else 0
            if receipt.get("horn") is not None:
                receipt["horn"].update(horn_evidence((out / "run" / "runtime.log").read_text(encoding="utf-8", errors="replace") if (out / "run" / "runtime.log").exists() else ""))
            failures = verdict(receipt)
            (out / "receipt.json").write_text(json.dumps(receipt, indent=1, default=str))
            print(json.dumps({k: receipt.get(k) for k in ("result", "failures", "pid", "exitCode", "forced", "retained", "phase", "race", "modes", "maxRecordSteer",
                                                         "maxConsumedSteer", "maxConsumedAccel", "seal", "replay", "r3")}, indent=1))
        finally:
            # --horn (sana #132742): the lease is held through the seal, replay, evidence and receipt, and released only after
            # all of it (also when that work raised); a retained process keeps it. A failed release is added to the receipt.
            if leased and o.horn and not o.lease_held and not receipt.get("retained"):
                try: lease("Exit")
                except Exception as e:
                    receipt["errors"].append("lease exit: %r" % e); failures = verdict(receipt)
                    (out / "receipt.json").write_text(json.dumps(receipt, indent=1, default=str))
                    print(json.dumps(dict(result=receipt.get("result"), failures=failures), indent=1))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
