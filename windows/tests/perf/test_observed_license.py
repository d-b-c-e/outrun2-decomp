"""Offline identity/partial-row checks for the bounded frontend trace reader."""
import pathlib
import tempfile

from run_observed_license import FIELDS, RouteTrace, StageError


def row(seq, pid=123, run="fresh"):
    values = [seq, pid, run, 500 + seq, 32, 0, 2, 0, 1, 1, 0, 0, 11, "DDRR", 0, 0]
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
