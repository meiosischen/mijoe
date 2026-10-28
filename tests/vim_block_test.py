#!/usr/bin/env python3
"""Compare real PTY block insert/append behavior with installed Vim."""
import importlib.util
import json
from pathlib import Path
import sys
import unittest

if len(sys.argv) < 2:
    raise SystemExit("usage: vim_block_test.py /path/to/joe")
BINARY = str(Path(sys.argv.pop(1)).resolve())
arguments = sys.argv[:]
sys.argv = [sys.argv[0], BINARY]
spec = importlib.util.spec_from_file_location("motion_tests", Path(__file__).with_name("vim_motion_test.py"))
motion_tests = importlib.util.module_from_spec(spec)
spec.loader.exec_module(motion_tests)
sys.argv = arguments
pty_tests = motion_tests.pty_tests
CASES = [
    ("insert-line-start", "abc\ndef\nghi\n", "\x16jjI// \x1b"),
    ("insert-current-column", "abcd\nefgh\nijkl\n", "ll\x16jjIX\x1b"),
    ("append-current-column", "abcd\nefgh\nijkl\n", "l\x16jjAX\x1b"),
    ("append-block-width", "abcd\nefgh\nijkl\n", "\x16lljjAX\x1b"),
    ("insert-upwards", "abcd\nefgh\nijkl\n", "jjl\x16kkIX\x1b"),
    ("append-upwards", "abcd\nefgh\nijkl\n", "jjl\x16kkAX\x1b"),
    ("insert-counted-height", "abc\ndef\nghi\n", "\x162jIX\x1b"),
    ("insert-skip-short-lines", "abcde\nx\n\nABCDE\n", "ll\x16jjjIX\x1b"),
    ("insert-at-equal-length", "abcde\nab\nx\n\nABCDE\n", "ll\x16jjjjIX\x1b"),
    ("append-pad-short-lines", "abcde\nx\n\nABCDE\n", "ll\x16jjjAX\x1b"),
    ("short-line-endpoint", "abcde\nx\n", "ll\x16jIX\x1b"),
    ("short-line-top", "x\nabcde\nABCDE\n", "jll\x16kIX\x1b"),
    ("empty-line-start", "\nabc\n", "\x16jIX\x1b"),
    ("append-each-line-end", "ab\ncdef\nx\n", "\x16jj$AX\x1b"),
    ("append-end-before-vertical-motion", "ab\ncdef\nx\n", "\x16$jjAX\x1b"),
    ("insert-unicode", "abc\ndef\nghi\n", "\x16jjI中文\x1b"),
    ("insert-after-wide-characters", "甲abc\n乙def\n丙ghi\n", "l\x16jjIX\x1b"),
    ("insert-interior-wide-character", "abcd\n甲乙\nZZZZ\n", "l\x16jjIX\x1b"),
    ("append-interior-wide-character", "abcd\n甲乙\nZZZZ\n", "\x16jjAX\x1b"),
    ("insert-tab-interior", "abcd\n\tXYZ\nZZZZ\n", "ll\x16jjIX\x1b"),
    ("append-tab-interior", "abcd\n\tXYZ\nZZZZ\n", "ll\x16jjAX\x1b"),
    ("tab-endpoint", "aBC\n\tXYZ\n", "l\x16jAX\x1b"),
    ("insert-tab-text", "abc\ndef\n", "\x16jI\tX\x1b"),
    ("backspace-in-added-text", "ab\ncd\n", "\x16jIXY\x08Z\x1b"),
    ("empty-insert", "ab\n\tcd\n", "\x16jI\x1b"),
    ("empty-append", "abcde\nx\n", "ll\x16jA\x1b"),
    ("insert-newline-first-line-only", "ab\ncd\n", "\x16jIX\rY\x1b"),
    ("undo-whole-block", "abc\ndef\nghi\n", "\x16jjIXY\x1bu"),
    ("redo-whole-block", "abc\ndef\nghi\n", "\x16jjIXY\x1bu\x12"),
    ("repeat-block-insert", "abc\ndef\nghi\n", "\x16jIX\x1bl."),
    ("switch-to-character-visual", "abcd\nefgh\n", "\x16jvd"),
    ("switch-to-line-visual", "abcd\nefgh\n", "\x16jVd"),
    ("switch-from-character-visual", "abcd\nefgh\n", "vl\x16jIX\x1b"),
    ("cancel-block", "abc\ndef\n", "\x16j\x1bx"),
    ("toggle-block-off", "abc\ndef\n", "\x16j\x16x"),
    ("unterminated-last-line", "abc\ndef", "\x16jIX\x1b"),
    ("windows-line-endings", "abc\r\ndef\r\n", "\x16jIX\x1b"),
]


class BlockTests(unittest.TestCase):
    oracle = motion_tests.MotionTests.oracle

    def setUp(self):
        self.directory = pty_tests.ARTIFACTS / self.id().split(".")[-1]
        self.directory.mkdir()

    def test_vim_block_reference_cases(self):
        results = []
        for name, initial, keys in CASES:
            with self.subTest(case=name):
                directory = self.directory / name
                directory.mkdir()
                expected = self.oracle(directory, initial.encode(), keys)
                editor = pty_tests.Editor(directory, initial.encode())
                try:
                    editor.send(keys)
                    actual = editor.finish()
                    results.append(dict(case=name, keys=keys, expected=expected.hex(), actual=actual.hex()))
                    self.assertEqual(actual, expected)
                finally:
                    editor.close()
        (self.directory / "comparison.json").write_text(json.dumps(results, ensure_ascii=False, indent=2))

    def test_readonly_block_insert_keeps_original(self):
        editor = pty_tests.Editor(self.directory, b"abc\ndef\n", options=("-rdonly",))
        try:
            editor.send(b"\x16jIX\x1b")
            self.assertEqual(editor.finish(save=False), b"abc\ndef\n")
        finally:
            editor.close()

    def test_bracketed_paste_in_block_insert(self):
        editor = pty_tests.Editor(self.directory, b"abc\ndef\n")
        try:
            editor.send("\x16jI\x1b[200~中文\x1b[201~\x1b")
            self.assertEqual(editor.finish(), "中文abc\n中文def\n".encode())
        finally:
            editor.close()

    def test_multiline_paste_changes_first_line_only(self):
        editor = pty_tests.Editor(self.directory, b"abc\ndef\n")
        try:
            editor.send(b"\x16jI\x1b[200~X\nY\x1b[201~\x1b")
            self.assertEqual(editor.finish(), b"X\nYabc\ndef\n")
        finally:
            editor.close()

    def test_paste_in_selection_does_not_edit(self):
        editor = pty_tests.Editor(self.directory, b"abc\ndef\n")
        try:
            editor.send(b"\x16j\x1b[200~BAD\x1b[201~\x1b")
            self.assertEqual(editor.finish(), b"abc\ndef\n")
        finally:
            editor.close()

    def test_cancel_before_insert_does_not_replace_dot(self):
        editor = pty_tests.Editor(self.directory, b"abc\ndef\nghi\n")
        try:
            editor.send(b"x\x16j\x1b.")
            self.assertEqual(editor.finish(), b"bc\nef\nghi\n")
        finally:
            editor.close()


if __name__ == "__main__":
    print(f"Block transcripts: {pty_tests.ARTIFACTS}; Vim oracle cases: {len(CASES)}", flush=True)
    unittest.main(verbosity=2)
