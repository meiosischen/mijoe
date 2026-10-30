#!/usr/bin/env python3
"""Register contents and modal cursor regressions against the real editor."""
import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('bridge',Path(__file__).with_name('macos_gui_bridge_test.py'))
bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)

class RegisterTests(unittest.TestCase):
    options=()
    setUp=bridge.BridgeTests.setUp
    tearDown=bridge.BridgeTests.tearDown
    packet=bridge.BridgeTests.packet
    receive=bridge.BridgeTests.receive
    call=bridge.BridgeTests.call
    wait_exit=bridge.BridgeTests.wait_exit
    def open(self,data):
        path=self.directory/'input.txt';path.write_bytes(data)
        self.editor.ex('e '+str(path));self.call(1);return path
    def state(self,keys=''):
        if keys:self.editor.send(keys)
        return self.call(1)
    def test_visual_yank_register_zero_in_command(self):
        self.open(b'alpha first\nother\nalpha second\n')
        r=self.state('v4ly:grep \x120');self.assertEqual(r['command'],'grep alpha')
        r=self.state('\r');self.assertEqual(r['grepCount'],2);self.editor.send('\x1b')
    def test_visual_selection_direct_to_command(self):
        self.open(b'alpha first\nalpha second\n')
        r=self.state('v4l:grep \x120');self.assertEqual(r['command'],'grep alpha')
        self.assertEqual(self.state('\r')['grepCount'],2);self.editor.send('\x1b')
    def test_joe_marked_selection_to_command(self):
        self.open(b'alpha first\nalpha second\n')
        r=self.state('\x0b\x025l\x0b\x0b:grep \x120')
        self.assertEqual(r['command'],'grep alpha')
        self.assertEqual(self.state('\r')['grepCount'],2);self.editor.send('\x1b')
    def test_native_copy_fills_zero_after_selection_cleared(self):
        self.open(b'alpha first\nalpha second\n')
        self.editor.send('v4l');self.assertEqual(self.call(4)['text'],'alpha')
        self.assertEqual(self.state('\x1b:grep \x120')['command'],'grep alpha')
        self.editor.send('\x1b')
    def test_yank_zero_survives_small_delete(self):
        self.open(b'alpha beta\nalpha xx\n')
        r=self.state('yex:grep \x120');self.assertEqual(r['command'],'grep alpha')
        self.assertEqual(self.state('\r')['grepCount'],1);self.editor.send('\x1b')
    def test_named_register_put_and_uppercase_append(self):
        path=self.open(b'abc def\n')
        self.editor.send('"ayll"AylG$"ap')
        self.assertTrue(self.call(2)['ok']);self.assertEqual(path.read_bytes(),b'abc defab\n')
    def test_named_yank_preserves_zero_and_append_updates_unnamed(self):
        path=self.open(b'abc def\n')
        self.assertEqual(self.state('yel"byl:grep \x120')['command'],'grep abc');self.editor.send('\x1b')
        self.editor.send('gg0"ayll"AylG$p')
        self.assertTrue(self.call(2)['ok']);self.assertEqual(path.read_bytes(),b'abc defab\n')
    def test_named_small_delete_updates_numbered_one(self):
        self.open(b'abc def\n')
        self.assertEqual(self.state('"ax:grep \x121')['command'],'grep a');self.editor.send('\x1b')
    def test_numbered_delete_register_and_black_hole(self):
        path=self.open(b'one\ntwo\nthree\n')
        self.editor.send('dddd"1P')
        self.assertTrue(self.call(2)['ok']);self.assertEqual(path.read_bytes(),b'two\nthree\n')
        self.editor.send('ggyy"_ddP')
        self.assertTrue(self.call(2)['ok']);self.assertEqual(path.read_bytes(),b'two\nthree\n')
    def test_insert_register_literal_and_undo(self):
        path=self.open(b'abc def\n')
        self.editor.send('v2lyA\x120\x1b')
        self.assertTrue(self.call(2)['ok']);self.assertEqual(path.read_bytes(),b'abc defabc\n')
        self.editor.send('u');self.assertTrue(self.call(2)['ok']);self.assertEqual(path.read_bytes(),b'abc def\n')
    def test_utf8_and_multiline_register_never_executes_command(self):
        self.open('中文\n其他\n中文\n'.encode())
        self.assertEqual(self.state('vly:grep \x120')['command'],'grep 中文')
        self.assertEqual(self.state('\r')['grepCount'],2);self.editor.send('\x1bggVj')
        r=self.state(':grep \x120');self.assertEqual(r['mode'],'C')
        self.assertEqual(r['command'],'grep 中文 其他 ');self.assertFalse(r['changed']);self.editor.send('\x1b')
    def test_register_capacity_and_cancel(self):
        self.open(b'a'*9000+b'\n')
        self.editor.send('yy:grep \x120')
        r=self.call(1);self.assertEqual(r['command'],'grep ');self.assertEqual(r['mode'],'C')
        r=self.state('\x12\x1bX');self.assertEqual(r['command'],'grep X');self.editor.send('\x1b')
    def test_named_read_and_special_search_register(self):
        self.open(b'alpha beta\nalpha\n')
        self.assertEqual(self.state('"ayee:grep \x12a')['command'],'grep alpha');self.editor.send('\x1b')
        self.editor.send('/beta\r');self.assertEqual(self.state(':grep \x12/')['command'],'grep beta');self.editor.send('\x1b')
    def test_grep_repeated_status_does_not_repaint_list(self):
        self.open(b'alpha\n'*80)
        self.editor.ex('grep alpha');self.call(1)
        start=len(self.editor.output)
        for unused in range(8):self.assertTrue(self.call(1)['grepActive'])
        self.assertLess(len(self.editor.output)-start,1024)
        self.assertIn(b'\x1b[?25l',self.editor.output)
        self.editor.send('\x1b');self.assertIn(b'\x1b[?25h',self.editor.output)

if __name__=='__main__':
    print('Register transcripts:',bridge.pty_tests.ARTIFACTS,flush=True)
    unittest.main(verbosity=2)
