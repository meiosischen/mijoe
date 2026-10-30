#!/usr/bin/env python3
"""Real editor register inspection and F5/F6 recording/playback checks."""
import importlib.util
from pathlib import Path
import unittest
import re
spec=importlib.util.spec_from_file_location('bridge',Path(__file__).with_name('macos_gui_bridge_test.py'))
bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
F5='\x1b[15~';F6='\x1b[17~'
class MacroTests(unittest.TestCase):
    options=()
    setUp=bridge.BridgeTests.setUp
    tearDown=bridge.BridgeTests.tearDown
    packet=bridge.BridgeTests.packet
    receive=bridge.BridgeTests.receive
    call=bridge.BridgeTests.call
    wait_exit=bridge.BridgeTests.wait_exit
    def state(self,keys=''):
        if keys:self.editor.send(keys)
        return self.call(1)
    def plain(self):
        data=re.sub(rb'\x1b\[[0-?]*[ -/]*[@-~]',b'',bytes(self.editor.output))
        while b'\x08' in data:data=re.sub(rb'.?\x08',b'',data)
        return data
    def test_register_quote_echo_and_selection_echo(self):
        start=len(self.editor.output);self.editor.send('"')
        self.assertIn(b'register?',self.editor.output[start:])
        start=len(self.editor.output);self.editor.send('a')
        self.assertIn(b'"a [register selected',self.editor.output[start:])
        start=len(self.editor.output);self.editor.send('y')
        self.assertIn(b'"ay',self.editor.output[start:]);self.editor.send('\x1b')
    def test_registers_view_is_paged_and_readonly(self):
        self.editor.send('"ayl:reg a\r')
        self.assertIn(b'MiJoe registers',self.editor.output);self.assertIn(b'"a  char  1  a',self.plain())
        self.editor.send('\x1b[6~\x1b[5~q')
        self.assertFalse(self.call(1)['changed'])
        self.editor.ex('reg 0');self.assertIn(b'[empty]',self.plain());self.editor.send('q')
    def test_record_playback_and_view(self):
        self.assertTrue(self.state(F5)['macroRecording'])
        self.editor.send('iX\x1bl')
        r=self.state(F5);self.assertFalse(r['macroRecording']);self.assertEqual(r['macroSteps'],4)
        self.assertIn(b'REC',self.editor.output)
        self.editor.send(F6);r=self.call(1);self.assertFalse(r['macroPlaying'])
        self.assertTrue(self.call(2)['ok']);self.assertEqual(self.editor.path.read_bytes(),b'XXabcdef\n')
        self.editor.ex('macro');self.assertIn(b'MiJoe macro',self.editor.output);self.assertIn(b'<Esc>',self.plain())
        self.editor.send('q');self.assertEqual(self.call(1)['macroSteps'],4)
    def test_multiple_playbacks_do_not_grow_recording(self):
        self.editor.send(F5+'iX\x1b'+F5)
        self.editor.send(F6);self.editor.send(F6)
        self.assertEqual(self.call(1)['macroSteps'],3)
        self.assertTrue(self.call(2)['ok']);self.assertEqual(self.editor.path.read_bytes(),b'XXXabcdef\n')
    def test_insert_mode_recording_requires_same_mode(self):
        self.editor.send('i'+F5+'中文'+F5)
        self.assertEqual(self.call(1)['macroSteps'],2)
        self.editor.send(F6+'\x1b');self.assertTrue(self.call(2)['ok'])
        self.assertEqual(self.editor.path.read_bytes(),'中文中文abcdef\n'.encode())
        r=self.state(F6);self.assertFalse(r['macroPlaying']);self.assertIn(b'mode in which recording started',self.editor.output)
    def test_arrow_keys_and_bracketed_paste_replay(self):
        self.editor.send(F5+'i\x1b[200~中文\nZ\x1b[201~\x1b'+F5)
        before=self.call(1)['macroSteps'];self.assertGreater(before,5)
        self.editor.send(F6);self.assertEqual(self.call(1)['macroSteps'],before)
        self.assertTrue(self.call(2)['ok']);self.assertEqual(self.editor.path.read_bytes(),'中文\n中文\nZZabcdef\n'.encode())
    def test_decoded_arrow_keys_replay_without_reading_live_terminal(self):
        self.editor.send(F5+'\x1b[CiX\x1b'+F5)
        self.editor.send(F6)
        self.assertTrue(self.call(2)['ok']);self.assertEqual(self.editor.path.read_bytes(),b'aXXbcdef\n')
    def test_native_ctrl_k_sequences_replay(self):
        self.editor.send(F5+'\x0b\x02l\x0b\x0b\x0b\x03'+F5)
        self.editor.send('gg0');self.editor.send(F6)
        self.assertTrue(self.call(2)['ok']);self.assertEqual(self.editor.path.read_bytes(),b'aaabcdef\n')
    def test_ex_commands_record_and_replay(self):
        self.editor.send(F5+':2\riX\x1b'+F5)
        self.editor.send(F6);self.assertFalse(self.call(1)['macroPlaying'])
        self.assertTrue(self.call(2)['ok']);self.assertEqual(self.editor.path.read_bytes(),b'abcdef\nXX')
    def test_play_during_recording_and_empty_recording(self):
        self.assertFalse(self.state(F6)['macroPlaying'])
        self.editor.send(F5+'iX\x1b'+F6)
        self.assertTrue(self.call(1)['macroRecording']);self.assertEqual(self.call(1)['macroSteps'],3)
        self.editor.send(F5+F5+F5)
        self.assertEqual(self.call(1)['macroSteps'],3)
    def test_ctrl_c_interrupt_and_readonly_abort(self):
        self.editor.send(F5+'iX\x1bl'+F5)
        self.editor.send(F6+'\x03')
        self.assertFalse(self.call(1)['macroPlaying'])
        self.editor.send('\x14O');self.assertTrue(self.call(1)['readonly'])
        self.editor.send(F6);self.assertFalse(self.call(1)['macroPlaying']);self.assertEqual(self.call(1)['mode'],'N')
    def test_register_text_with_control_characters_is_not_executed(self):
        self.editor.send('yy:registers\r')
        self.assertIn(b'<NL>',self.plain());self.editor.send('q')
        self.assertFalse(self.call(1)['changed'])
if __name__=='__main__':
    print('Macro transcripts:',bridge.pty_tests.ARTIFACTS,flush=True)
    unittest.main(verbosity=2)
