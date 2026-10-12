"""Offline check of run.py's inject publish against the control worker's snapshot open (control_snapshot.hpp:
GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, no FILE_SHARE_WRITE). No game, lease or shim: a temp folder and a
reader thread that opens the file as the worker does, as fast as it can.

The first live R1 (r1-01, 2026-10-11 22:26) failed after batch 1 with "inject.txt unreadable: snapshot open refused
(Win32 32)" while run.py published with ReplaceFileW. Here ReplaceFileW is the mutation control: under the same reader it
must produce Win32 32 refusals, and run.py's rename publish must produce none.

    py -3 -B test_publish.py
"""
import ctypes, ctypes.wintypes as wt, pathlib, sys, tempfile, threading, time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import run

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.CreateFileW.restype = wt.HANDLE
k32.CreateFileW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, ctypes.c_void_p, wt.DWORD, wt.DWORD, wt.HANDLE]
GENERIC_READ, SHARE_READ, SHARE_DELETE, OPEN_EXISTING = 0x80000000, 1, 4, 3
INVALID = wt.HANDLE(-1).value
failures = []
def check(ok, what):
    if not ok: failures.append(what)


def worker_open(path):
    """The control worker's snapshot open, exactly; returns (handle or None, Win32 error)."""
    h = k32.CreateFileW(str(path), GENERIC_READ, SHARE_READ | SHARE_DELETE, None, OPEN_EXISTING, 0x00200000 | 0x08000000, None)
    return (None, ctypes.get_last_error()) if h == INVALID else (h, 0)


def read_all(h):
    buf = ctypes.create_string_buffer(4096); n = wt.DWORD()
    k32.ReadFile(h, buf, 4096, ctypes.byref(n), None); return buf.raw[:n.value]


def hot_reader(path, stop, errors):
    """Open, read, close in a loop, as the worker polls; count every refusal other than a missing file."""
    while not stop.is_set():
        h, e = worker_open(path)
        if h is None:
            if e not in (2, 3): errors[e] = errors.get(e, 0) + 1
        else: read_all(h); k32.CloseHandle(h)


def publish_under_reader(path, publish, n):
    stop, errors = threading.Event(), {}
    th = threading.Thread(target=hot_reader, args=(path, stop, errors)); th.start()
    failed = 0
    try:
        for k in range(n):
            try: publish(path, b"batch=%d\n" % k)
            except OSError: failed += 1
    finally:
        stop.set(); th.join()
    return failed, errors


def replace_file_w(path, data):   # the former publish (mutation control)
    tmp = path.with_name(path.name + ".rfw"); tmp.write_bytes(data)
    if not k32.ReplaceFileW(str(path), str(tmp), None, 0, None, None): raise OSError(ctypes.get_last_error())


with tempfile.TemporaryDirectory() as t:
    target = pathlib.Path(t) / "inject.txt"
    run.replace_file(target, b"batch=0\n")
    check(target.read_bytes() == b"batch=0\n", "a new file is published")
    # A worker handle held briefly (a snapshot read) is waited out by the retry, not broken.
    h, _ = worker_open(target)
    releaser = threading.Timer(0.15, lambda: k32.CloseHandle(h)); releaser.start()
    run.replace_file(target, b"batch=1\n"); releaser.join()
    check(target.read_bytes() == b"batch=1\n", "a publish waits out a briefly held worker handle")
    # The live pattern: the worker reading continuously while batches are published.
    failed, errors = publish_under_reader(target, run.replace_file, 300)
    check(failed == 0, "rename publishes failed under a reading worker: %d" % failed)
    check(not errors, "the worker's opens were refused during rename publishes: %r" % errors)
    check(target.read_bytes() == b"batch=299\n", "the last batch is in place")
    check(not [p for p in pathlib.Path(t).iterdir() if ".tmp-" in p.name], "no temp files are left")
    # Mutation control: the former ReplaceFileW publish makes the same reader fail with Win32 32 (r1-01's failure).
    _, rfw_errors = publish_under_reader(target, replace_file_w, 300)
    check(rfw_errors.get(32, 0) > 0, "mutation control: ReplaceFileW must make the worker's open fail with Win32 32 (%r)" % rfw_errors)

print("publish: %d failure(s)" % len(failures))
for f in failures: print("  " + f)
sys.exit(1 if failures else 0)
