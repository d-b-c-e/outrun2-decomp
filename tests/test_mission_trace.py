"""Actual inert native-wrapper child only. No game, renderer, device or owner writes."""
import os
from pathlib import Path
import subprocess
import sys

exe = Path(sys.argv[1]).resolve()
for value in (None, "0", "true", "1"):
    env = dict(os.environ)
    env.pop("OR2_MISSION_TRACE", None)
    if value is not None:
        env["OR2_MISSION_TRACE"] = value
    result = subprocess.run([str(exe), "--trace-cap"], env=env, capture_output=True, text=True, timeout=20)
    if result.returncode == 77:
        print("SKIP: native mission wrapper requires OR2_EXE")
        sys.exit(77)
    assert result.returncode == 0, result.stderr[-4000:]
    lines = [line for line in result.stderr.splitlines() if line.startswith("mission-trace ")]
    if value != "1":
        assert not lines, (value, lines[:2])
        continue
    rows = [line for line in lines if line.startswith("mission-trace tick=")]
    assert len(rows) == 600 and len(lines) == 602
    assert lines[0] == "mission-trace begin limit=600 types=2,6"
    assert lines[-1] == "mission-trace end limit=600"
    assert rows[0].startswith("mission-trace tick=1 ") and rows[-1].startswith("mission-trace tick=600 ")
    assert "type=6" in rows[0] and "place=1:1 score=3:2" in rows[0]
    assert any("end=0:1" in row for row in rows)
    assert any("type=2" in row and "score=0:10" in row and "score=100:110" in row for row in rows)
    assert any("pause=1" in row and "score=30:30" in row for row in rows)
    assert any("r7{" in row and "truncated=1" in row for row in rows)
    assert not any("r8{" in row for row in rows)
    assert all("missing=0:0 ok=1" in row and len(row) < 3000 for row in rows)
    assert "638 checks" in result.stdout
print("mission trace: actual wrapper off/strict-opt-in, before/after fields, pause, end and 600-call cap PASS")
