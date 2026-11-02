"""Synthetic private fixtures only: no game, build, device, owner config or install."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

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


def importing_pe():
    data = bytearray(fake_pe()) + bytearray(512)
    struct.pack_into("<I", data, 88 + 60, 512)  # SizeOfHeaders
    struct.pack_into("<I", data, 88 + 108, 16)  # NumberOfRvaAndSizes
    struct.pack_into("<II", data, 88 + 120, 0x1000, 60)
    struct.pack_into("<IIII", data, 328 + 8, 512, 0x1000, 512, 512)
    struct.pack_into("<IIIII", data, 512, 0, 0, 0, 0x1050, 1)
    struct.pack_into("<IIIII", data, 532, 0, 0, 0, 0x1064, 1)
    data[592:605] = b"KERNEL32.dll\0"
    data[612:622] = b"d3d11.dll\0"
    return data


class PeImports(unittest.TestCase):
    def test_empty_and_section_imports(self):
        self.assertEqual(pkg.pe_static_imports(fake_pe()), [])
        self.assertEqual(pkg.pe_static_imports(importing_pe()), ["d3d11.dll", "kernel32.dll"])

    def test_header_rvas_and_case_deduplication(self):
        data = importing_pe()
        struct.pack_into("<I", data, 532 + 12, 400)
        data[400:413] = b"kernel32.DLL\0"
        self.assertEqual(pkg.pe_static_imports(data), ["kernel32.dll"])

    def test_truncated_or_unterminated_directory(self):
        for position, value in ((84, 112), (88 + 124, 40), (88 + 124, 4096 * 20 + 1)):
            with self.subTest(position=position, value=value):
                data = importing_pe()
                struct.pack_into("<H" if position == 84 else "<I", data, position, value)
                with self.assertRaises(ValueError):
                    pkg.pe_static_imports(data)

    def test_unmapped_and_virtual_only_rvas(self):
        for rva in (0x3000, 0x1200):
            data = importing_pe()
            struct.pack_into("<I", data, 328 + 8, 1024)  # virtual tail has no raw bytes
            struct.pack_into("<I", data, 512 + 12, rva)
            with self.assertRaisesRegex(ValueError, "file-backed"):
                pkg.pe_static_imports(data)

    def test_names_are_bounded_and_text(self):
        for payload in (b"\0", b"\xff\0", b"\n\0", b"x" * (1024 - 592)):
            data = importing_pe()
            data[592:592 + len(payload)] = payload
            with self.assertRaises(ValueError):
                pkg.pe_static_imports(data)

    def test_directory_cannot_cross_raw_region(self):
        data = importing_pe()
        struct.pack_into("<I", data, 88 + 124, 520)
        with self.assertRaisesRegex(ValueError, "file-backed region"):
            pkg.pe_static_imports(data)

    def test_nonterminal_descriptor_needs_a_name(self):
        data = importing_pe()
        struct.pack_into("<I", data, 512 + 12, 0)
        with self.assertRaisesRegex(ValueError, "no DLL name"):
            pkg.pe_static_imports(data)


class PackageSafety(unittest.TestCase):
    def setUp(self):
        fixture_parent = Path(__file__).resolve().parents[2] / "work/validation-20261102/package-fixtures"
        fixture_parent.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="outrun-alpha-package-", dir=fixture_parent)
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
            self.assertEqual(manifest["staticImports"], [])
            self.assertIn("pending", manifest["readiness"])
            for member in manifest["members"]:
                self.assertEqual(pkg.digest(archive.read(member["path"])), member["sha256"])
            for name in names:
                self.assertNotIn(b"PRIVATE-SENTINEL", archive.read(name))
                self.assertNotIn(str(self.base).encode(), archive.read(name))
            self.assertEqual(archive.read("OutRunDesktop.exe"), self.exe.read_bytes())

    def test_manifest_inventories_only_static_names(self):
        self.exe.write_bytes(importing_pe())
        self.sha = pkg.digest(self.exe.read_bytes())
        self.create()
        with zipfile.ZipFile(self.output) as archive:
            manifest = json.loads(archive.read("candidate-manifest.json"))
            self.assertEqual(manifest["staticImports"], ["d3d11.dll", "kernel32.dll"])
            self.assertIn("do not prove", manifest["runtimeDependencies"])

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
