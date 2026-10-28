#!/usr/bin/env python3
"""Run the shared real-PTY suite and macOS package relocation checks."""
import importlib.util
from pathlib import Path
import re
import shutil
import sys
import unittest

if len(sys.argv) < 2:
    raise SystemExit("usage: macos_package_test.py /path/to/MiJoe-macOS-arm64")
PACKAGE = Path(sys.argv.pop(1)).resolve()
CLI = PACKAGE / "bin/mijoe"
arguments = sys.argv[:]
sys.argv = [sys.argv[0], str(CLI)]
spec = importlib.util.spec_from_file_location("mijoe_pty", Path(__file__).with_name("vim_pty_test.py"))
pty_tests = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pty_tests)
sys.argv = arguments


class MacPackageTests(pty_tests.VimTests):
    def relocated(self):
        destination = self.directory / "可移动 Mac 测试's folder" / "MiJoe"
        shutil.copytree(PACKAGE, destination)
        config = destination / "etc/joe/joerc"
        original = config.read_bytes()
        changed, count = re.subn(rb"^-lmsg .*", b"-lmsg MACOS_RESOURCE_PROBE %n", original, flags=re.MULTILINE)
        self.assertEqual(count, 1)
        config.write_bytes(changed)
        return destination

    def probe_editor(self, executable, **options):
        original = pty_tests.BINARY
        try:
            pty_tests.BINARY = str(executable)
            return self.editor(b"native macOS\n", **options)
        finally:
            pty_tests.BINARY = original

    def test_macos_relocated_config_in_unicode_space_path(self):
        relocated = self.relocated()
        editor = self.probe_editor(relocated / "bin/mijoe")
        editor.until(b"MACOS_RESOURCE_PROBE")
        editor.send(b"A!\x1b")
        self.assertEqual(editor.finish(), b"native macOS!\n")

    def test_macos_real_binary_works_through_symlink(self):
        relocated = self.relocated()
        alias = self.directory / "joe-alias"
        alias.symlink_to(relocated / "bin/joe")
        editor = self.probe_editor(alias)
        editor.until(b"MACOS_RESOURCE_PROBE")
        self.assertEqual(editor.finish(), b"native macOS\n")

    def test_macos_relocated_color_resources(self):
        relocated = self.relocated()
        colors = relocated / "share/joe/colors"
        scheme = (colors / "default.jcf").read_text().replace(".colors 16", ".colors 256")
        scheme += "\n-text 196/17\n"
        (colors / "macos-relocation-probe.jcf").write_text(scheme)
        editor = self.probe_editor(relocated / "bin/mijoe", options=("-colors", "macos-relocation-probe", "-assume_256color"))
        editor.until(b"38;5;196")
        self.assertEqual(editor.finish(), b"native macOS\n")


if __name__ == "__main__":
    print(f"macOS package: {PACKAGE}", flush=True)
    print(f"PTY transcripts: {pty_tests.ARTIFACTS}", flush=True)
    unittest.main(verbosity=2)
