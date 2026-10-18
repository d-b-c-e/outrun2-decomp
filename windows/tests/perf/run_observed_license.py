"""One ordinary OutRun route gated by the game's opt-in, passive frontend trace.

The key messages are the same native PostMessage scan-code path as run_perf.py.
No stage is inferred from a posted key: the game must report the transition.
"""
import argparse
import csv
import datetime
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time

import run_perf as base

FIELDS = ("seq", "pid", "run", "ms", "mode", "startStage", "editorState", "item",
          "focused", "ready", "accepted", "selected", "nameLen", "doneMoves", "saves", "loaded",
          "childState", "menuIndex", "choiceResult")
# Only the title transition is timed. Once the editor opens, all license
# navigation uses observed state; the old 11-12.8 s Down/Enter sequence can
# otherwise select Done and save an unnamed profile before the keyboard opens.
PREFIX = ((6.0, "enter"), (9.0, "enter"))


class StageError(Exception):
    pass


class RouteTrace:
    def __init__(self, path, pid, run_id):
        self.path, self.pid, self.run_id = path, pid, run_id
        self.position, self.partial, self.last_seq, self.last = 0, b"", 0, None
        self.rows = []

    def poll(self):
        try:
            with self.path.open("rb") as file:
                file.seek(self.position)
                data = file.read()
        except FileNotFoundError:
            return self.last
        self.position += len(data)
        *lines, self.partial = (self.partial + data).split(b"\n")
        for raw in lines:
            values = next(csv.reader([raw.decode("ascii", "strict")], delimiter="\t"))
            if self.last_seq == 0 and self.last is None and values == list(FIELDS):
                continue
            if len(values) != len(FIELDS):
                raise StageError("frontend trace has a malformed complete row")
            row = dict(zip(FIELDS, values))
            try:
                seq, pid = int(row["seq"]), int(row["pid"])
                for key in ("ms", "mode", "startStage", "editorState", "item", "focused", "ready",
                            "accepted", "selected", "nameLen", "saves", "loaded", "childState",
                            "menuIndex", "choiceResult"):
                    row[key] = int(row[key])
            except ValueError as error:
                raise StageError("frontend trace has a noninteger field") from error
            if pid != self.pid or row["run"] != self.run_id or seq != self.last_seq + 1:
                raise StageError("frontend trace identity or sequence mismatch")
            row["seq"], row["pid"] = seq, pid
            self.last_seq, self.last = seq, row
            self.rows.append(row)
        return self.last


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_brake_coast(hwnd, proc, receipt, start, race_time, seconds, persist):
    """Post the opt-in pedal sequence; telemetry analysis owns acceptance."""
    receipt["phaseClock"] = {"epochSeconds": time.time(), "monotonic": time.monotonic(),
                             "runStartMonotonic": start}
    receipt["phases"] = []

    def check(name):
        if proc.poll() is not None:
            raise StageError(f"brake-coast-{name}: game exited {proc.returncode}")
        if hwnd not in base.windows_of(proc.pid):
            raise StageError(f"brake-coast-{name}: game window disappeared")

    def edge(name, up):
        entry = {"key": name, "posted": round(time.monotonic() - start, 6), "up": up}
        try:
            base.post_key(hwnd, name, up)
        except (OSError, ValueError) as error:
            entry["error"] = str(error)
            raise
        finally:
            receipt["keysSent"].append(entry)
            persist()

    def hold(name, deadline):
        while True:
            check(name)
            remaining = deadline - time.monotonic()
            if remaining <= 0: return
            time.sleep(min(.05, remaining))

    try:
        deadline = race_time
        for name, duration, edges in (
                ("throttle", 12, (("up", False),)),
                ("coast", 3, (("up", True),)),
                ("throttle-again", 4, (("up", False),)),
                ("brake", 3, (("up", True), ("down", False))),
                ("neutral", 3, (("down", True),))):
            check(name)
            deadline += duration
            if time.monotonic() >= deadline:
                raise StageError(f"brake-coast-{name}: phase deadline exceeded")
            phase = {"phase": name, "start": round(time.monotonic() - start, 6), "end": None,
                     "scheduledEnd": round(deadline - start, 6)}
            receipt["phases"].append(phase)
            persist()
            try:
                for key_name, up in edges: edge(key_name, up)
                # Fixed monotonic phase boundaries avoid accumulating receipt
                # write overhead beyond the requested race hold. Actual edges
                # and start/end times, rather than nominal durations, are saved.
                hold(name, deadline)
                phase["complete"] = True
            finally:
                phase["end"] = round(time.monotonic() - start, 6)
                persist()
        hold("neutral-rest", race_time + seconds)
    finally:
        # Attempt each release independently, even if posting the other fails.
        for name in ("up", "down"):
            try: edge(name, True)
            except (OSError, ValueError) as error:
                receipt["errors"].append(f"brake-coast-release-{name}: {error}")


def main():
    a = argparse.ArgumentParser(description=__doc__)
    a.add_argument("--game", type=pathlib.Path, required=True)
    a.add_argument("--result", type=pathlib.Path, required=True)
    a.add_argument("--lease-owner", required=True)
    a.add_argument("--run-id", required=True)
    a.add_argument("--port", type=int, default=18765)
    a.add_argument("--seconds", type=float, default=75.0, help="time after observed race entry")
    a.add_argument("--weave", type=float, default=0.0,
                   help="seconds to hold the accelerator after race entry, steering right and left in turn after 12 s so "
                        "traffic passes on both sides (0: the short drive)")
    a.add_argument("--brake-coast", action="store_true",
                   help="opt-in 25 s throttle/coast/throttle/brake/neutral sequence; no steering")
    a.add_argument("--check-only", action="store_true")
    o = a.parse_args()
    game, out = o.game.resolve(), o.result.resolve()
    exe, save = game / "OutRunDesktop.exe", game / "SaveGame"
    problems = []
    if not o.run_id or any(ch not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_" for ch in o.run_id):
        problems.append("run ID must use ASCII letters, numbers, dash or underscore")
    if any(part in str(game).lower() for part in base.REFUSED_PARTS):
        problems.append("refused owner/Steam game folder")
    if not exe.is_file() or not (game / "OR2006C2C.exe").is_file():
        problems.append("disposable game or data absent")
    if (game / "dinput8.dll").exists(): problems.append("local dinput8.dll would intercept native input")
    if out.exists(): problems.append("result folder already exists")
    existing_files = {p.name for p in save.iterdir()} if save.is_dir() else set()
    fresh = not existing_files
    if not save.is_dir() or (existing_files and existing_files != {"License1.dat", "common.dat"}):
        problems.append("SaveGame must be empty or contain only License1.dat and common.dat")
    if (game / "WheelFfb.dll").exists(): problems.append("force DLL present")
    if base.INJECT_ON.exists(): problems.append("recording latch present")
    if base.lease_problem(o.lease_owner): problems.append(base.lease_problem(o.lease_owner))
    if os.environ.get("DBCE_FFB_MUTE") != "1": problems.append("DBCE_FFB_MUTE=1 required")
    if o.port == 8000 or not 1 <= o.port <= 65535: problems.append("invalid/private telemetry port")
    if not 15 <= o.seconds <= 180: problems.append("seconds outside 15..180")
    if o.weave and not 15 <= o.weave <= o.seconds: problems.append("weave outside 15..seconds")
    if o.brake_coast and o.weave: problems.append("brake-coast cannot be combined with weave")
    if o.brake_coast and o.seconds < 25: problems.append("brake-coast requires seconds >= 25")
    if "OutRunDesktop.exe" in subprocess.run(["tasklist", "/FI", "IMAGENAME eq OutRunDesktop.exe", "/NH"],
                                             capture_output=True, text=True).stdout:
        problems.append("OutRunDesktop already running")
    if o.check_only or problems:
        print(json.dumps({"game": str(game), "result": str(out), "exeSha256": sha(exe) if exe.exists() else None,
                          "runId": o.run_id, "port": o.port, "freshLicense": fresh, "problems": problems}, indent=2))
        return 1 if problems else 0

    out.mkdir(parents=True)
    trace_path = out / "frontend-state.tsv"
    env = dict(os.environ, OR2_TELEMETRY="1", OR2_TELEMETRY_PORT=str(o.port),
               OR2_FRONTEND_STATE_TRACE=str(trace_path), OR2_FRONTEND_RUN_ID=o.run_id)
    receipt = {"started": datetime.datetime.now().astimezone().isoformat(), "game": str(game),
               "exeSha256": sha(exe), "pid": None, "runId": o.run_id, "port": o.port,
               "env": {key: env[key] for key in ("OR2_TELEMETRY", "OR2_TELEMETRY_PORT", "OR2_FRONTEND_STATE_TRACE", "OR2_FRONTEND_RUN_ID", "DBCE_FFB_MUTE")},
               "keysSent": [], "stages": [], "errors": [], "forced": False}
    receipt["freshLicense"] = fresh
    baseline_save = {name: sha(save / name) for name in existing_files} if not fresh else {}
    proc = None
    start = time.monotonic()
    trace = None
    window_time = None
    last_key_at = None

    def persist():
        (out / "route.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")

    def stage(name, row=None):
        receipt["stages"].append({"stage": name, "t": round(time.monotonic() - start, 3), "status": row})
        persist()

    def wait_for(name, predicate, limit):
        deadline = time.monotonic() + limit
        while time.monotonic() < deadline:
            if proc.poll() is not None: raise StageError(f"{name}: game exited {proc.returncode}")
            row = trace.poll()
            if row and predicate(row):
                stage(name, row)
                return row
            time.sleep(.05)
        raise StageError(f"{name}: timeout after {limit:g}s; last={trace.last}")

    def key(name, hold=.20):
        nonlocal last_key_at
        if proc.poll() is not None: raise StageError(f"key-{name}: game exited")
        # The observed menu accepted the original half-second spaced actions;
        # a second immediate Enter on the name keyboard did not register.
        if last_key_at is not None:
            time.sleep(max(0.0, .5 - (time.monotonic() - last_key_at)))
        last_key_at = time.monotonic()
        posted = round(time.monotonic() - start, 3)
        base.post_key(hwnd, name, False)
        try: time.sleep(hold)
        finally: base.post_key(hwnd, name, True)
        receipt["keysSent"].append({"key": name, "posted": posted, "hold": hold})
        persist()

    try:
        launched = time.monotonic()
        proc = subprocess.Popen([str(exe)], cwd=str(game), env=env)
        receipt["pid"] = proc.pid
        hwnd = None
        while time.monotonic() - launched < 90 and proc.poll() is None:
            windows = base.windows_of(proc.pid)
            if windows: hwnd = windows[0]; break
            time.sleep(.1)
        if hwnd is None: raise StageError("window: no game window within 90s")
        receipt["hwnd"] = hwnd
        window_time = time.monotonic()
        trace = RouteTrace(trace_path, proc.pid, o.run_id)
        wait_for("trace-initial", lambda r: r["seq"] >= 1, 10)
        license_file, common_file = save / "License1.dat", save / "common.dat"
        if fresh:
            for due, name in PREFIX:
                while time.monotonic() - window_time < due:
                    if proc.poll() is not None: raise StageError("title-prefix: game exited")
                    time.sleep(.02)
                key(name, .12)
            row = wait_for("editor-open", lambda r: (r["editorState"] == 1 and r["item"] == 0) or
                           (r["editorState"] == 2 and r["focused"] == 1), 20)
            if row["editorState"] == 1:
                key("enter")
            row = wait_for("keyboard-ready", lambda r: r["editorState"] == 2 and r["focused"] == 1 and r["ready"] == 1, 15)
            # Existing license-name text may be prefilled by the editor. Only
            # add a character when the observed name is blank; rapid repeated
            # Enter on one cell can be ignored by the game's key debounce.
            if row["nameLen"] == 0:
                if row["selected"] == 43: raise StageError("keyboard-name: Done selected before a name")
                key("enter")
                row = wait_for("keyboard-name", lambda r: r["editorState"] == 2 and r["nameLen"] > 0, 4)
            if row["nameLen"] < 1: raise StageError("keyboard-name: no observed characters")
            for _ in range(44):
                if row["selected"] == 43: break
                moves = row["doneMoves"]
                if not moves or moves == "-": raise StageError("keyboard-done-path: no path from observed cell")
                old = row["selected"]
                name = {"L": "left", "U": "up", "R": "right", "D": "down"}[moves[0]]
                key(name)
                row = wait_for("keyboard-navigate", lambda r: r["editorState"] == 2 and r["selected"] != old, 3)
            if row["selected"] != 43: raise StageError("keyboard-done-path: exceeded 44 moves")
            key("enter")
            row = wait_for("name-accepted", lambda r: r["accepted"] == 1 and r["editorState"] == 1, 12)
            for _ in range(3):
                if row["item"] == 4: break
                old = row["item"]
                key("down")
                row = wait_for("editor-done-item", lambda r: r["editorState"] == 1 and r["item"] != old, 4)
            if row["item"] != 4: raise StageError("editor-done-item: did not reach item 4")
            key("enter")
            row = wait_for("license-saved", lambda r: r["saves"] >= 1 and r["loaded"] >= 1 and
                           license_file.is_file() and license_file.stat().st_size > 0 and
                           common_file.is_file() and common_file.stat().st_size > 0, 15)
            receipt["saveWitness"] = {str(p): {"bytes": p.stat().st_size, "sha256": sha(p)} for p in (license_file, common_file)}
            persist()
            # The completed editor remains at state 7 in the retained owner
            # storage even after its save result is consumed. The save/files
            # above, not a reset to state 0, are the completion witness.
            if row["editorState"] != 7: raise StageError("editor-complete: no completed editor state")
            stage("editor-complete", row)
        else:
            # The welcome owner queries profiles only after title input
            # (frontend_welcome.cpp: case 2 / native_runtime.cpp: 0x416380).
            # A loaded bank goes directly to child 1; do not send the fresh
            # route's second Enter into that menu.
            wait_for("existing-title-ready", lambda r: r["mode"] == 32 and r["childState"] == 0, 10)
            due, name = PREFIX[0]
            while time.monotonic() - window_time < due:
                if proc.poll() is not None: raise StageError("existing-title-advance: game exited")
                time.sleep(.02)
            key(name, .12)
            row = wait_for("existing-title-advanced", lambda r: r["loaded"] >= 1 or r["childState"] != 0, 20)
            if row["childState"] == 21:
                # Key 21 is the chooser, whose +0x38 state and +0x44 cursor
                # are not in this executable's trace: `selected` belongs to
                # the name keyboard. Never guess an existing/new-slot action.
                raise StageError("existing-license-chooser: chooser state/cursor unavailable in frontend trace")
            row = wait_for("existing-license-loaded", lambda r: r["loaded"] >= 1, 20)
            receipt["saveWitness"] = {str(p): {"bytes": p.stat().st_size, "sha256": baseline_save[p.name]}
                                      for p in (license_file, common_file)}
            persist()
        # Fixed-choice key 2 defaults to cursor 0, which opens C2C categories
        # and missions. The original successful route moved Right first. Gate
        # that selection on the active child and its observed cursor.
        row = wait_for("first-choice", lambda r: r["childState"] in (1, 2), 15)
        if row["childState"] == 1:
            time.sleep(2)  # its authored opening animation before native input
            key("enter")
            row = wait_for("second-choice", lambda r: r["childState"] == 2, 12)
        if row["menuIndex"] not in (0, 1): raise StageError(f"second-choice: unexpected cursor {row['menuIndex']}")
        if row["menuIndex"] == 0:
            time.sleep(2)
            key("right")
            row = wait_for("outrun-cursor", lambda r: r["childState"] == 2 and r["menuIndex"] == 1, 4)
        time.sleep(.8)
        key("enter")
        row = wait_for("outrun-choice", lambda r: r["childState"] in (36, 58, 59), 12)
        if row["childState"] != 36: raise StageError(f"outrun-choice: wrong mode child {row['childState']}")
        for target, label in ((10, "car"), (16, "transmission"), (4, "music")):
            time.sleep(2)
            key("enter")
            row = wait_for(f"{label}-choice", lambda r, target=target: r["childState"] == target, 12)
        time.sleep(2)
        key("enter")
        row = wait_for("start-entry", lambda r: r["mode"] in (13, 16), 15)
        row = wait_for("race-game", lambda r: r["mode"] == 16, 45)
        stage("race-entry", row)
        race_time = time.monotonic()
        if o.brake_coast:
            run_brake_coast(hwnd, proc, receipt, start, race_time, o.seconds, persist)
        else:
            posted = round(time.monotonic() - start, 3)
            base.post_key(hwnd, "up", False)
            try:
                if o.weave:
                    time.sleep(12)
                    side = "right"
                    while time.monotonic() - race_time < o.weave - 4 and proc.poll() is None:
                        key(side, 1.5)
                        side = "left" if side == "right" else "right"
                        time.sleep(2.5)
                    while time.monotonic() - race_time < o.weave and proc.poll() is None: time.sleep(.05)
                else:
                    time.sleep(3)
                    key("left", 1.5)
                    time.sleep(1.5)
                    key("right", 1.5)
                    time.sleep(2.5)
            finally:
                base.post_key(hwnd, "up", True)
                receipt["keysSent"].extend(({"key": "up", "posted": posted, "up": False},
                                            {"key": "up", "posted": round(time.monotonic() - start, 3), "up": True}))
                persist()
        while time.monotonic() - race_time < o.seconds and proc.poll() is None: time.sleep(.05)
        if proc.poll() is not None: raise StageError("race-hold: game exited itself")
        stage("race-hold-complete", trace.poll())
    except (StageError, OSError, ValueError) as error:
        receipt["errors"].append(str(error))
        stage("failed", trace.last if trace else None)
    finally:
        if proc and proc.poll() is None:
            for window in base.windows_of(proc.pid): base.user32.PostMessageW(window, base.WM_CLOSE, 0, 0)
            try: proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                receipt["forced"] = True
                proc.kill(); proc.wait(timeout=15)
        receipt["exitCode"] = proc.returncode if proc else None
        if not fresh:
            for name, expected in baseline_save.items():
                file = save / name
                if not file.exists() or sha(file) != expected:
                    receipt["errors"].append(f"existing license changed: {name}")
        for name in ("runtime.log", "OutRunXbox.log"):
            file = game / name
            if file.exists(): shutil.copy2(file, out / name)
            else: receipt["errors"].append(f"missing {name}")
        log = out / "OutRunXbox.log"
        receipt["profile"] = base.parse_profile(log.read_text(errors="replace")) if log.exists() else None
        if receipt["profile"] is None: receipt["errors"].append("no race-mode frame profile")
        receipt["result"] = "pass" if not receipt["errors"] and not receipt["forced"] and receipt["exitCode"] == 0 else "fail"
        persist()
    print(json.dumps({key: receipt.get(key) for key in ("result", "errors", "exitCode", "forced", "stages")}, indent=2))
    return 0 if receipt["result"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
