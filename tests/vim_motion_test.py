#!/usr/bin/env python3
"""Compare the real editor with installed Vim for character and % motions."""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

if len(sys.argv) < 2:
    raise SystemExit("usage: vim_motion_test.py /path/to/mijoe")
BINARY = str(Path(sys.argv.pop(1)).resolve())
VIM = shutil.which("vim")
if not VIM:
    raise SystemExit("Install Vim to run the motion reference comparisons")
arguments = sys.argv[:]
sys.argv = [sys.argv[0], BINARY]
spec = importlib.util.spec_from_file_location("mijoe_pty", Path(__file__).with_name("vim_pty_test.py"))
pty_tests = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pty_tests)
sys.argv = arguments
CASES = [
    ("find-forward", "abxcxd tail\n", "fxi!\x1b"),
    ("find-backward", "abxcxd tail\n", "$Fxi!\x1b"),
    ("till-forward", "abxcxd tail\n", "txi!\x1b"),
    ("till-backward", "abxcxd tail\n", "$Txi!\x1b"),
    ("find-count", "abxcxd tail\n", "2fxi!\x1b"),
    ("find-digit", "a1b2c2 tail\n", "f2x"),
    ("find-space", "ab cd ef\n", "f x"),
    ("find-command-character", "abfF;,% tail\n", "fFx"),
    ("find-semicolon-character", "abfF;,% tail\n", "f;x"),
    ("find-comma-character", "abfF;,% tail\n", "f,x"),
    ("find-percent-character", "abfF;,% tail\n", "f%x"),
    ("find-chinese", "甲乙雪丙雪丁\n", "2f雪x"),
    ("till-chinese", "甲乙雪丙雪丁\n", "t雪x"),
    ("repeat-forward", "aXbXcXdX tail\n", "fX;x"),
    ("repeat-backward", "aXbXcXdX tail\n", "$FX;x"),
    ("repeat-reverse-stays-original-direction", "aXbXcXdX tail\n", "2fX,;x"),
    ("repeat-till-skips-adjacent", "aXbXcXdX tail\n", "tX;x"),
    ("repeat-till-count", "aXbXcXdX tail\n", "tX2;x"),
    ("repeat-till-reverse-count", "aXbXcXdX tail\n", "$TX2;x"),
    ("repeat-till-backward", "aXbXcXdX tail\n", "$TX;x"),
    ("repeat-till-reverse", "aXbXcXdX tail\n", "3tX,x"),
    ("delete-find-forward", "abxcxd tail\n", "dfx"),
    ("delete-find-backward-exclusive", "abxcxd tail\n", "$dFx"),
    ("delete-till-forward", "abxcxd tail\n", "dtx"),
    ("delete-till-backward-exclusive", "abxcxd tail\n", "$dTx"),
    ("delete-count-multiplication", "aXbXcXdXeXfX tail\n", "2d3fX"),
    ("change-find", "abxcxd tail\n", "cfxQ\x1b"),
    ("yank-find-forward", "abxcxd tail\n", "yfx$p"),
    ("yank-find-backward-position", "abxcxd tail\n", "$yFxp"),
    ("delete-repeat-find", "aXbXcXdX tail\n", "fXd;"),
    ("repeat-change-find", "aXbXcXdX tail\n", "dfX."),
    ("repeat-change-till", "aXbXcXdX tail\n", "dtXl."),
    ("dot-keeps-latest-find", "aXbYcXdYeXfYgXhYtail\n", "dfXfY.;x"),
    ("visual-find", "abxcxd tail\n", "vfxd"),
    ("visual-till-uppercase-V", "abcVdefV tail\n", "vtVd"),
    ("visual-find-uppercase-V", "abcVdefV tail\n", "vfVd"),
    ("visual-till-uppercase-V-count", "abcVdefV tail\n", "v2tVd"),
    ("visual-find-uppercase-V-count", "abcVdefV tail\n", "v2fVd"),
    ("visual-find-uppercase-V-copy", "abcVdefV tail\n", "vfVy$p"),
    ("visual-till-uppercase-V-change", "abcVdefV tail\n", "vtVcQ\x1b"),
    ("visual-find-mode-key-target", "abvcdV tail\n", "vfvd"),
    ("visual-till-operator-key-target", "abcdEF tail\n", "vtdd"),
    ("visual-till-backward", "abxcxd tail\n", "$vTxd"),
    ("percent-parentheses", "call(a(b)c) tail\n", "%x"),
    ("percent-closing-backward", "(a(b)c) tail\n", "f)%x"),
    ("percent-square", "a[b[c]d] tail\n", "%x"),
    ("percent-curly-multiline", "a {\n (b[c])\n} tail\n", "%x"),
    ("percent-quotes", '(a "ignored )" b) tail\n', "%x"),
    ("percent-character-literal", "(a ')' b) tail\n", "%x"),
    ("percent-escaped-brackets", r"\( ( \) tail" + "\n", "%x"),
    ("percent-in-string", '"(a)" tail\n', "%x"),
    ("percent-comment", "/* comment */ tail\n", "%x"),
    ("percent-comment-backward", "/* comment */ tail\n", "f/%x"),
    ("percent-comment-on-star", "/* comment */ tail\n", "l%x"),
    ("percent-comment-closing-star", "/* comment */ tail\n", "f*;%x"),
    ("percent-comment-multiline", "/* begin\n comment\n end */ tail\n", "%x"),
    ("percent-comment-delete", "/* comment */ tail\n", "d%"),
    ("percent-preprocessor", "#if A\nA\n#else\nB\n#endif\n", "%x"),
    ("percent-preprocessor-nested", "#if A\n#if B\nB\n#endif\n#else\nC\n#endif\n", "%x"),
    ("percent-preprocessor-backward", "#if A\nA\n#else\nB\n#endif\n", "G%x"),
    ("percent-preprocessor-cycle", "#if A\nA\n#elif B\nB\n#else\nC\n#endif\n", "%%%x"),
    ("percent-preprocessor-linewise-delete", "#if A\nA\n#else\nB\n#endif\n", "d%"),
    ("percent-preprocessor-indented", "  #ifdef A\nA\n  #endif\n", "%x"),
    ("percent-preprocessor-fallback", "#ifndef A\nA\n#endif\n", "fA%x"),
    ("percent-even-escapes", r"\\( \) ) tail" + "\n", "%x"),
    ("percent-character-literal-backward", "(a '(' b) tail\n", "f)%x"),
    ("percent-double-quotes-backward", '(a "ignored (" b) tail\n', "f)%x"),
    ("percent-odd-double-quote", '(a "odd ) tail\n', "%x"),
    ("percent-double-quote-character", "(a '\"' b) tail\n", "%x"),
    ("percent-no-final-newline", "(a) tail", "%x"),
    ("percent-crlf", "a {\r\n body\r\n} tail\r\n", "%x"),
    ("percent-number", "a\nb\nc\nd\ne\nf\ng\n", "50%x"),
    ("percent-one-hundred", "a\nb\nc\nd\ne\nf\ng\n", "100%x"),
    ("percent-delete-pair", "(a(b)c) tail\n", "d%"),
    ("percent-delete-before-pair", "call(a) tail\n", "d%"),
    ("percent-delete-backwards", "(a(b)c) tail\n", "2f)d%"),
    ("percent-visual", "(a(b)c) tail\n", "v%d"),
    ("percent-delete-number", "a\nb\nc\nd\ne\nf\ng\n", "d50%"),
]


def vim_string(text):
    special = {"\x1b": r"\<Esc>", "\r": r"\<CR>", "\x12": r"\<C-R>",
               "\x16": r"\<C-V>", "\x08": r"\<BS>", "\x03": r"\<C-C>"}
    return '"' + "".join(special.get(char, '\\' + char if char in '\\"' else char) for char in text) + '"'


class MotionTests(unittest.TestCase):
    def setUp(self):
        self.directory = pty_tests.ARTIFACTS / self.id().split(".")[-1]
        self.directory.mkdir()

    def oracle(self, directory, initial, keys):
        source = directory / "reference.txt"
        result = directory / "reference-result.txt"
        script = directory / "reference.vim"
        source.write_bytes(initial)
        script.write_text("set nocompatible encoding=utf-8 nofixeol nomore\nset cpoptions&vim\ncall cursor(1, 1)\n"
                          + "execute " + vim_string("normal! " + keys) + "\n"
                          + "execute 'write! ' . fnameescape('" + str(result).replace("'", "''") + "')\nqa!\n")
        run = subprocess.run([VIM, "-Nu", "NONE", "-i", "NONE", "-n", "-es", "-S", str(script), str(source)],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=10)
        self.assertEqual(run.returncode, 0, run.stdout)
        return result.read_bytes()

    def test_vim_reference_cases(self):
        evidence = []
        for name, text, keys in CASES:
            with self.subTest(case=name):
                directory = self.directory / name
                directory.mkdir()
                initial = text.encode()
                expected = self.oracle(directory, initial, keys)
                editor = pty_tests.Editor(directory, initial)
                try:
                    editor.send(keys)
                    actual = editor.finish()
                    evidence.append(dict(case=name, keys=keys, expected=expected.hex(), actual=actual.hex()))
                    self.assertEqual(actual, expected)
                finally:
                    editor.close()
        (self.directory / "comparison.json").write_text(json.dumps(evidence, ensure_ascii=False, indent=2))

    def test_failed_find_does_not_cross_line_or_delete(self):
        editor = pty_tests.Editor(self.directory, b"abXcd\nX next\n")
        try:
            editor.send(b"d2fX")
            self.assertEqual(editor.finish(), b"abXcd\nX next\n")
        finally:
            editor.close()

    def test_escape_cancels_find_and_count(self):
        editor = pty_tests.Editor(self.directory, b"abcdef\n")
        try:
            editor.send(b"2df\x1bx")
            self.assertEqual(editor.finish(), b"bcdef\n")
        finally:
            editor.close()

    def test_repeat_without_previous_find(self):
        editor = pty_tests.Editor(self.directory, b"abcdef\n")
        try:
            editor.send(b"d;")
            self.assertEqual(editor.finish(), b"abcdef\n")
        finally:
            editor.close()

    def test_unmatched_percent_does_not_delete(self):
        editor = pty_tests.Editor(self.directory, b"before (unmatched\nnext\n")
        try:
            editor.send(b"d%")
            self.assertEqual(editor.finish(), b"before (unmatched\nnext\n")
        finally:
            editor.close()

    def test_invalid_percentage_does_not_delete(self):
        editor = pty_tests.Editor(self.directory, b"a\nb\nc\n")
        try:
            editor.send(b"d101%")
            self.assertEqual(editor.finish(), b"a\nb\nc\n")
        finally:
            editor.close()

    def test_failed_find_preserves_cursor_and_next_command(self):
        editor = pty_tests.Editor(self.directory, b"aXbXcXdX\n")
        try:
            # The reverse T repeat fails because its adjacent X is skipped.
            # Separate real input continues with x, even after a failed motion.
            editor.send(b"2tX,x")
            self.assertEqual(editor.finish(), b"aXXcXdX\n")
        finally:
            editor.close()


if __name__ == "__main__":
    print(f"Motion transcripts: {pty_tests.ARTIFACTS}; Vim oracle cases: {len(CASES)}", flush=True)
    unittest.main(verbosity=2)
