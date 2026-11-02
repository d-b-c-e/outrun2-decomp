"""Create a private Windows candidate ZIP from explicit inputs, never a folder sweep.

This binds operator-supplied build bytes to a clean source revision; it does not
prove that those bytes were built from that revision, or qualify distribution.
Only Python's standard library and read-only Git commands are used.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import subprocess
import zipfile


SOURCE_TEXT = (
    "windows/Install-Candidate.ps1",
    "docs/PRIVATE-CANDIDATE-INSTALL.md",
    "THIRD_PARTY_NOTICES.md",
    "docs/ALPHA-READINESS.md",
    "windows/vendor/controls/LICENSE",
    "windows/vendor/native/LICENSE",
    "windows/vendor/native/third_party/imgui/LICENSE.txt",
    "windows/vendor/session/LICENSE",
    "windows/vendor/telemetry/LICENSE",
    "windows/vendor/wheelffb/LICENSE",
)
DEPENDENCIES = ("zlib", "ogg", "vorbis", "ffmpeg")
MAX_EXE = 256 * 1024 * 1024
MAX_TEXT = 1024 * 1024
STAMP = (1980, 1, 1, 0, 0, 0)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def regular_bytes(path: Path, limit: int) -> bytes:
    # Reject symlinks/junctions at every component, including Windows reparse points.
    for part in (path, *path.parents):
        info = part.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError(f"Reparse/symlink input refused: {path.name}")
    if not path.is_file() or path.stat().st_size > limit:
        raise ValueError(f"Missing, non-file or oversized input: {path.name}")
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f"Oversized input: {path.name}")
    return data


def text_bytes(path: Path) -> bytes:
    data = regular_bytes(path, MAX_TEXT)
    text = data.decode("utf-8-sig")
    if not text.strip() or "\0" in text or data[:2] == b"MZ":
        raise ValueError(f"Non-text/empty notice refused: {path.name}")
    return data


def git(root: Path, *args: str) -> str:
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith("GIT_")}
    result = subprocess.run(
        ["git", "--no-optional-locks", "-C", str(root), *args],
        env=env, check=True, capture_output=True, timeout=15,
    )
    return result.stdout.decode("utf-8").strip()


def source_revision(root: Path, commit: str) -> None:
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Source commit must be a full lowercase SHA-1.")
    if Path(git(root, "rev-parse", "--show-toplevel")).resolve() != root.resolve():
        raise ValueError("Source root must be the exact repository root.")
    if git(root, "rev-parse", "HEAD") != commit:
        raise ValueError("Source HEAD does not match the requested revision.")
    if git(root, "status", "--porcelain=v1", "--untracked-files=normal"):
        raise ValueError("Commit source changes before packaging; source tree is dirty.")


def pe_identity(data: bytes) -> None:
    # Header admission only, not a loader or authenticity check.
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError("Candidate is not a PE executable.")
    offset = struct.unpack_from("<I", data, 0x3C)[0]
    if offset < 64 or offset + 26 > len(data) or data[offset:offset + 4] != b"PE\0\0":
        raise ValueError("Invalid PE header.")
    machine, sections = struct.unpack_from("<HH", data, offset + 4)
    optional_size, flags, magic = struct.unpack_from("<HHH", data, offset + 20)
    if (machine != 0x8664 or not sections or optional_size < 112 or
            offset + 24 + optional_size + sections * 40 > len(data) or
            magic != 0x20B or not flags & 2 or flags & 0x2000):
        raise ValueError("Expected an AMD64 PE32+ executable, not a DLL or x86 image.")


def pe_static_imports(data: bytes) -> list[str]:
    """DLL names from the ordinary import directory, never load/resolve anything.

    Microsoft PE format: https://learn.microsoft.com/windows/win32/debug/pe-format
    Delay, transitive and dynamic imports are deliberately outside this inventory.
    """
    pe_identity(data)
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    optional, sections = pe + 24, struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    directories = struct.unpack_from("<I", data, optional + 108)[0]
    if directories < 2:
        return []
    if optional_size < 128:
        raise ValueError("Truncated PE import data directory.")
    directory, size = struct.unpack_from("<II", data, optional + 120)
    if not directory and not size:
        return []
    if not directory or size < 20 or size > 4096 * 20:
        raise ValueError("Invalid or oversized PE import directory.")
    headers = struct.unpack_from("<I", data, optional + 60)[0]
    table = optional + optional_size

    def locate(rva: int) -> tuple[int, int]:
        candidates = []
        if rva < min(headers, len(data)):
            candidates.append((rva, min(headers, len(data)) - rva))
        for i in range(sections):
            virtual_size, address, raw_size, raw = struct.unpack_from("<IIII", data, table + i * 40 + 8)
            if address <= rva < address + max(virtual_size, raw_size):
                delta = rva - address
                if delta < raw_size and raw + raw_size <= len(data):
                    candidates.append((raw + delta, raw_size - delta))
        if len(candidates) != 1:
            raise ValueError("PE import RVA is unmapped, ambiguous or outside file-backed data.")
        return candidates[0]

    start, available = locate(directory)
    if size > available:
        raise ValueError("PE import directory exceeds its file-backed region.")
    names = set()
    for descriptor in range(size // 20):
        words = struct.unpack_from("<IIIII", data, start + descriptor * 20)
        if not any(words):
            return sorted(names)
        if not words[3]:
            raise ValueError("PE import descriptor has no DLL name.")
        at, available = locate(words[3])
        end = data.find(b"\0", at, at + min(available, 4096))
        if end <= at:
            raise ValueError("Empty or unterminated PE import DLL name.")
        raw_name = data[at:end]
        if any(b < 32 or b > 126 for b in raw_name):
            raise ValueError("Non-ASCII/control byte in PE import DLL name.")
        names.add(raw_name.decode("ascii").lower())
    raise ValueError("PE import directory lacks a terminating descriptor.")


def package(root: Path, exe: Path, expected_sha: str, commit: str,
            licenses: dict[str, Path], output: Path) -> dict:
    root, exe, output = root.absolute(), exe.absolute(), output.absolute()
    if not re.fullmatch(r"[0-9a-fA-F]{64}", expected_sha):
        raise ValueError("Expected EXE SHA-256 is required.")
    if exe.name.casefold() != "outrundesktop.exe":
        raise ValueError("Only the explicitly named OutRunDesktop.exe is admitted.")
    if set(licenses) != set(DEPENDENCIES):
        raise ValueError("Supply exactly zlib, ogg, vorbis and ffmpeg license texts.")
    if output.suffix.lower() != ".zip" or output.exists():
        raise ValueError("Output must be a new ZIP; existing output is never replaced.")
    source_revision(root, commit)
    binary = regular_bytes(exe, MAX_EXE)
    if digest(binary) != expected_sha.lower():
        raise ValueError("Candidate EXE hash differs from the qualified build receipt.")
    imports = pe_static_imports(binary)
    members = {"OutRunDesktop.exe": binary}
    for name in SOURCE_TEXT:
        # Bind every supplied source text to the recorded Git blob, even when ignored.
        data = text_bytes(root / name)
        blob = git(root, "rev-parse", f"{commit}:{name}")
        # Apply Git's normal checkout filters (notably Windows CRLF) for this
        # comparison; the manifest separately hashes the exact packaged bytes.
        actual = git(root, "hash-object", "--path", name, str(root / name))
        if blob != actual:
            raise ValueError(f"Source text differs from recorded revision: {name}")
        members[name] = data
    for name in DEPENDENCIES:
        members[f"licenses/{name}.txt"] = text_bytes(licenses[name].absolute())
    manifest = {
        "schemaVersion": 1, "kind": "outrun-windows-private-alpha-candidate",
        "sourceCommit": commit, "platform": "windows-x64",
        "buildBinding": "Operator-supplied EXE hash; verify the separate build receipt.",
        "readiness": "pending-candidate-regression-and-runtime-review",
        "distribution": "private-only; public release requires separate review",
        "staticImports": imports,
        "runtimeDependencies": "Ordinary PE imports only; delay, transitive and dynamic loads are not inventoried. "
                               "Names do not prove dependency availability; system/VC runtimes are not bundled.",
        "members": [{"path": name, "bytes": len(data), "sha256": digest(data)}
                    for name, data in sorted(members.items())],
    }
    members["candidate-manifest.json"] = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    # Stored entries avoid zlib-version-dependent output. Fixed names/order/metadata
    # make identical inputs byte-reproducible, with no local paths or timestamps.
    with output.open("xb") as stream:
        with zipfile.ZipFile(stream, "w", compression=zipfile.ZIP_STORED) as archive:
            for name, data in sorted(members.items()):
                info = zipfile.ZipInfo(name, STAMP)
                info.create_system = 3
                info.external_attr = 0o100644 << 16
                archive.writestr(info, data)
    # Read every archived byte back. An error leaves this new archive for diagnosis;
    # this tool neither deletes nor overwrites any pre-existing path.
    with zipfile.ZipFile(output) as archive:
        if archive.namelist() != sorted(members):
            raise ValueError("Archive member list mismatch.")
        for name, data in members.items():
            if archive.read(name) != data:
                raise ValueError(f"Archive readback mismatch: {name}")
    return {"sourceCommit": commit, "archiveSha256": digest(output.read_bytes()),
            "exeSha256": expected_sha.lower(), "members": len(members),
            "readiness": manifest["readiness"]}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--exe-sha256", required=True)
    parser.add_argument("--output", type=Path, required=True)
    for name in DEPENDENCIES:
        parser.add_argument(f"--{name}-license", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = package(args.source_root, args.exe, args.exe_sha256, args.source_commit,
                         {name: getattr(args, name + "_license") for name in DEPENDENCIES}, args.output)
    except (ValueError, OSError, subprocess.SubprocessError, UnicodeError) as error:
        parser.exit(1, f"Package refused: {error}\n")
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
