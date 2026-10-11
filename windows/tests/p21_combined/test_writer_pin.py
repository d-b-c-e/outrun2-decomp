"""Run against the reviewed toolkit writer; never edits that input."""
import pathlib
import sys
from writer_pin import verify_writer

original = pathlib.Path(sys.argv[1]).read_bytes()
lf = original.replace(b"\r\n", b"\n")
crlf = lf.replace(b"\n", b"\r\n")
a, b = verify_writer(lf), verify_writer(crlf)
assert a["lfSha256"] == b["lfSha256"]
assert a["rawSha256"] != b["rawSha256"]
for content in (lf + b" ", lf.replace(b"\n", b"\r", 1), b"\xef\xbb\xbf" + lf, lf[:-1], lf.replace(b"return", b"RETURN", 1)):
    try:
        verify_writer(content)
    except RuntimeError:
        continue
    raise AssertionError("Changed writer accepted")
print("PASS: LF/CRLF accepted with distinct raw pins; five changed-content cases refused")
