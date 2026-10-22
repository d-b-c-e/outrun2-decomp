"""Synthetic private fixtures only: no game, build, device, owner config or install."""
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import zipfile
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("candidate_package", Path(__file__).with_name("package_candidate.py"))
pkg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pkg)


def fake_pe(machine=0x8664, flags=0x22):
    data = bytearray(512)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 64)
    data[64:68] = b"PE\0\0"
    struct.pack_into("<HH", data, 68, machine, 1)
    struct.pack_into("<HHH", data, 84, 240, flags, 0x20B)
    return bytes(data)


class PackageSafety(unittest.TestCase):
    def setUp(self):
        build = Path(__file__).parent / "build"
        build.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="outrun-alpha-package-", dir=build)
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base / "source"
        self.root.mkdir()
        pkg.git(self.root, "init", "-q")
        pkg.git(self.root, "config", "user.name", "Synthetic packaging fixture")
        pkg.git(self.root, "config", "user.email", "fixture@example.invalid")
        pkg.git(self.root, "config", "core.autocrlf", "false")
        pkg.git(self.root, "config", "commit.gpgsign", "false")
        for name in pkg.SOURCE_TEXT:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("Synthetic notice: " + name + "\n", encoding="utf-8")
        launcher = self.root / pkg.LAUNCHER_SOURCE
        launcher.parent.mkdir(parents=True, exist_ok=True)
        launcher.write_bytes(Path(__file__).with_name("Setup-Controls.ps1").read_bytes())
        (self.root / ".gitignore").write_text("private/\n", encoding="ascii")
        pkg.git(self.root, "add", ".")
        pkg.git(self.root, "-c", "core.hooksPath=", "commit", "--no-verify", "-qm", "Synthetic seed")
        self.commit = pkg.git(self.root, "rev-parse", "HEAD")
        self.build = self.base / "build"
        self.build.mkdir()
        self.exe = self.build / "OutRunDesktop.exe"
        self.exe.write_bytes(fake_pe())
        self.sha = pkg.digest(self.exe.read_bytes())
        self.licenses = {}
        for name in pkg.DEPENDENCIES:
            path = self.base / (name + "-license")
            path.write_text("Synthetic " + name + " license\n", encoding="ascii")
            self.licenses[name] = path
        self.output = self.base / "candidate.zip"

    def create(self, **kwargs):
        options = dict(root=self.root, exe=self.exe, expected_sha=self.sha,
                       commit=self.commit, licenses=self.licenses, output=self.output)
        options.update(kwargs)
        return pkg.package(**options)

    def helper(self, changes=None, manifest_changes=None):
        payload = {f"payload/{i}.dll": b"synthetic payload" for i in range(525)}
        payload.update({"Start-ModSetup.ps1": b"throw 'fixture must never launch'",
                        "DbceModSetup.exe": b"not executable synthetic payload"})
        manifest = dict(schema=1, kind="private-standalone-helper",
                        sourceCommit="6cb66313d9e0cd1e0cea41b49827feb992d73c2c",
                        rid="win-x64", selfContained=True,
                        files=[dict(path=n, bytes=len(b), sha256=pkg.digest(b)) for n, b in payload.items()])
        if manifest_changes:
            manifest_changes(manifest)
        manifest_bytes = json.dumps(manifest).encode()
        payload["bundle-manifest.json"] = manifest_bytes
        for name, data in (changes or {}).items():
            if data is None:
                del payload[name]
            else:
                payload[name] = data
        self.helper_zip = self.base / "helper.zip"
        with zipfile.ZipFile(self.helper_zip, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for name, data in payload.items():
                archive.writestr(name, data)
        for name, value in (("HELPER_SHA", pkg.digest(self.helper_zip.read_bytes())),
                            ("HELPER_MANIFEST_SHA", pkg.digest(manifest_bytes))):
            replacement = patch.object(pkg, name, value)
            replacement.start()
            self.addCleanup(replacement.stop)
        self.cap = self.base / "capabilities.json"
        self.cap.write_text(json.dumps(dict(schema="dbce.session-capabilities@1", gameBuildSha256=self.sha,
            settings=dict(contract="outrun.display-settings@1", file="OutRunDesktop.ini"),
            controls=dict(contract="ini-controls-1", adapter="outrun-decomp-ini-controls-1",
                          file="OutRunDesktop.ini", section="Controls"))), encoding="utf-8")
        return dict(helper_zip=self.helper_zip, setup_launcher=self.root / pkg.LAUNCHER_SOURCE,
                    capabilities=self.cap, capabilities_sha256=pkg.digest(self.cap.read_bytes()))

    def test_helper_exact_members_hashes_and_exclusion(self):
        options = self.helper()
        (self.base / "OutRunDesktop.install-owner.json").write_bytes(b"PRIVATE-SENTINEL")
        (self.base / "profile.json").write_bytes(b"PRIVATE-SENTINEL")
        self.create(**options)
        with zipfile.ZipFile(self.output) as archive:
            expected = {*pkg.SOURCE_TEXT, "OutRunDesktop.exe", "candidate-manifest.json",
                        "session-capabilities.json", "Setup-Controls.ps1",
                        *("licenses/" + n + ".txt" for n in pkg.DEPENDENCIES)}
            with zipfile.ZipFile(self.helper_zip) as helper:
                expected.update(pkg.HELPER_DIR + "/" + n for n in helper.namelist())
                for name in helper.namelist():
                    self.assertEqual(archive.read(pkg.HELPER_DIR + "/" + name), helper.read(name))
            self.assertEqual(set(archive.namelist()), expected)
            manifest = json.loads(archive.read("candidate-manifest.json"))
            self.assertIn("private-only", manifest["distribution"])
            for item in manifest["members"]:
                data = archive.read(item["path"])
                self.assertEqual((len(data), pkg.digest(data)), (item["bytes"], item["sha256"]))
                self.assertNotIn(b"PRIVATE-SENTINEL", data)
        self.create(output=self.base / "plain.zip")
        with zipfile.ZipFile(self.base / "plain.zip") as plain:
            self.assertFalse(any(n.startswith("Extras/") for n in plain.namelist()))
            self.assertNotIn("Setup-Controls.ps1", plain.namelist())
            self.assertNotIn("session-capabilities.json", plain.namelist())

    def test_helper_wrong_zip_and_manifest_hash(self):
        options = self.helper()
        with patch.object(pkg, "HELPER_SHA", "0" * 64):
            with self.assertRaisesRegex(ValueError, "Helper ZIP hash"):
                self.create(**options)
        with patch.object(pkg, "HELPER_MANIFEST_SHA", "0" * 64):
            with self.assertRaisesRegex(ValueError, "bundle-manifest hash"):
                self.create(**options)
        self.assertFalse(self.output.exists())

    def test_helper_wrong_missing_extra_payload(self):
        for changes in ({"payload/0.dll": b"changed"}, {"payload/0.dll": None},
                        {"owner.ini": b"PRIVATE-SENTINEL"},
                        {"payload/0.dll": None, "owner.ini": b"PRIVATE-SENTINEL"}):
            with self.subTest(changes=changes):
                options = self.helper(changes=changes)
                with self.assertRaises(ValueError):
                    self.create(**options)
                self.assertFalse(self.output.exists())

    def test_safe_helper_paths_and_size_bounds(self):
        for name in ("../secret", "/absolute", "C:/secret", "a\\secret", "file:stream",
                     "a//b", "a/./b", "CON.txt", "a/trailing.", "a/trailing "):
            with self.subTest(name=name):
                options = self.helper(changes={"payload/0.dll": None, name: b"unsafe"})
                with self.assertRaisesRegex(ValueError, "Unsafe"):
                    self.create(**options)
        options = self.helper()
        for limit in ("MAX_HELPER_FILE", "MAX_HELPER"):
            with patch.object(pkg, limit, 10):
                with self.assertRaises(ValueError):
                    self.create(**options)
        with patch.object(pkg, "MAX_HELPER", self.helper_zip.stat().st_size + 1):
            with self.assertRaisesRegex(ValueError, "oversized"):
                self.create(**options)
        self.assertFalse(self.output.exists())

    def test_helper_manifest_membership_and_aliases(self):
        options = self.helper(manifest_changes=lambda m: m["files"].__setitem__(0, m["files"][1]))
        with self.assertRaisesRegex(ValueError, "membership"):
            self.create(**options)
        options = self.helper(changes={"payload/0.dll": None, "PAYLOAD/1.DLL": b"alias"})
        with self.assertRaisesRegex(ValueError, "membership"):
            self.create(**options)

    def test_helper_archive_symlink_and_duplicate_entries_refused(self):
        options = self.helper()
        with zipfile.ZipFile(self.helper_zip) as archive:
            contents = {n: archive.read(n) for n in archive.namelist()}
        for symlink in (True, False):
            with zipfile.ZipFile(self.helper_zip, "w") as archive:
                for name, data in contents.items():
                    info = zipfile.ZipInfo(name)
                    if name == "payload/0.dll" and symlink:
                        info.create_system = 3
                        info.external_attr = 0o120777 << 16
                    archive.writestr(info, data)
                if not symlink:
                    archive.writestr("payload/0.dll", b"duplicate")
            with patch.object(pkg, "HELPER_SHA", pkg.digest(self.helper_zip.read_bytes())):
                with self.assertRaisesRegex(ValueError, "membership"):
                    self.create(**options)

    def test_helper_requires_explicit_matching_capability_and_launcher(self):
        options = self.helper()
        for key in options:
            with self.subTest(missing=key), self.assertRaisesRegex(ValueError, "requires"):
                self.create(**{k: v for k, v in options.items() if k != key})
        with self.assertRaisesRegex(ValueError, "Capability SHA"):
            self.create(**{**options, "capabilities_sha256": "0" * 64})
        cap = json.loads(self.cap.read_bytes())
        cap["gameBuildSha256"] = "0" * 64
        self.cap.write_text(json.dumps(cap), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "current matching"):
            self.create(**{**options, "capabilities_sha256": pkg.digest(self.cap.read_bytes())})
        with self.assertRaisesRegex(ValueError, "committed"):
            self.create(**{**options, "setup_launcher": self.cap})
        self.assertFalse(self.output.exists())

    def test_launcher_checks_synthetic_payload_without_executing_helper(self):
        pwsh = shutil.which("pwsh")
        if not pwsh:
            self.skipTest("PowerShell 7 unavailable")
        self.helper()
        folder = self.base / "extracted"
        with zipfile.ZipFile(self.helper_zip) as archive:
            archive.extractall(folder)
        launcher = self.base / "launcher.ps1"
        source = Path(__file__).with_name("Setup-Controls.ps1").read_text(encoding="utf-8")
        launcher.write_text(source.replace(
            "635a8703f86384caea394b91a2ae8b05e369fd10234546afd4d2d185996dba395",
            pkg.HELPER_MANIFEST_SHA), encoding="utf-8")
        # Extract only the production verifier AST; never evaluate the launch block.
        runner = self.base / "verify.ps1"
        runner.write_text('''param($Source, $Folder)
$ErrorActionPreference = 'Stop'
$ast = [Management.Automation.Language.Parser]::ParseFile($Source, [ref]$null, [ref]$null)
$fn = $ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Test-StandaloneHelper'}, $true)
. ([scriptblock]::Create($fn.Extent.Text))
Test-StandaloneHelper -Folder $Folder
''', encoding="utf-8")
        def verify():
            return subprocess.run([pwsh, "-NoProfile", "-File", str(runner), str(launcher), str(folder)],
                                  capture_output=True, text=True, timeout=30)
        result = verify()
        self.assertEqual(result.returncode, 0, result.stderr)
        payload = folder / "payload/0.dll"
        payload.write_bytes(b"changed")
        self.assertNotEqual(verify().returncode, 0)
        payload.unlink()
        self.assertNotEqual(verify().returncode, 0)
        payload.write_bytes(b"synthetic payload")
        (folder / "owner.ini").write_bytes(b"private")
        self.assertNotEqual(verify().returncode, 0)
        (folder / "owner.ini").unlink()
        (folder / "bundle-manifest.json").write_bytes(b"changed manifest")
        self.assertNotEqual(verify().returncode, 0)

    def test_exact_members_readback_and_reproducibility_exclude_private_neighbors(self):
        private = self.root / "private"
        private.mkdir()
        for folder in (private, self.build):
            for name in ("OR2006C2C.EXE", "OR2006C2C.cache", "OutRunDesktop.ini",
                         "options.ini", "WheelFfb.dll", "auth-token", "save.dat", "capture.json"):
                (folder / name).write_bytes(b"PRIVATE-SENTINEL")
        one = self.create()
        two = self.create(output=self.base / "another.zip")
        self.assertEqual(one["archiveSha256"], two["archiveSha256"])
        with zipfile.ZipFile(self.output) as archive:
            names = archive.namelist()
            expected = {*pkg.SOURCE_TEXT, "OutRunDesktop.exe", "candidate-manifest.json",
                        *("licenses/" + name + ".txt" for name in pkg.DEPENDENCIES)}
            self.assertEqual(set(names), expected)
            manifest = json.loads(archive.read("candidate-manifest.json"))
            self.assertEqual(manifest["sourceCommit"], self.commit)
            self.assertIn("pending", manifest["readiness"])
            for member in manifest["members"]:
                self.assertEqual(pkg.digest(archive.read(member["path"])), member["sha256"])
            for name in names:
                self.assertNotIn(b"PRIVATE-SENTINEL", archive.read(name))
                self.assertNotIn(str(self.base).encode(), archive.read(name))
            self.assertEqual(archive.read("OutRunDesktop.exe"), self.exe.read_bytes())

    def test_wrong_build_hash_refuses_without_output(self):
        with self.assertRaisesRegex(ValueError, "hash differs"):
            self.create(expected_sha="0" * 64)
        self.assertFalse(self.output.exists())

    def test_dirty_or_wrong_revision_refuses(self):
        with self.assertRaisesRegex(ValueError, "HEAD"):
            self.create(commit="0" * 40)
        (self.root / "THIRD_PARTY_NOTICES.md").write_text("edited", encoding="ascii")
        with self.assertRaisesRegex(ValueError, "dirty"):
            self.create()
        self.assertFalse(self.output.exists())

    def test_existing_archive_is_never_overwritten(self):
        self.output.write_bytes(b"existing owner artifact")
        with self.assertRaisesRegex(ValueError, "never replaced"):
            self.create()
        self.assertEqual(self.output.read_bytes(), b"existing owner artifact")

    def test_executable_renaming_does_not_admit_dll_x86_or_non_pe(self):
        for data in (fake_pe(flags=0x2022), fake_pe(machine=0x14C), b"not a PE"):
            self.exe.write_bytes(data)
            with self.assertRaises(ValueError):
                self.create(expected_sha=pkg.digest(data))
        self.assertFalse(self.output.exists())

    def test_retail_exe_name_is_not_admitted(self):
        retail = self.build / "OR2006C2C.EXE"
        retail.write_bytes(fake_pe())
        with self.assertRaisesRegex(ValueError, "Only the explicitly named"):
            self.create(exe=retail)

    def test_license_inputs_are_exact_bounded_text(self):
        with self.assertRaisesRegex(ValueError, "exactly"):
            self.create(licenses={**self.licenses, "extra": self.exe})
        self.licenses["ffmpeg"].write_bytes(fake_pe())
        with self.assertRaises((ValueError, UnicodeError)):
            self.create()
        self.assertFalse(self.output.exists())

    def test_symlink_executable_is_not_followed(self):
        target = self.build / "real.exe"
        self.exe.rename(target)
        try:
            self.exe.symlink_to(target)
        except OSError:
            self.skipTest("Creating symlinks unavailable to this Windows account")
        with self.assertRaisesRegex(ValueError, "Reparse/symlink"):
            self.create()
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
