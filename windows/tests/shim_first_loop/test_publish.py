"""Offline first-loop publisher integration. No game, device, lease or owner path.
Usage: py -3 -B test_publish.py <control_publish.exe> <fresh fixture directory>
"""
import contextlib, io, pathlib, subprocess, sys
from unittest import mock
import run

tool, out = (pathlib.Path(a).resolve() for a in sys.argv[1:3])
if out.exists(): raise SystemExit("Use a fresh fixture directory")
out.mkdir(parents=True)
target = out / "command-\u03bb.txt"
checks = 0

def check(condition, message):
    global checks
    checks += 1
    if not condition: raise AssertionError(message)

for data in (b"first\n", b"next\r\n\x00\xff", b"", b"x" * 4096):
    run.replace_file(target, data, tool)
    check(target.read_bytes() == data, "Python uses native tool without text/newline conversion")
try:
    run.replace_file(target, b"x" * 4097, tool)
    raise AssertionError("oversize CLI publication accepted")
except OSError as error:
    check("4096" in str(error), "oversize refusal reaches caller")
check(target.read_bytes() == b"x" * 4096, "oversize leaves original generation intact")
directory = out / "directory"; directory.mkdir()
try:
    run.replace_file(directory, b"no", tool)
    raise AssertionError("directory publication accepted")
except OSError:
    check(directory.is_dir(), "failed rename preserves directory")
check(not list(out.glob("*.publish-*.tmp")), "success and failure clean owned temp files")
explicit = out / "missing.exe"
check(run.publisher_tool(explicit, tool) == explicit, "explicit missing tool never falls back")
sibling = out / "control_publish.exe"; sibling.write_bytes(b"fixture-only-not-executed")
check(run.publisher_tool(candidate=out / "game.exe") == sibling, "deterministic candidate sibling default")
with mock.patch.object(sys, "argv", ["run.py", "--game", str(out / "absent-game"), "--candidate", str(out / "missing-game.exe"),
        "--result", str(out / "never-created"), "--publisher-tool", str(explicit), "--check-only"]), \
     mock.patch.object(run, "DBCE", out / "absent-control-root"), \
     mock.patch.object(run, "lease", side_effect=AssertionError("preflight took lease")), \
     mock.patch.object(run.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")), \
     contextlib.redirect_stdout(io.StringIO()) as text:
    try:
        run.main(); raise AssertionError("missing publisher passed preflight")
    except SystemExit as error:
        check(error.code == 1 and "no control publisher" in text.getvalue(), "missing helper refused before lease/process")
check(not (out / "never-created").exists(), "failed preflight creates no run state")
target.unlink(); sibling.unlink(); directory.rmdir(); out.rmdir()
check(not out.exists(), "all integration handles released for exact fixture cleanup")
print("PASS %d Python/native publisher integration checks" % checks)
