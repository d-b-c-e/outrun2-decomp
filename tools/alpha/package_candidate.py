"""Create a private Windows candidate ZIP from explicit inputs, never a folder sweep.

This binds operator-supplied build bytes to a clean source revision; it does not
prove that those bytes were built from that revision, or qualify distribution.
Only Python's standard library and read-only Git commands are used.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import stat
import struct
import subprocess
import zipfile


SOURCE_TEXT = (
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
HELPER_SHA = "dfa510ef8dbad597a710cf6dd7d8c3b85c4f18b15b6a51afe561e12f5fc3221e"
HELPER_MANIFEST_SHA = "635a8703f86384caea394b91a2ae8b05e369fd10234546afd4d2d185996dba395"
HELPER_MEMBERS = 528
MAX_HELPER_FILE = 32 * 1024 * 1024
MAX_HELPER = 256 * 1024 * 1024
HELPER_DIR = "Extras/StandaloneModSetup"
LAUNCHER_SOURCE = "tools/alpha/Setup-Controls.ps1"


def safe_member(name: str) -> bool:
    # Windows extraction must not introduce aliases, streams or traversal.
    return (isinstance(name, str) and len(name) <= 240 and
            all(re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_. -]*", part) and
                not part.endswith((".", " ")) and
                part.split(".")[0].upper() not in
                {"CON", "PRN", "AUX", "NUL", *(f"COM{i}" for i in range(1, 10)),
                 *(f"LPT{i}" for i in range(1, 10))}
                for part in name.split("/")))


def helper_payload(path: Path) -> dict[str, bytes]:
    data = regular_bytes(path.absolute(), MAX_HELPER)
    if digest(data) != HELPER_SHA:
        raise ValueError("Helper ZIP hash differs from the qualified private helper; supply the resolved 6cb66313d9e0 ZIP.")
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        entries = archive.infolist()
        names = [entry.filename for entry in entries]
        if (len(names) != HELPER_MEMBERS or len({n.casefold() for n in names}) != len(names) or
                any(not safe_member(n) for n in names) or
                any(e.file_size > MAX_HELPER_FILE or e.flag_bits & 1 or
                    stat.S_IFMT(e.external_attr >> 16) not in (0, stat.S_IFREG) for e in entries) or
                sum(e.file_size for e in entries) > MAX_HELPER):
            raise ValueError("Unsafe, oversized or incorrect helper membership.")
        manifest_bytes = archive.read("bundle-manifest.json")
        if digest(manifest_bytes) != HELPER_MANIFEST_SHA:
            raise ValueError("Helper bundle-manifest hash differs from the qualified helper.")
        manifest = json.loads(manifest_bytes)
        if (manifest.get("schema") != 1 or manifest.get("kind") != "private-standalone-helper" or
                manifest.get("sourceCommit") != "6cb66313d9e0cd1e0cea41b49827feb992d73c2c" or
                manifest.get("rid") != "win-x64" or manifest.get("selfContained") is not True):
            raise ValueError("Unsupported helper manifest identity.")
        files = manifest["files"]
        listed = [item["path"] for item in files]
        if (len(listed) != HELPER_MEMBERS - 1 or len(set(listed)) != len(listed) or
                set(names) != {*listed, "bundle-manifest.json"} or
                not {"Start-ModSetup.ps1", "DbceModSetup.exe"} <= set(listed)):
            raise ValueError("Helper payload membership differs from its manifest.")
        payload = {"bundle-manifest.json": manifest_bytes}
        for item in files:
            content = archive.read(item["path"])
            if len(content) != item["bytes"] or digest(content) != item["sha256"]:
                raise ValueError(f"Helper payload hash/size mismatch: {item['path']}")
            payload[item["path"]] = content
        return payload


def capability_bytes(path: Path, expected_sha: str, exe_sha: str) -> bytes:
    data = text_bytes(path.absolute())
    if not re.fullmatch(r"[0-9a-fA-F]{64}", expected_sha) or digest(data) != expected_sha.lower():
        raise ValueError("Capability SHA-256 differs from the supplied build capability receipt.")
    cap = json.loads(data)
    controls = cap.get("controls", {})
    if (cap.get("schema") != "dbce.session-capabilities@1" or
            cap.get("gameBuildSha256", "").lower() != exe_sha.lower() or
            cap.get("settings") != {"contract": "outrun.display-settings@1", "file": "OutRunDesktop.ini"} or
            controls.get("contract") != "ini-controls-1" or
            controls.get("adapter") != "outrun-decomp-ini-controls-1" or
            controls.get("file") != "OutRunDesktop.ini" or controls.get("section") != "Controls"):
        raise ValueError("Supply current matching OutRun settings/controls capability; historical owner receipts cannot admit a new build.")
    return data


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
    # Header admission only, not a loader, dependency scanner or authenticity check.
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


def package(root: Path, exe: Path, expected_sha: str, commit: str,
            licenses: dict[str, Path], output: Path, *, helper_zip: Path | None = None,
            setup_launcher: Path | None = None, capabilities: Path | None = None,
            capabilities_sha256: str | None = None) -> dict:
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
    pe_identity(binary)
    members = {"OutRunDesktop.exe": binary}
    helper_inputs = (helper_zip, setup_launcher, capabilities, capabilities_sha256)
    if any(value is not None for value in helper_inputs):
        if any(value is None for value in helper_inputs):
            raise ValueError("Helper route requires --helper-zip, --setup-launcher, --capabilities and --capabilities-sha256 together.")
        if setup_launcher.absolute() != root / LAUNCHER_SOURCE:
            raise ValueError("Setup launcher must be the committed tools/alpha/Setup-Controls.ps1 source.")
        launcher = text_bytes(setup_launcher)
        if git(root, "rev-parse", f"{commit}:{LAUNCHER_SOURCE}") != git(
                root, "hash-object", "--path", LAUNCHER_SOURCE, str(setup_launcher)):
            raise ValueError("Setup launcher differs from the recorded revision.")
        members["session-capabilities.json"] = capability_bytes(capabilities, capabilities_sha256, expected_sha)
        members["Setup-Controls.ps1"] = launcher
        members.update({f"{HELPER_DIR}/{name}": data for name, data in helper_payload(helper_zip).items()})
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
        "runtimeDependencies": "Inspect actual PE imports; system/VC runtime prerequisites are not bundled.",
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
    parser.add_argument("--helper-zip", type=Path)
    parser.add_argument("--setup-launcher", type=Path)
    parser.add_argument("--capabilities", type=Path)
    parser.add_argument("--capabilities-sha256")
    for name in DEPENDENCIES:
        parser.add_argument(f"--{name}-license", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = package(args.source_root, args.exe, args.exe_sha256, args.source_commit,
                         {name: getattr(args, name + "_license") for name in DEPENDENCIES}, args.output,
                         helper_zip=args.helper_zip, setup_launcher=args.setup_launcher,
                         capabilities=args.capabilities, capabilities_sha256=args.capabilities_sha256)
    except (ValueError, OSError, subprocess.SubprocessError, UnicodeError, zipfile.BadZipFile,
            KeyError, TypeError, AttributeError) as error:
        parser.exit(1, f"Package refused: {error}\n")
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
