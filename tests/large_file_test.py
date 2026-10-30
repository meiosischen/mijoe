#!/usr/bin/env python3
"""Real-editor large-file paging, safety and 64-bit-offset regressions."""
import importlib.util
import sys
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('bridge',Path(__file__).with_name('macos_gui_bridge_test.py'))
bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)

class LargeFileCase(unittest.TestCase):
    options=('-large_threshold','1')
    setUp=bridge.BridgeTests.setUp
    tearDown=bridge.BridgeTests.tearDown
    packet=bridge.BridgeTests.packet
    receive=bridge.BridgeTests.receive
    call=bridge.BridgeTests.call
    wait_exit=bridge.BridgeTests.wait_exit

    def open(self, data, name='large.txt', lazy=True):
        path=self.directory/name;path.write_bytes(data)
        self.editor.ex('e '+str(path));result=self.call(1)
        self.assertTrue(result['ok'],result)
        if len(data)>1048576:
            self.assertTrue(result['largePending'],result)
            self.assertLessEqual(result['readBytes'],65536)
            if lazy:
                result=self.call(17,'lazy')
                self.assertTrue(result['ok'],result);self.assertTrue(result['lazy'])
                self.assertTrue(result['readonly']);self.assertLessEqual(result['loadedBytes'],1048576*2)
        return path,result

    def state(self, keys=''):
        if keys:self.editor.send(keys)
        return self.call(1)

class LargeFileTests(LargeFileCase):
    def test_paging_text_and_line_jump(self):
        data=b'line abcdefghijklmnopqrstuvwxyz\n'*100000
        path,r=self.open(data)
        self.assertEqual(r['fileOffset'],0)
        r=self.state('G');self.assertGreater(r['fileOffset'],len(data)-100)
        self.assertLess(r['readBytes'],3*1048576)
        r=self.state('gg');self.assertEqual(r['fileOffset'],0)
        r=self.state('50%');self.assertLess(abs(r['fileOffset']-len(data)//2),64)
        r=self.state(':12345\r');self.assertEqual(r['fileOffset'],12344*32)
        r=self.state('gg90000j');self.assertEqual(r['fileOffset'],90000*32)
        self.assertLess(r['readBytes'],15*1048576)
        r=self.state('90000k');self.assertEqual(r['fileOffset'],0)

    def test_cancel_preserves_dirty_previous(self):
        self.editor.send('iX\x1b')
        path,r=self.open(b'abc\n'*300000,lazy=False)
        r=self.call(17,'cancel');self.assertTrue(r['ok'],r)
        self.assertEqual(r['file'],str(self.editor.path));self.assertTrue(r['changed'])
        self.assertEqual(self.editor.finish(),b'Xabcdef\n')

    def test_normal_choice_restores_editing(self):
        path,r=self.open(b'abcd\n'*250000,lazy=False)
        r=self.call(17,'normal');self.assertTrue(r['ok'],r)
        self.assertFalse(r['lazy']);self.assertFalse(r['readonly'])
        self.editor.send('iX\x1b');self.assertTrue(self.call(2)['ok'])
        self.assertTrue(path.read_bytes().startswith(b'Xabcd'))

    def test_readonly_blocks_edit_save_and_saveas(self):
        data=b'abcd\n'*250000;path,r=self.open(data)
        self.editor.send('iX\x1bddp\x14O')
        self.assertTrue(self.state()['readonly'])
        self.assertFalse(self.call(2)['ok'])
        self.assertFalse(self.call(13)['ok'])
        target=self.directory/'forbidden.txt'
        self.assertFalse(self.call(3,target)['ok']);self.assertFalse(target.exists())
        self.assertEqual(path.read_bytes(),data)

    def test_small_binary_auto_hex(self):
        path,r=self.open(b'\x00\x01hello\xff',name='small.dat')
        self.assertTrue(r['binary']);self.assertFalse(r['lazy'])
        self.assertIn(b'01 68 65 6c 6c 6f ff',self.editor.output)

    def test_utf16_text_detection_and_lazy_mapping(self):
        text='普通行 hello 😀\r\n'*70000
        data=text.encode('utf-16');path,r=self.open(data)
        self.assertFalse(r['binary']);self.assertTrue(r['lazy'])
        self.assertIn('普通行'.encode(),self.editor.output)
        r=self.state(':101\r');self.assertEqual(r['fileOffset'],2+100*len('普通行 hello 😀\r\n'.encode('utf-16-le')))
        r=self.state('G');self.assertGreater(r['fileOffset'],len(data)-100)

    def test_source_change_fails_without_saving(self):
        path,r=self.open(b'abc\n'*600000)
        with path.open('ab') as f:f.write(b'changed\n')
        self.state('G')
        self.assertIn(b'changed on disk',self.editor.output)
        self.assertFalse(self.call(2)['ok'])

    def test_literal_search_across_windows_and_wrap(self):
        data=bytearray(b'a'*2500000)
        for offset in (65534,1048574,2200000):data[offset:offset+6]=b'needle'
        path,r=self.open(data)
        self.assertEqual(self.state('/needle\r')['fileOffset'],65534)
        self.assertEqual(self.state('n')['fileOffset'],1048574)
        self.assertEqual(self.state('n')['fileOffset'],2200000)
        self.assertEqual(self.state('n')['fileOffset'],65534)
        self.assertEqual(self.state('N')['fileOffset'],2200000)
        self.assertEqual(self.state('2n')['fileOffset'],1048574)

    def test_terminal_prompt_keys(self):
        path,r=self.open(b'abc\n'*300000,lazy=False)
        self.assertTrue(self.state('l')['lazy'])
        self.editor.ex('e '+str(path));self.assertTrue(self.call(1)['largePending'])
        self.editor.send('n');self.assertFalse(self.call(1)['lazy']);self.assertFalse(self.call(1)['readonly'])
        self.editor.ex('e '+str(self.editor.path))
        path,r=self.open(b'def\n'*300000,name='second.txt',lazy=False)
        self.editor.send('\x1b');self.assertEqual(self.call(1)['file'],str(self.editor.path))

    def test_utf16_search_and_whole_word(self):
        text='token tokenizer token\n'+('filler line\n'*100000)+'token end\n'
        data=text.encode('utf-16');path,r=self.open(data)
        self.assertEqual(self.state('*')['fileOffset'],2+16*2)
        self.assertEqual(self.state('n')['fileOffset'],2+len(text[:text.rfind('token')].encode('utf-16-le')))
        self.assertEqual(self.state('n')['fileOffset'],2)
        self.assertEqual(self.state('N')['fileOffset'],2+len(text[:text.rfind('token')].encode('utf-16-le')))

    def test_startup_readonly_survives_normal_choice(self):
        directory=self.directory/'readonly-startup';directory.mkdir()
        data=b'abcd\n'*250000
        editor=bridge.pty_tests.Editor(directory,data,options=('-rdonly','-large_threshold','1'))
        try:
            editor.send('n');editor.send('iX\x1b')
            self.assertEqual(editor.finish(),data)
            self.assertIn(b'Read only',editor.output)
        finally:editor.close()

    def test_explicit_hex_survives_lazy_choice_for_text(self):
        directory=self.directory/'hex-startup';directory.mkdir()
        editor=bridge.pty_tests.Editor(directory,b'abcd\n'*250000,options=('-hex','-large_threshold','1'))
        try:
            editor.send('l')
            self.assertIn(b'62 63 64 0a',editor.output)
            editor.finish(save=False)
        finally:editor.close()

    def test_sparse_over_4gb_binary(self):
        size=5*1024**3+17;path=self.directory/'sparse.bin'
        with path.open('wb') as f:
            f.truncate(size);f.seek(size-18);f.write(b'last-binary-byte!\n')
        self.editor.ex('e '+str(path));r=self.call(1);self.assertTrue(r['largePending']);self.assertTrue(r['binary'])
        self.assertEqual(r['fileBytes'],size);self.assertLessEqual(r['readBytes'],65536)
        r=self.call(17,'lazy');self.assertTrue(r['lazy'])
        r=self.state('G');self.assertEqual(r['fileOffset'],size-1)
        self.assertLess(r['readBytes'],3*1048576)
        self.assertIn(b'140000010',self.editor.output)
        self.assertEqual(self.state('gg')['fileOffset'],0)
        self.assertEqual(self.state('50%')['fileOffset'],size//2)

class DefaultThresholdTests(LargeFileCase):
    options=()
    def test_real_513mib_text_uses_default_prompt_and_bounded_reads(self):
        path=self.directory/'513MiB.txt'
        block=b'plain text line\n'*4096
        with path.open('wb') as f:
            for unused in range(513*16):f.write(block)
        size=path.stat().st_size
        self.assertEqual(size,513*1024**2)
        self.editor.ex('e '+str(path));r=self.call(1)
        self.assertTrue(r['largePending']);self.assertFalse(r['binary'])
        self.assertEqual(r['readBytes'],65536);self.assertLess(r['loadedBytes'],1024)
        r=self.call(17,'lazy');self.assertTrue(r['lazy']);self.assertLess(r['loadedBytes'],1048608)
        r=self.state('G');self.assertGreater(r['fileOffset'],size-32)
        self.assertLess(r['readBytes'],3*1048576)
        path.unlink()

class ForcedLazyTests(LargeFileCase):
    options=('-lazy',)
    def test_force_lazy_small_file_and_save_guard(self):
        r=self.call(1);self.assertTrue(r['lazy']);self.assertTrue(r['readonly'])
        self.assertFalse(r['largePending']);self.assertEqual(r['fileBytes'],7)
        self.assertFalse(self.call(2)['ok'])

if __name__=='__main__':
    print('Large-file transcripts:',bridge.pty_tests.ARTIFACTS,flush=True)
    unittest.main(verbosity=2)
