#!/usr/bin/env python3
"""Check that mj follows rebuilt executables and preserves unrelated commands."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("command_link", ROOT / "tools/link_macos_command.py")
linker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(linker)


class CommandLinkTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name).resolve()
        self.package = self.root / "package"
        self.binary = self.package / "bin/joe"
        self.binary.parent.mkdir(parents=True)
        (self.package / "etc/joe").mkdir(parents=True)
        (self.package / "etc/joe/joerc").write_text("test config")
        (self.package / "share/joe/colors").mkdir(parents=True)
        self.command = self.root / "commands/mj"
        self.write_binary("first")

    def tearDown(self):
        self.temporary.cleanup()

    def write_binary(self, value):
        candidate = self.binary.with_suffix(".new")
        candidate.write_text("#!/bin/sh\nprintf '%s\\n' " + value + "\n")
        candidate.chmod(0o755)
        candidate.replace(self.binary)
        (self.package / "build-info.json").write_text(json.dumps({
            "binary_sha256": hashlib.sha256(self.binary.read_bytes()).hexdigest()}))

    def test_link_automatically_runs_new_binary_after_atomic_rebuild(self):
        linker.ensure_link(self.package, self.command)
        self.assertEqual(subprocess.check_output([self.command]), b"first\n")
        inode = self.command.lstat().st_ino
        self.write_binary("second")
        linker.ensure_link(self.package, self.command)
        self.assertEqual(self.command.lstat().st_ino, inode)
        self.assertEqual(subprocess.check_output([self.command]), b"second\n")

    def test_already_correct_link_needs_no_directory_write(self):
        linker.ensure_link(self.package, self.command)
        self.command.parent.chmod(0o555)
        try:
            self.assertEqual(linker.ensure_link(self.package, self.command), str(self.binary))
        finally:
            self.command.parent.chmod(0o755)

    def test_old_managed_launcher_becomes_link(self):
        self.command.parent.mkdir()
        self.command.write_text("#!/bin/sh\n" + linker.MARKER + "exit 0\n")
        linker.ensure_link(self.package, self.command)
        self.assertTrue(self.command.is_symlink())
        self.assertEqual(self.command.resolve(), self.binary)

    def test_unrelated_command_and_link_are_preserved(self):
        self.command.parent.mkdir()
        self.command.write_text("unrelated")
        with self.assertRaises(ValueError):
            linker.ensure_link(self.package, self.command)
        self.assertEqual(self.command.read_text(), "unrelated")
        self.command.unlink()
        self.command.symlink_to(self.root / "unrelated")
        with self.assertRaises(ValueError):
            linker.ensure_link(self.package, self.command)
        self.assertEqual(self.command.readlink(), self.root / "unrelated")

    def test_invalid_build_cannot_replace_existing_launcher(self):
        self.command.parent.mkdir()
        content = "#!/bin/sh\n" + linker.MARKER
        self.command.write_text(content)
        self.binary.write_text("incomplete build")
        with self.assertRaises(ValueError):
            linker.ensure_link(self.package, self.command)
        self.assertEqual(self.command.read_text(), content)


if __name__ == "__main__":
    unittest.main(verbosity=2)
