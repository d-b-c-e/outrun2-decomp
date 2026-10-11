"""Reviewed writer content pin; checkout line endings may differ, source may not."""
import hashlib

REVIEWED_LF_SHA256 = "a9de5d49c55b9b4be464fb173178d20a97972eacff500aee0267e30fb44d19db"


def verify_writer(content):
    normalized = content.replace(b"\r\n", b"\n")
    if b"\r" in normalized or hashlib.sha256(normalized).hexdigest() != REVIEWED_LF_SHA256:
        raise RuntimeError("Unreviewed native writer")
    return {"rawSha256": hashlib.sha256(content).hexdigest(), "lfSha256": REVIEWED_LF_SHA256}
