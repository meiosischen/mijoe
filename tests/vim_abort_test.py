#!/usr/bin/env python3
"""Exercise Ctrl+C with JOE's real window/buffer exit and confirmation rules."""
import importlib.util
import os
import time
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('pty_tests', Path(__file__).with_name('vim_pty_test.py'))
pty = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pty)


class AbortTests(unittest.TestCase):
    def setUp(self):
        self.directory = pty.ARTIFACTS / self.id().split('.')[-1]
        self.directory.mkdir()
        self.editor = pty.Editor(self.directory, b'alpha\nhit beta\n')

    def tearDown(self):
        self.editor.close()

    def exited(self):
        deadline = time.monotonic() + 5
        while self.editor.status is None and time.monotonic() < deadline:
            self.editor.drain(.05)
            pid, status = os.waitpid(self.editor.pid, os.WNOHANG)
            if pid:
                self.editor.status = status
        self.assertEqual(self.editor.status, 0)

    def test_clean_last_window_exits(self):
        self.editor.send('\x03')
        self.exited()

    def test_insert_dirty_cancel_then_confirm(self):
        self.editor.send('iX\x03')
        self.editor.until(b'Lose changes')
        self.editor.send('niY\x1b')
        self.editor.ex('w')
        self.assertEqual(self.editor.path.read_bytes(), b'XYalpha\nhit beta\n')
        self.editor.send('iZ\x1b\x03y')
        self.exited()
        self.assertEqual(self.editor.path.read_bytes(), b'XYalpha\nhit beta\n')

    def test_visual_selection_is_dismissed_before_exit(self):
        self.editor.send('vl\x03iX\x1b')
        self.assertEqual(self.editor.finish(), b'aXlpha\nhit beta\n')

    def test_grep_closes_and_restores_cursor(self):
        self.editor.send('02l:grep hit\r\x03iX\x1b')
        self.assertEqual(self.editor.finish(), b'alXpha\nhit beta\n')

    def test_inspection_views_close_without_exiting_editor(self):
        for command in ('help', 'ls', 'reg', 'macro'):
            self.editor.ex(command)
            self.editor.send('\x03')
        self.editor.send('iX\x1b')
        self.assertEqual(self.editor.finish(), b'Xalpha\nhit beta\n')

    def test_command_line_ctrl_c_cancels_input(self):
        self.editor.send(':q\x03iX\x1b')
        self.assertEqual(self.editor.finish(), b'Xalpha\nhit beta\n')

    def test_orphan_source_returns_before_last_window_closes(self):
        self.editor.send('iX\x1b:enew\r\x03')
        self.editor.ex('w')
        self.assertEqual(self.editor.path.read_bytes(), b'Xalpha\nhit beta\n')
        self.editor.send('\x03')
        self.exited()

    def test_shared_dirty_buffer_only_prompts_on_last_view(self):
        self.editor.send('iX\x1b\x0bO\x03')
        self.assertNotIn(b'Lose changes', self.editor.output)
        self.editor.send('\x03')
        self.editor.until(b'Lose changes')
        self.editor.send('n')
        self.editor.ex('w')
        self.assertEqual(self.editor.path.read_bytes(), b'Xalpha\nhit beta\n')
        self.editor.send('\x03')
        self.exited()


if __name__ == '__main__':
    print('Ctrl+C transcripts:', pty.ARTIFACTS, flush=True)
    unittest.main(verbosity=2)
