"""Offline identity/partial-row checks for the bounded frontend trace reader."""
import pathlib
import tempfile
import contextlib
import io
import json
import types
from unittest import mock

import run_observed_license as runner
from run_observed_license import FIELDS, RouteTrace, StageError


def row(seq, pid=123, run="fresh"):
    values = [seq, pid, run, 500 + seq, 32, 0, 2, 0, 1, 1, 0, 0, 11, "DDRR", 0, 0, 2, 1, 0]
    return "\t".join(map(str, values)) + "\n"


with tempfile.TemporaryDirectory() as folder:
    file = pathlib.Path(folder) / "state.tsv"
    reader = RouteTrace(file, 123, "fresh")
    assert reader.poll() is None
    file.write_text("\t".join(FIELDS) + "\n" + row(1)[:-2])
    assert reader.poll() is None  # never consume a partially written transition
    with file.open("a") as stream: stream.write(row(1)[-2:])
    first = reader.poll()
    assert first["seq"] == 1 and first["ready"] == 1 and first["doneMoves"] == "DDRR"
    with file.open("a") as stream: stream.write(row(2))
    assert reader.poll()["seq"] == 2
    with file.open("a") as stream: stream.write(row(4))
    try: reader.poll()
    except StageError as error: assert "sequence" in str(error)
    else: raise AssertionError("skipped sequence accepted")

with tempfile.TemporaryDirectory() as folder:
    file = pathlib.Path(folder) / "state.tsv"
    file.write_text("\t".join(FIELDS) + "\n" + row(1, run="stale"))
    try: RouteTrace(file, 123, "fresh").poll()
    except StageError as error: assert "identity" in str(error)
    else: raise AssertionError("stale run accepted")

print("observed license trace: partial rows and stale identity/sequence refused")


class Clock:
    def __init__(self): self.now = 0.0
    def monotonic(self): return self.now
    def sleep(self, seconds): self.now += seconds


def existing_route(scenario):
    """Run the real main/waits/cleanup against a passive simulated frontend.

    No process, window messages or lease are created. Save files and receipts
    are real private files, so the production final hash guard is exercised.
    """
    with tempfile.TemporaryDirectory() as folder:
        root = pathlib.Path(folder)
        game, out = root / "game", root / "result"
        save = game / "SaveGame"
        save.mkdir(parents=True)
        for name in ("OutRunDesktop.exe", "OR2006C2C.exe", "runtime.log", "OutRunXbox.log"):
            (game / name).write_bytes(b"offline fixture")
        original = {"License1.dat": b"existing license\x00\xff", "common.dat": b"existing common\x00\xfe"}
        for name, data in original.items(): (save / name).write_bytes(data)
        clock = Clock()
        proc = types.SimpleNamespace(pid=123, returncode=None)
        proc.poll = lambda: proc.returncode
        proc.wait = lambda timeout: proc.returncode
        state = dict(seq=1, mode=32, childState=0, loaded=0, menuIndex=0)
        messages = []

        class Frontend:
            def __init__(self, *args): self.last = None
            def poll(self):
                # Reaching a race is downstream evidence only; the fixture's
                # regression is that title input must precede profile loading.
                self.last = dict(state)
                return self.last

        def post_key(hwnd, name, up):
            messages.append((clock.now, name, up, state["childState"], state["loaded"]))
            if not up: return
            if name == "enter":
                child = state["childState"]
                if child == 0:
                    if scenario == "stalled-title": return
                    if scenario == "chooser":
                        state["childState"] = 21
                    else:
                        state.update(childState=1, loaded=1)
                else:
                    state["childState"] = {1: 2, 2: 36, 36: 10, 10: 16, 16: 4}.get(child, child)
                    if child == 4: state["mode"] = 16
            elif name == "right" and state["childState"] == 2:
                state["menuIndex"] = 1

        def close(hwnd, message, wparam, lparam):
            if scenario == "changed-save": (save / "License1.dat").write_bytes(b"unexpected change")
            proc.returncode = 0

        args = ["run_observed_license.py", "--game", str(game), "--result", str(out),
                "--lease-owner", "fixture", "--run-id", "existing", "--seconds", "15"]
        with contextlib.ExitStack() as stack:
            patches = (
                mock.patch.object(runner.sys, "argv", args),
                mock.patch.dict(runner.os.environ, {"DBCE_FFB_MUTE": "1"}),
                mock.patch.object(runner.time, "monotonic", clock.monotonic),
                mock.patch.object(runner.time, "sleep", clock.sleep),
                mock.patch.object(runner, "RouteTrace", Frontend),
                mock.patch.object(runner.subprocess, "run", return_value=types.SimpleNamespace(stdout="")),
                mock.patch.object(runner.subprocess, "Popen", return_value=proc),
                mock.patch.object(runner.base, "REFUSED_PARTS", ()),
                mock.patch.object(runner.base, "INJECT_ON", root / "absent-latch"),
                mock.patch.object(runner.base, "lease_problem", return_value=None),
                mock.patch.object(runner.base, "windows_of", return_value=[456]),
                mock.patch.object(runner.base, "post_key", post_key),
                mock.patch.object(runner.base, "parse_profile", return_value={"fixture": True}),
                mock.patch.object(runner.base, "user32", types.SimpleNamespace(PostMessageW=close)),
                contextlib.redirect_stdout(io.StringIO()),
            )
            for patch in patches: stack.enter_context(patch)
            result = runner.main()
        receipt = json.loads((out / "route.json").read_text())
        after = {p.name: p.read_bytes() for p in save.iterdir()}
        if scenario != "changed-save": assert after == original, "existing save bytes changed"
        assert receipt["exitCode"] == 0 and not receipt["forced"]
        assert receipt["freshLicense"] is False
        return result, receipt, messages


result, receipt, messages = existing_route("normal")
assert result == 0, receipt["errors"]
stages = {entry["stage"]: entry["t"] for entry in receipt["stages"]}
title = messages[0]
assert title[1:] == ("enter", False, 0, 0), messages
assert stages["existing-title-ready"] <= title[0] < stages["existing-title-advanced"]
assert title[0] < stages["existing-license-loaded"] <= stages["first-choice"]
# The next Enter belongs to the observed first menu, not the old timed prefix.
assert [m for m in messages if m[1] == "enter" and not m[2]][1][3:] == (1, 1)
assert receipt["saveWitness"] and not receipt["errors"]

result, receipt, messages = existing_route("stalled-title")
assert result == 1 and "existing-title-advanced: timeout" in receipt["errors"][0]
assert len(messages) == 2, messages  # one press/release, no blind retry/menu action
assert not any(s["stage"] == "existing-license-loaded" for s in receipt["stages"])

result, receipt, messages = existing_route("chooser")
assert result == 1 and "existing-license-chooser:" in receipt["errors"][0]
assert len(messages) == 2, messages  # unavailable chooser cursor must not be guessed

result, receipt, messages = existing_route("changed-save")
assert result == 1 and "existing license changed: License1.dat" in receipt["errors"]
print("observed existing license: title-before-load, stalled title, unobservable chooser, save guard passed")
