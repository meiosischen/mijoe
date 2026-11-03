#!/usr/bin/env python3
"""Status-path selection through the real GUI bridge and terminal mouse reports."""
import base64
import json
import os
from pathlib import Path
import re
import select
import time
import unittest
from grep_control_test import GrepCase, bridge

class StatusPathTests(GrepCase):
    options=('-mouse',)
    def setUp(self):
        super().setUp()
        self.events=[]
    def receive(self, identifier):
        deadline=time.monotonic()+5
        while time.monotonic()<deadline:
            while b'\n' in self.buffer:
                line,_,tail=self.buffer.partition(b'\n');self.buffer=bytearray(tail)
                response=json.loads(line)
                if 'clipboardText' in response:self.events.append(response['clipboardText'])
                if response['id']==identifier:return response
            ready,_,_=select.select([self.response,self.editor.fd],[],[],.05)
            if self.editor.fd in ready:self.editor.drain(.01)
            if self.response in ready:self.buffer.extend(os.read(self.response,65536))
        self.fail('Missing bridge response')
    def mouse(self,kind,x,y=0):
        return self.call(14,f'{kind};{x};{y}')
    def test_drag_both_directions_preserves_editor_modes_and_cursor(self):
        for enter,mode in [('', 'N'),('i','I'),('v','V')]:
            self.editor.send('\x1b02l'+enter)
            before=self.call(1);x=before['pathColumn']
            self.assertEqual(before['mode'],mode)
            self.mouse(1,x);self.mouse(2,x+6);self.mouse(3,x+6)
            after=self.call(1)
            self.assertEqual((after['mode'],after['fileOffset'],after['changed']),
                             (mode,before['fileOffset'],False))
            self.assertEqual(self.events[-1],before['fullPath'][:6])
            self.assertEqual(self.call(4)['text'],before['fullPath'][:6])
            # A new non-adjacent anchor avoids the native multi-click timer.
            self.mouse(1,x+12);self.mouse(2,x+8);self.mouse(3,x+8)
            self.assertEqual(self.events[-1],before['fullPath'][8:12])
    def test_native_double_click_copies_entire_clipped_path(self):
        name='very long '+('x'*100)+'.txt'
        path=self.open(b'body\n',name)
        s=self.call(1);self.assertGreater(len(str(path)),s['pathEndColumn']-s['pathColumn'])
        self.mouse(4,s['pathColumn']+3);self.mouse(3,s['pathColumn']+3)
        self.assertEqual(self.events[-1],str(path))
        self.assertEqual(self.call(4)['text'],str(path))
        self.assertEqual(path.read_bytes(),b'body\n')
    def test_unicode_control_and_backslash_names_are_copied_verbatim(self):
        for name in ['中文 空格.txt','quote\\b*name\n.txt']:
            path=self.directory/name
            self.assertTrue(self.call(3,str(path).replace('\\','\\\\'))['ok'])
            s=self.call(1)
            self.mouse(4,s['pathColumn']+1);self.mouse(3,s['pathColumn']+1)
            self.assertEqual(self.events[-1],str(path))
            self.assertTrue(path.exists())
            self.assertFalse(self.call(1)['changed'])
    def test_unicode_drag_never_splits_a_wide_character(self):
        import fcntl,termios,struct
        path=self.directory/'中文.txt';self.assertTrue(self.call(3,path)['ok'])
        fcntl.ioctl(self.editor.fd,termios.TIOCSWINSZ,struct.pack('HHHH',40,350,0,0))
        self.call(1);s=self.call(1)
        start=s['pathColumn']+len(str(path).rsplit('/',1)[0])+1
        self.assertLess(start+4,s['pathEndColumn'])
        self.mouse(1,start);self.mouse(2,start+2);self.mouse(3,start+2)
        self.assertEqual(self.events[-1],'中')
        self.mouse(1,start+4);self.mouse(2,start+2);self.mouse(3,start+2)
        self.assertEqual(self.events[-1],'文')
    def test_save_as_invalidates_previous_selection(self):
        s=self.call(1);self.mouse(4,s['pathColumn']);self.mouse(3,s['pathColumn'])
        target=self.directory/'new.txt';self.assertTrue(self.call(3,target)['ok'])
        self.assertFalse(self.call(4)['ok'])
        s=self.call(1);self.mouse(4,s['pathColumn']);self.mouse(3,s['pathColumn'])
        self.assertEqual(self.events[-1],str(target))
    def test_header_cut_does_not_delete_a_retained_visual_selection(self):
        self.editor.send('vll')
        s=self.call(1);self.mouse(4,s['pathColumn']);self.mouse(3,s['pathColumn'])
        self.assertFalse(self.call(5)['ok'])
        after=self.call(1)
        self.assertEqual((after['mode'],after['fileOffset'],after['changed']),('V',2,False))
        self.assertEqual(self.call(4)['text'],s['fullPath'])
    def test_body_click_clears_header_selection(self):
        s=self.call(1);self.mouse(4,s['pathColumn']);self.mouse(3,s['pathColumn'])
        self.mouse(1,2,1);self.mouse(3,2,1)
        self.assertFalse(self.call(4)['ok'])
        self.assertEqual(self.call(1)['fileOffset'],2)
    def test_new_keyboard_selection_replaces_header_copy_target(self):
        s=self.call(1);self.mouse(4,s['pathColumn']);self.mouse(3,s['pathColumn'])
        self.editor.send('vll')
        self.assertEqual(self.call(4)['text'],'abc')
        self.assertEqual(self.call(1)['mode'],'V')
    def test_sgr_terminal_mouse_preserves_insert_mode(self):
        self.editor.send('i')
        s=self.call(1);x=s['pathColumn']+1;y=s['pathRow']+1
        self.editor.send(f'\x1b[<0;{x};{y}M\x1b[<32;{x+8};{y}M\x1b[<0;{x+8};{y}m')
        s=self.call(1)
        self.assertEqual(self.events[-1],s['fullPath'][:8])
        self.assertEqual((s['mode'],s['fileOffset'],s['changed']),('I',0,False))
        self.editor.send('X\x1b');self.call(2)
        self.assertEqual(self.editor.path.read_bytes(),b'Xabcdef\n')
    def test_legacy_x10_terminal_mouse(self):
        s=self.call(1);x=s['pathColumn']+1;y=s['pathRow']+1
        for button,column in [(0,x),(32,x+8),(3,x+8)]:
            self.editor.send(b'\x1b[M'+bytes([32+button,32+column,32+y]))
        self.call(1)
        self.assertEqual(self.events[-1],s['fullPath'][:8])
        self.assertFalse(self.call(1)['changed'])

class ConsoleClipboardTests(unittest.TestCase):
    def test_unicode_path_osc52_without_gui_bridge(self):
        # No private pipe: the same selection must use the terminal clipboard.
        directory=bridge.pty_tests.ARTIFACTS/self.id().split('.')[-1]
        directory.mkdir()
        editor=bridge.pty_tests.Editor(directory,b'body\n',filename='中文.txt',options=('-mouse',))
        try:
            # Default status prefix is JOE plus [N]; locate path using an
            # independent bridge instance only in the tests above. Here a
            # double click scans the short prefix without assuming its width.
            for x in range(1,30):
                editor.send(f'\x1b[<0;{x};1M\x1b[<0;{x};1m')
                editor.send(f'\x1b[<0;{x};1M\x1b[<0;{x};1m')
                if b'\x1b]52;c;' in editor.output:break
            copies=re.findall(rb'\x1b\]52;c;([^\x1b]*)\x1b\\',editor.output)
            self.assertTrue(copies)
            self.assertEqual(base64.b64decode(copies[-1]).decode(),str(editor.path))
            self.assertEqual(editor.finish(),b'body\n')
        finally:editor.close()

if __name__=='__main__':unittest.main(verbosity=2)
