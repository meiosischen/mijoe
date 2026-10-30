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

    def reopen(self, initial=b"abcdef\n", options=()):
        self.editor.finish(save=False)
        self.editor.close()
        self.editor = pty_tests.Editor(self.directory, initial, options=options)

    def assert_gruvbox(self):
        self.editor.drain(0.15)
        self.assertRegex(self.editor.output, rb"\x1b\[38;(?:5;223|2;235;219;178)m")

    def test_help_lists_pages_and_preserves_cursor(self):
        self.editor.send("ll:help\r")
        self.editor.until(b"MiJoe :help")
        self.editor.until("已支持的 Vim".encode())
        # New command sections can move saveas onto a later help page.
        offset=len(self.editor.output)
        for _ in range(16):
            self.editor.send(b"\x1b[6~")
            if b":saveas" in self.editor.output[offset:]: break
        self.assertIn(b":saveas",self.editor.output[offset:])
        self.fresh(b"\x1b[5~" * 16, b"h j k l")
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

    def test_ctrl_t_closes_options_without_enabling_overtype(self):
        self.editor.send(b"\x14\x14iXY\x1b")
        self.assertEqual(self.editor.finish(), b"XYabcdef\n")

    def test_ctrl_t_closes_options_without_disabling_overtype(self):
        self.editor.send(b"\x14T\x14\x14iXY\x1b")
        self.assertEqual(self.editor.finish(), b"XYcdef\n")

    def test_ctrl_t_toggle_preserves_insert_mode(self):
        self.editor.send(b"iX\x14\x14Y\x1b")
        self.assertEqual(self.editor.finish(), b"XYabcdef\n")

    def test_options_explicit_t_still_toggles_overtype(self):
        self.editor.send(b"\x14T\x14tiXY\x1b")
        self.assertEqual(self.editor.finish(), b"XYabcdef\n")

    def test_picture_menu_allows_normal_motion_past_eol(self):
        self.editor.send(b"\x14P8liX\x1b")
        self.assertEqual(self.editor.finish(), b"abcdef  X\n")

    def test_picture_insert_arrows_move_past_eol(self):
        self.reopen(options=("-picture",))
        self.editor.send(b"i" + b"\x1b[C" * 8 + b"X\x1b")
        self.assertEqual(self.editor.finish(), b"abcdef  X\n")

    def test_picture_vertical_motion_keeps_virtual_column(self):
        self.reopen(b"abcdef\nx\n", options=("-picture",))
        self.editor.send(b"$jiX\x1b")
        self.assertEqual(self.editor.finish(), b"abcdef\nx    X\n")

    def test_picture_left_returns_from_virtual_column(self):
        self.reopen(options=("-picture",))
        self.editor.send(b"8l3hiX\x1b")
        self.assertEqual(self.editor.finish(), b"abcdeXf\n")

    def test_picture_append_after_virtual_cursor(self):
        self.reopen(options=("-picture",))
        self.editor.send(b"8laX\x1b")
        self.assertEqual(self.editor.finish(), b"abcdef   X\n")

    def test_picture_append_eol_resets_virtual_column(self):
        self.reopen(options=("-picture",))
        self.editor.send(b"8lAX\x1b")
        self.assertEqual(self.editor.finish(), b"abcdefX\n")

    def test_picture_down_extends_unterminated_file_and_undoes(self):
        self.reopen(b"ab", options=("-picture",))
        self.editor.send(b"2ju")
        self.assertEqual(self.editor.finish(), b"ab")

    def test_picture_readonly_down_does_not_extend_buffer(self):
        self.reopen(b"ab", options=("-picture", "-rdonly"))
        self.editor.send(b"3j")
        self.editor.ex("w!")
        self.assertEqual(self.editor.finish(save=False), b"ab")

    def test_picture_virtual_column_uses_display_cells(self):
        self.reopen("甲b\n".encode(), options=("-picture",))
        self.editor.send(b"4liX\x1b")
        self.assertEqual(self.editor.finish(), "甲b X\n".encode())

    def test_picture_visual_and_operator_use_text_ranges(self):
        self.reopen(options=("-picture",))
        self.editor.send(b"v2lddl")
        self.assertEqual(self.editor.finish(), b"ef\n")

    def test_picture_undo_resets_column_to_restored_text(self):
        self.reopen(options=("-picture",))
        self.editor.send(b"8liX\x1buiY\x1b")
        self.assertEqual(self.editor.finish(), b"abcdefY\n")

    def test_picture_line_change_starts_at_text_column(self):
        self.reopen(b"abc\ndef\n", options=("-picture",))
        self.editor.send(b"llccX\x1b")
        self.assertEqual(self.editor.finish(), b"X\ndef\n")

    def test_picture_block_insert_uses_left_edge(self):
        self.reopen(b"abc\ndef\nghi\n", options=("-picture",))
        self.editor.send(b"\x16lljjIX\x1b")
        self.assertEqual(self.editor.finish(), b"Xabc\nXdef\nXghi\n")

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

    def test_synhill_default_256color_and_rgb(self):
        self.editor.finish(save=False)
        self.editor.close()
        source = b'int answer = 42; // synhill comment\nchar *s = "hello";\n'
        self.editor = pty_tests.Editor(self.directory, source, filename="theme.c", options=("-colors", "synhill"),
                                      extra_env={"COLORTERM": "", "HOME": str(self.directory)})
        self.editor.drain(0.2)
        for color in (167, 230, 135, 39):
            self.assertIn(f"\x1b[38;5;{color}m".encode(), self.editor.output)
        self.editor.finish(save=False)
        self.editor.close()
        self.editor = pty_tests.Editor(self.directory, source, filename="theme.c", options=("-colors", "synhill"),
                                      extra_env={"COLORTERM": "truecolor", "HOME": str(self.directory)})
        self.editor.drain(0.2)
        for color in ("215;95;95", "255;255;215", "175;95;255", "0;175;255"):
            self.assertIn(f"\x1b[38;2;{color}m".encode(), self.editor.output)
        self.assertEqual(self.editor.finish(), source)

    def test_ctrl_k_in_paste_remains_data(self):
        self.editor.send(b"i\x1b[200~A\x0bHB\x1b[201~\x1b")
        self.assertEqual(self.editor.finish(), b"A\x0bHBabcdef\n")


if __name__ == "__main__":
    print(f"UI PTY transcripts: {pty_tests.ARTIFACTS}", flush=True)
    unittest.main(verbosity=2)
