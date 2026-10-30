#!/usr/bin/env python3
"""Real editor checks for Emacs navigation, native prefixes and modal grep."""
import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('bridge',Path(__file__).with_name('macos_gui_bridge_test.py'))
bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)

class GrepCase(unittest.TestCase):
    options=()
    setUp=bridge.BridgeTests.setUp
    tearDown=bridge.BridgeTests.tearDown
    packet=bridge.BridgeTests.packet
    receive=bridge.BridgeTests.receive
    call=bridge.BridgeTests.call
    wait_exit=bridge.BridgeTests.wait_exit
    def open(self,data,name='input.txt'):
        path=self.directory/name;path.write_bytes(data)
        self.editor.ex('e '+str(path));self.assertEqual(self.call(1)['file'],str(path))
        return path
    def state(self,keys=''):
        if keys:self.editor.send(keys)
        return self.call(1)
class GrepControlTests(GrepCase):
    def test_navigation_all_three_modes(self):
        path=self.open(b'abcdef\nUVWXYZ\n123456\n')
        for mode,enter in [('N',''),('I','i'),('V','v')]:
            for key,expected in [(1,7),(5,13 if mode=='I' else 12),(16,3),(14,17),(2,9),(6,11)]:
                with self.subTest(mode=mode,key=key):
                    self.editor.send('\x1bgg0j03l'+enter)
                    r=self.state(chr(key));self.assertEqual(r['mode'],mode)
                    self.assertEqual(r['fileOffset'],expected,r)
        self.assertEqual(path.read_bytes(),b'abcdef\nUVWXYZ\n123456\n')
    def test_insert_end_and_unicode_movement(self):
        path=self.open('中文abc\n'.encode())
        self.editor.send('i\x05!\x01>\x06X\x1b')
        self.assertTrue(self.call(2)['ok'])
        self.assertEqual(path.read_text(),'>中X文abc!\n')
    def test_character_controls_cross_lines_in_insert(self):
        path=self.open(b'abc\ndef\n')
        self.editor.send('j0i\x02X\x06Y\x1b')
        self.assertTrue(self.call(2)['ok'])
        self.assertEqual(path.read_bytes(),b'abcX\nYdef\n')

    def test_ctrl_k_ctrl_b_remains_mark_begin(self):
        self.editor.send('03l\x0b\x02')
        self.assertEqual(self.state()['fileOffset'],3)
        self.editor.send('2l\x0b\x0b')
        self.assertEqual(self.call(4)['text'],'de')
    def test_ctrl_k_ctrl_f_remains_find_prompt(self):
        self.editor.send('03l\x0b\x06')
        self.assertIn(b'Find',self.editor.output)
        self.editor.send('\x03')
        self.assertEqual(self.state()['fileOffset'],3)
    def test_command_input_is_cyan(self):
        before=len(self.editor.output);self.editor.send(':grep')
        output=bytes(self.editor.output[before:])
        self.assertTrue(b'36m' in output or b'96m' in output or b'0;255;255m' in output,output[-1500:])
        self.editor.send('\x1b')
    def test_grep_regex_preview_confirm_and_escape(self):
        data=b'origin\nWARN alpha\nquiet\nERROR beta\nWARN last\n'
        self.open(data);self.editor.send('gg02l')
        r=self.state(':grep WARN\r');self.assertTrue(r['grepActive']);self.assertEqual(r['grepCount'],2)
        self.assertEqual(r['fileOffset'],7)
        r=self.state('\x0e');self.assertEqual(r['grepSelected'],1);self.assertEqual(r['fileOffset'],35)
        r=self.state('\x10');self.assertEqual(r['grepSelected'],0)
        self.assertFalse(self.call(2)['ok']);self.assertFalse(self.call(14,'1;1;1')['ok'])
        r=self.state('\x1b[200~iDANGER:q\r\x1b[201~');self.assertTrue(r['grepActive']);self.assertEqual(r['mode'],'N');self.assertFalse(r['changed'])
        r=self.state('\x1b');self.assertFalse(r['grepActive']);self.assertEqual(r['fileOffset'],2)
        r=self.state(':grep ^ERROR\r');self.assertEqual(r['grepCount'],1);self.assertEqual(r['fileOffset'],24)
        r=self.state('\r');self.assertFalse(r['grepActive']);self.assertEqual(r['fileOffset'],24)
    def test_grep_scroll_and_resize(self):
        self.open(b'match line\n'*100)
        self.editor.ex('grep match')
        self.assertEqual(self.call(1)['grepCount'],100)
        self.editor.send('\x1b[6~');self.assertGreater(self.call(1)['grepSelected'],10)
        self.editor.send('G');self.assertEqual(self.call(1)['grepSelected'],99)
        self.assertIn(b'100: match line',self.editor.output)
        import fcntl,termios,struct
        fcntl.ioctl(self.editor.fd,termios.TIOCSWINSZ,struct.pack('HHHH',16,50,0,0))
        self.editor.send('\x10');self.assertEqual(self.call(1)['grepSelected'],98)
        self.assertTrue(self.call(15,'2')['ok']);self.assertEqual(self.call(1)['grepSelected'],96)
        self.editor.send('\r')
    def test_grep_no_match_invalid_regex_and_literal_preview(self):
        self.open(b'hello\x1b[31mworld\n')
        r=self.state(':grep missing\r');self.assertFalse(r['grepActive'])
        r=self.state(':grep [\r');self.assertFalse(r['grepActive'])
        r=self.state(':grep world\r');self.assertTrue(r['grepActive'])
        self.assertFalse(r['changed']);self.editor.send('\x1b')
    def test_grep_caps_results(self):
        self.open(b'match\n'*10010)
        r=self.state(':grep match\r');self.assertEqual(r['grepCount'],10000)
        self.assertIn(b'(limit)',self.editor.output);self.editor.send('\x1b')
    def test_lazy_grep_and_utf16(self):
        for encoding in ['utf-8','utf-16']:
            data=('plain line\n'*100000+'WARN 中文\n'+'plain line\n'*10000).encode(encoding)
            path=self.open(data,'large-'+encoding+'.txt')
            # Force a low threshold only for this test process via test-specific class below.
            r=self.call(1)
            if r['largePending']:self.call(17,'lazy')
            r=self.state(':grep WARN\r');self.assertEqual(r['grepCount'],1,r)
            expected=len(('plain line\n'*100000).encode(encoding)) if r['lazy'] else len(('plain line\n'*100000).encode())+(3 if encoding=='utf-16' else 0)
            self.assertEqual(r['fileOffset'],expected)
            if r['lazy']:self.assertLess(r['readBytes'],len(data)+5*1048576)
            self.editor.send('\x1b');self.assertFalse(self.call(1)['grepActive'])

class LazyGrepTests(GrepCase):
    options=('-large_threshold','1')
    test_large_streaming_grep_utf8_and_utf16=GrepControlTests.test_lazy_grep_and_utf16

if __name__=='__main__':
    print('Grep/control transcripts:',bridge.pty_tests.ARTIFACTS,flush=True)
    unittest.main(verbosity=2)
