#!/usr/bin/env python3
"""Exercise in-editor help, native JOE shortcuts, and theme menus over a PTY."""
import fcntl
import importlib.util
import os
from pathlib import Path
import signal
import struct
import sys
import termios
import unittest

if len(sys.argv) < 2:
    raise SystemExit("usage: vim_ui_test.py /path/to/joe")
BINARY = str(Path(sys.argv.pop(1)).resolve())
arguments = sys.argv[:]
sys.argv = [sys.argv[0], BINARY]
spec = importlib.util.spec_from_file_location("mijoe_pty", Path(__file__).with_name("vim_pty_test.py"))
pty_tests = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pty_tests)
sys.argv = arguments


class UITests(unittest.TestCase):
    def setUp(self):
        self.directory = pty_tests.ARTIFACTS / self.id().split(".")[-1]
        self.directory.mkdir()
        self.editor = pty_tests.Editor(self.directory, b"abcdef\n")

    def tearDown(self):
        self.editor.close()

    def fresh(self, keys, token):
        offset = len(self.editor.output)
        self.editor.send(keys)
        self.editor.until(token)
        self.assertIn(token, self.editor.output[offset:])

    def assert_unchanged(self):
        self.assertEqual(self.editor.finish(), b"abcdef\n")

    def assert_gruvbox(self):
        self.editor.drain(0.15)
        self.assertRegex(self.editor.output, rb"\x1b\[38;(?:5;223|2;235;219;178)m")

    def test_help_lists_pages_and_preserves_cursor(self):
        self.editor.send("ll:help\r")
        self.editor.until(b"MiJoe :help")
        self.editor.until("已支持的 Vim".encode())
        self.fresh(b"\x1b[6~", b":saveas")
        self.fresh(b"\x1b[5~", b"h j k l")
        self.editor.send(b"qxi!\x1b")
        self.assertEqual(self.editor.finish(), b"ab!def\n")

    def test_help_is_readonly_and_escape_keeps_undo(self):
        self.editor.send(b"x:help\r")
        self.editor.until(b"MiJoe :help")
        self.editor.send(b"iddccyy.:wq\r")
        self.editor.send(b"\x1bu")
        self.assert_unchanged()

    def test_help_page_boundaries_and_alias(self):
        self.editor.ex("h")
        self.editor.until(b"MiJoe :help")
        self.editor.send(b"\x1b[5~" * 3)
        self.editor.send(b"\x1b[6~" * 8)
        self.editor.until("joe_state".encode())
        self.editor.send(b" " * 3 + b"b" * 8)
        self.editor.send(b"q")
        self.assert_unchanged()

    def test_help_paste_does_not_enter_insert_or_execute(self):
        self.editor.ex("help")
        self.editor.send(b"\x1b[200~\x0bHiBAD\x1b:wq\r\x1b[201~qx")
        self.assertEqual(self.editor.finish(), b"bcdef\n")

    def test_help_wraps_and_reflows_after_resize(self):
        self.editor.ex("help")
        self.editor.until(b"MiJoe :help")
        fcntl.ioctl(self.editor.fd, termios.TIOCSWINSZ, struct.pack("HHHH", 12, 52, 0, 0))
        os.kill(self.editor.pid, signal.SIGWINCH)
        self.editor.drain(0.15)
        self.editor.send(b"\x1b[6~" * 3)
        fcntl.ioctl(self.editor.fd, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 140, 0, 0))
        os.kill(self.editor.pid, signal.SIGWINCH)
        self.editor.drain(0.15)
        self.editor.send(b"q")
        self.assert_unchanged()

    def test_native_help_shortcut_and_paging(self):
        self.editor.send(b"\x0bH")
        self.editor.until(b"REGION")
        self.fresh(b"\x1b[6~", b"MACROS")
        self.fresh(b"\x1b[5~", b"REGION")
        self.editor.send(b"\x0bh")
        self.assert_unchanged()

    def test_native_window_shortcut_does_not_start_till_motion(self):
        self.editor.send(b"\x0bTx")
        self.assertEqual(self.editor.finish(), b"bcdef\n")

    def test_native_help_from_new_help_and_back(self):
        self.editor.ex("help")
        self.editor.send(b"\x0bH")
        self.editor.until(b"REGION")
        self.editor.send(b"\x0bH:help\rq")
        self.assert_unchanged()

    def test_closing_new_help_restores_existing_joe_help(self):
        self.editor.send(b"\x0bH")
        self.editor.until(b"REGION")
        self.editor.ex("help")
        self.editor.until(b"MiJoe :help")
        self.fresh(b"q", b"REGION")
        self.fresh(b"\x1b[6~", b"MACROS")
        self.editor.send(b"\x0bH")
        self.assert_unchanged()

    def test_options_menu_theme_prompt(self):
        self.editor.send(b"\x14")
        self.editor.until(b"Color scheme")
        self.editor.send(b"S")
        self.editor.until(b"Select color scheme")
        self.editor.send(b"gruvbox\r")
        self.assert_gruvbox()
        self.assert_unchanged()

    def test_menu_preserves_insert_mode(self):
        self.editor.send(b"iX\x14")
        self.editor.until(b"Color scheme")
        self.editor.send(b"\x03Y\x1b")
        self.assertEqual(self.editor.finish(), b"XYabcdef\n")

    def test_color_menu_select_and_cancel(self):
        self.editor.ex("colors")
        self.editor.until(b"gruvbox")
        self.editor.send(b"g")
        self.assert_gruvbox()
        self.editor.ex("colorscheme")
        self.editor.send(b"\x1b[6~\x1b[5~\x03")
        self.assert_unchanged()

    def test_color_by_name_and_missing_theme(self):
        self.editor.ex("colorscheme gruvbox")
        self.assert_gruvbox()
        self.editor.ex("colors no-such-mijoe-theme")
        self.editor.until(b"Color scheme file not found")
        self.editor.ex("colors default")
        self.assert_unchanged()

    def test_ctrl_k_in_paste_remains_data(self):
        self.editor.send(b"i\x1b[200~A\x0bHB\x1b[201~\x1b")
        self.assertEqual(self.editor.finish(), b"A\x0bHBabcdef\n")


if __name__ == "__main__":
    print(f"UI PTY transcripts: {pty_tests.ARTIFACTS}", flush=True)
    unittest.main(verbosity=2)
