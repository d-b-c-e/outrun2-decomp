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
          "focused", "ready", "accepted", "selected", "nameLen", "doneMoves", "saves", "loaded")
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
                            "accepted", "selected", "nameLen", "saves", "loaded"):
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


def main():
    a = argparse.ArgumentParser(description=__doc__)
    a.add_argument("--game", type=pathlib.Path, required=True)
    a.add_argument("--result", type=pathlib.Path, required=True)
    a.add_argument("--lease-owner", required=True)
    a.add_argument("--run-id", required=True)
    a.add_argument("--port", type=int, default=18765)
    a.add_argument("--seconds", type=float, default=75.0, help="time after observed race entry")
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
        if proc.poll() is not None: raise StageError(f"key-{name}: game exited")
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
            # The retail keyboard opens blank. Enter on the selected digit uses
            # its normal key action; require each character in the game trace.
            for index in range(3):
                if row["selected"] == 43: raise StageError("keyboard-name: Done selected before a name")
                old_length = row["nameLen"]
                key("enter")
                row = wait_for(f"keyboard-name-{index+1}",
                               lambda r: r["editorState"] == 2 and r["nameLen"] > old_length, 4)
            if row["nameLen"] < 3: raise StageError("keyboard-name: fewer than three observed characters")
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
            # The license owner exits through its own animation. Only then use
            # native Enter to continue the original frontend, bounded to four.
            wait_for("editor-exit", lambda r: r["editorState"] in (0, -1) or r["mode"] == 16, 15)
        else:
            row = wait_for("existing-license-loaded", lambda r: r["loaded"] >= 1, 20)
            receipt["saveWitness"] = {str(p): {"bytes": p.stat().st_size, "sha256": baseline_save[p.name]}
                                      for p in (license_file, common_file)}
            persist()
        row = trace.last
        for attempt in range(4):
            if row["mode"] == 16 or (row["mode"] == 13 and row["startStage"] >= 60): break
            key("enter")
            try: row = wait_for(f"race-menu-{attempt+1}", lambda r: r["mode"] == 16 or (r["mode"] == 13 and r["startStage"] >= 60), 2)
            except StageError as error:
                if "timeout" not in str(error): raise
                row = trace.poll()
        if row["mode"] != 16 and not (row["mode"] == 13 and row["startStage"] >= 60):
            raise StageError(f"race-entry: no observed transition after four native confirms; last={row}")
        row = wait_for("race-game", lambda r: r["mode"] == 16, 45)
        stage("race-entry", row)
        race_time = time.monotonic()
        posted = round(time.monotonic() - start, 3)
        base.post_key(hwnd, "up", False)
        try:
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
