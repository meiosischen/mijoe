#!/usr/bin/env python3
"""Exercise the real editor through a controlling PTY; standard library only.

python3 tests/vim_pty_test.py /absolute/path/to/joe
All edited files and terminal transcripts live in a temporary directory.
"""
import errno
import fcntl
import os
from pathlib import Path
import pty
import select
import signal
import struct
import sys
import tempfile
import termios
import time
import unittest

BINARY = str(Path(sys.argv.pop(1)).resolve())
ARTIFACTS = Path(tempfile.mkdtemp(prefix="mijoe-vim-pty-"))


class Editor:
    def __init__(self, directory, initial=b"", filename="sample.txt", options=(), extra_env=None):
        self.directory = directory
        self.path = directory / filename
        self.path.write_bytes(initial)
        self.output = bytearray()
        self.status = None
        self.pid, self.fd = pty.fork()
        if not self.pid:
            os.chdir(directory)
            env = dict(os.environ, TERM="xterm-256color", LANG="en_US.UTF-8")
            if extra_env:
                env.update(extra_env)
            os.execve(BINARY, [BINARY, "-nonotice", "--joe_state", "-nobackups",
                              "-nolocks", *options, str(self.path)], env)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 140, 0, 0))
        os.set_blocking(self.fd, False)
        self.until(b"[N]" if "-novim" not in options else b"JOE")

    def drain(self, seconds=0.06):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            ready, _, _ = select.select([self.fd], [], [], max(0, deadline-time.monotonic()))
            if not ready:
                break
            try:
                data = os.read(self.fd, 65536)
            except BlockingIOError:
                continue
            except OSError as exc:
                if exc.errno == errno.EIO:
                    break
                raise
            if not data:
                break
            self.output.extend(data)

    def until(self, token, timeout=5):
        deadline = time.monotonic() + timeout
        while token not in self.output and time.monotonic() < deadline:
            self.drain(0.1)
            pid, status = os.waitpid(self.pid, os.WNOHANG)
            if pid:
                self.status = status
                break
        if token not in self.output:
            raise AssertionError(f"Missing {token!r}: {bytes(self.output)[-1500:]!r}")

    def send(self, keys):
        data = keys.encode() if isinstance(keys, str) else keys
        deadline = time.monotonic() + 10
        while data:
            readable, writable, _ = select.select([self.fd], [self.fd], [], 0.1)
            if readable:
                self.drain(0.01)
            if writable:
                try:
                    written = os.write(self.fd, data[:512])
                    data = data[written:]
                except BlockingIOError:
                    pass
            if time.monotonic() > deadline:
                raise AssertionError("Timed out sending terminal input")
        self.drain()

    def ex(self, command):
        self.send(":" + command + "\r")

    def finish(self, save=True):
        if save:
            self.ex("w")
        self.ex("q!")
        deadline = time.monotonic() + 5
        while self.status is None and time.monotonic() < deadline:
            self.drain(0.05)
            pid, status = os.waitpid(self.pid, os.WNOHANG)
            if pid:
                self.status = status
        if self.status != 0:
            raise AssertionError(f"Editor exit={self.status}: {bytes(self.output)[-1000:]!r}")
        return self.path.read_bytes()

    def close(self):
        if self.status is None:
            try:
                os.kill(self.pid, signal.SIGTERM)
                os.waitpid(self.pid, 0)
            except ProcessLookupError:
                pass
        os.close(self.fd)
        (self.directory / "terminal.log").write_bytes(self.output)


class VimTests(unittest.TestCase):
    def setUp(self):
        self.directory = ARTIFACTS / self.id().split(".")[-1]
        self.directory.mkdir()
        self.editors = []

    def tearDown(self):
        for editor in self.editors:
            editor.close()

    def editor(self, initial=b"", **kwargs):
        editor = Editor(self.directory, initial, **kwargs)
        self.editors.append(editor)
        return editor

    def result(self, initial, keys, expected):
        editor = self.editor(initial)
        editor.send(keys)
        self.assertEqual(editor.finish(), expected)

    def test_insert_unicode_and_escape(self):
        self.result(b"", "i中文 alpha\rsecond\x1b", "中文 alpha\nsecond".encode())

    def test_word_and_punctuation_motion(self):
        self.result(b"one,two three\n", b"wxi!\x1b", b"one!two three\n")

    def test_line_and_file_motion(self):
        self.result(b"  abc\nsecond\nthird\n", b"Ggg^x2G$x", b"  bc\nsecon\nthird\n")

    def test_word_end_and_back(self):
        self.result(b"one two three\n", b"weblx", b"one to three\n")

    def test_horizontal_motion_does_not_cross_line(self):
        self.result(b"ab\ncd\n", b"10l10hx", b"b\ncd\n")

    def test_insert_before_and_append(self):
        self.result(b"  ab\n", b"I!\x1bA?\x1b0aX\x1b", b" X !ab?\n")

    def test_counted_delete_and_undo(self):
        self.result(b"a\nb\nc\nd\ne\n", b"3ddu\x12", b"d\ne\n")

    def test_operator_multiplies_counts(self):
        self.result(b"a b c d e f g\n", b"2d3w", b"g\n")

    def test_operator_to_end_of_file_without_count(self):
        self.result(b"a\nb\nc\nd\n", b"jdG", b"a\n")

    def test_change_word_undo_and_dot(self):
        self.result(b"one two three\n", b"cwX\x1bw.", b"X X three\n")

    def test_insert_session_one_undo(self):
        self.result(b"ab\n", b"Axyz\rnew\x1bu", b"ab\n")

    def test_character_yank_and_put(self):
        self.result(b"abc def\n", b"yw$p", b"abc defabc \n")

    def test_counted_change_word(self):
        self.result(b"one two three\n", b"c2wX\x1b", b"X three\n")

    def test_word_delete_preserves_next_indented_line(self):
        self.result(b"one\n  two\n", b"dw", b"\n  two\n")

    def test_delete_to_end_on_empty_line(self):
        self.result(b"\nnext\n", b"d$", b"\nnext\n")

    def test_yank_line_preserves_column(self):
        self.result(b"abcd\nnext\n", b"llyyx", b"abd\nnext\n")

    def test_dot_delete_and_undo(self):
        self.result(b"a\nb\nc\nd\n", b"dd.u", b"b\nc\nd\n")

    def test_visual_dot(self):
        self.result(b"abcdefgh\n", b"vld.", b"efgh\n")

    def test_line_put_on_unterminated_eof(self):
        self.result(b"a\nb", b"GyyP", b"a\nb\nb")

    def test_open_lines(self):
        self.result(b"a\nb\n", b"oX\x1bOY\x1b", b"a\nY\nX\nb\n")

    def test_line_yank_and_put(self):
        self.result(b"a\nb\n", b"yyp", b"a\na\nb\n")

    def test_final_unterminated_line_delete(self):
        self.result(b"a\nb", b"Gdd", b"a")

    def test_visual_inclusive_delete(self):
        self.result(b"abcdef\n", b"vlld", b"def\n")

    def test_visual_line_change(self):
        self.result(b"a\nb\nc\n", b"VjcX\x1b", b"X\nc\n")

    def test_visual_escape_no_edit(self):
        self.result(b"abc\n", b"vll\x1b", b"abc\n")

    def test_arrows_and_lone_escape(self):
        self.result(b"ab\ncd\n", b"iX\x1b\x1b[B\x1b[CX\x1b[A", b"Xab\ncd\n")

    def test_star_on_empty_line_keeps_cursor_and_content(self):
        self.result(b"\nfoo\n", b"*i!\x1b", b"!\nfoo\n")

    def test_search_wrap_and_reverse(self):
        self.result(b"one two one\n", b"/one\rnxNxi!\x1b", b"ne two !ne\n")

    def test_crlf_preserved(self):
        self.result(b"a\r\nb\r\n", b"A!\x1b", b"a!\r\nb\r\n")

    def test_write_copy_and_saveas(self):
        editor = self.editor(b"old\n")
        editor.send(b"ccnew\x1b")
        editor.ex('w "copy space.txt"')
        self.assertEqual((self.directory / "copy space.txt").read_bytes(), b"new\n")
        self.assertEqual(editor.path.read_bytes(), b"old\n")
        editor.ex('saveas "另存为 space.txt"')
        editor.send(b"A!\x1b")
        editor.ex("w")
        editor.finish(save=False)
        self.assertEqual((self.directory / "另存为 space.txt").read_bytes(), b"new!\n")
        self.assertEqual(editor.path.read_bytes(), b"old\n")

    def test_overwrite_requires_bang(self):
        target = self.directory / "exists.txt"
        target.write_bytes(b"keep")
        editor = self.editor(b"new\n")
        editor.ex("w exists.txt")
        self.assertEqual(target.read_bytes(), b"keep")
        editor.ex("w! exists.txt")
        self.assertEqual(target.read_bytes(), b"new\n")
        editor.finish(save=False)

    def test_copy_write_keeps_original_undo_savepoint(self):
        editor = self.editor(b"old\n")
        editor.send(b"ccnew\x1b")
        editor.ex("w copy.txt")
        editor.send(b"u")
        editor.ex("q")
        editor.drain(0.1)
        pid, status = os.waitpid(editor.pid, os.WNOHANG)
        self.assertEqual(pid, editor.pid, "Undo to original content should allow :q")
        editor.status = status
        self.assertEqual(status, 0)

    def test_failed_write_does_not_quit(self):
        editor = self.editor(b"a\n")
        editor.send(b"A!\x1b")
        editor.ex("wq missing/failed.txt")
        self.assertIsNone(editor.status)
        editor.ex("q")
        self.assertIn(b"No write", editor.output)
        self.assertEqual(editor.finish(), b"a!\n")

    def test_unsaved_buffer_switch_and_close_protection(self):
        other = self.directory / "other.txt"
        other.write_bytes(b"other\n")
        editor = self.editor(b"first\n")
        editor.send(b"A!\x1b")
        editor.ex("e other.txt")
        editor.ex("bp")
        editor.ex("bd")
        self.assertIn(b"No write", editor.output)
        editor.ex("w")
        editor.ex("bn")
        editor.ex("q")
        self.assertEqual(editor.path.read_bytes(), b"first!\n")

    def test_new_buffer_number_and_switch(self):
        editor = self.editor(b"first\n")
        editor.ex("enew")
        editor.send(b"inew\x1b")
        editor.ex("w second.txt")
        editor.ex("b 1")
        editor.send(b"A!\x1b")
        self.assertEqual(editor.finish(), b"first!\n")
        self.assertEqual((self.directory / "second.txt").read_bytes(), b"new")

    def test_unknown_command_does_not_insert(self):
        self.result(b"abc\n", b":unknown\r", b"abc\n")

    def test_long_or_unterminated_filename_rejected(self):
        editor = self.editor(b"abc\n")
        editor.ex('w "unfinished')
        self.assertIn(b"Unterminated", editor.output)
        editor.ex("w " + "a" * 1100)
        editor.until(b"too long")
        editor.finish(save=False)

    def test_bracketed_paste_is_data(self):
        self.result(b"", b"\x1b[200~dd:q!\ntext\x1b[201~", b"dd:q!\ntext")

    def test_paste_escape_and_dot_remain_data(self):
        self.result(b"", b"\x1b[200~a\x1bdd:q!\x1b[201~.", b"a\x1bdd:qa\x1bdd:q!!")

    def test_command_line_paste(self):
        editor = self.editor(b"abc\n")
        editor.send(b":w \x1b[200~copy space.txt\x1b[201~\r")
        self.assertEqual((self.directory / "copy space.txt").read_bytes(), b"abc\n")
        self.assertEqual(editor.finish(save=False), b"abc\n")

    def test_pasted_newline_does_not_execute_command(self):
        editor = self.editor(b"abc\n")
        editor.send(b":q\x1b[200~\r\x1b[201~")
        self.assertIsNone(editor.status)
        editor.send(b"\x1b")
        self.assertEqual(editor.finish(save=False), b"abc\n")

    def test_visual_paste_replaces_selection(self):
        self.result(b"abcdef\n", b"vl\x1b[200~X\x1b[201~", b"Xcdef\n")

    def test_hidden_unsaved_buffer_blocks_exit(self):
        editor = self.editor(b"a\n")
        editor.send(b"A!\x1b")
        editor.ex("enew")
        editor.ex("q")
        self.assertIn(b"unsaved buffer", editor.output)
        editor.ex("b 1")
        self.assertEqual(editor.finish(), b"a!\n")

    def test_readonly_edit_protected(self):
        editor = self.editor(b"keep\n", options=("-rdonly",))
        editor.send(b"dd")
        self.assertEqual(editor.finish(save=False), b"keep\n")

    def test_unicode_search_and_backspace_command(self):
        self.result("甲乙甲\n".encode(), "/甲\rx\x1b", "甲乙\n".encode())

    def test_classic_joe_switch(self):
        editor = self.editor(b"", options=("-novim",))
        editor.send(b"abc")
        editor.send(b"\x0b\x18")  # JOE save-and-exit.
        editor.drain(0.2)
        self.assertEqual(editor.path.read_bytes(), b"abc")


if __name__ == "__main__":
    print(f"PTY transcripts: {ARTIFACTS}", flush=True)
    unittest.main(verbosity=2)
