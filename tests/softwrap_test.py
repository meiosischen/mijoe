#!/usr/bin/env python3
"""Display wrapping through real PTY/editor and native control-pipe operations."""
import fcntl
import struct
import termios
import unittest
from grep_control_test import GrepCase, bridge

class SoftWrapTests(GrepCase):
    def wrap(self,width=12):
        self.editor.send('\x14R'+str(width)+'\r')
        self.editor.ex('set wrap')
        s=self.call(1);self.assertTrue(s['softwrap']);self.assertEqual(s['wrapWidth'],width)
        return s
    def click(self,x,y):
        self.assertTrue(self.call(14,f'1;{x};{y}')['ok'])
        self.assertTrue(self.call(14,f'3;{x};{y}')['ok'])
        return self.call(1)
    def test_default_off_and_independent_menu_toggle(self):
        self.assertFalse(self.call(1)['softwrap'])
        self.editor.send('\x14Q');self.assertTrue(self.call(1)['softwrap'])
        self.editor.send('\x14Q');self.assertFalse(self.call(1)['softwrap'])
        self.assertFalse(self.call(1)['changed'])
    def test_existing_line_mouse_and_save_are_display_only(self):
        data=b'abcdefghijklmnopqrstuvwxXYZ\nnext\n'
        path=self.open(data);self.wrap()
        for x,y,offset in [(0,2,12),(4,3,27),(0,3,24),(0,4,28)]:
            with self.subTest(x=x,y=y):
                # Each click is separated spatially to avoid double click.
                self.assertEqual(self.click(x,y)['fileOffset'],offset)
        self.assertFalse(self.call(1)['changed'])
        self.assertTrue(self.call(2)['ok']);self.assertEqual(path.read_bytes(),data)
    def test_normal_cross_wrap_movement_and_logical_j_k(self):
        self.open(b'abcdefghijklmnopqrstuvwxXYZ\n123456\n');self.wrap()
        self.editor.send('gg09l3l');self.assertEqual(self.call(1)['fileOffset'],12)
        self.editor.send('j');self.assertEqual(self.call(1)['fileOffset'],33)
        self.editor.send('k');self.assertEqual(self.call(1)['fileOffset'],12)
    def test_margin_change_reflows_without_changing_file(self):
        path=self.open(b'abcdefghijklmnopqrstuvwxyz\n');self.wrap(12)
        self.assertEqual(self.click(0,2)['fileOffset'],12)
        self.editor.send('gg0\x14R8\r')
        self.assertEqual(self.click(1,3)['fileOffset'],17)
        self.editor.ex('set nowrap');self.assertFalse(self.call(1)['softwrap'])
        self.assertFalse(self.call(1)['changed']);self.assertEqual(path.read_bytes(),b'abcdefghijklmnopqrstuvwxyz\n')
    def test_wide_unicode_and_tab_boundaries(self):
        data='abcdefghijk中文Z\n123456789\tXYZ\n'.encode()
        self.open(data);self.wrap(12)
        self.assertEqual(self.click(0,2)['fileOffset'],11)
        self.assertEqual(self.click(2,2)['fileOffset'],14)
        self.assertFalse(self.call(1)['changed'])
    def test_paging_within_one_long_logical_line_and_back(self):
        data=(b'0123456789'*500)+b'\nend\n';path=self.open(data);self.wrap(12)
        self.editor.send('gg0\x1b[6~');s=self.call(1)
        self.assertGreater(s['fileOffset'],300);self.assertLess(s['fileOffset'],600)
        self.editor.send('\x1b[5~');self.assertEqual(self.call(1)['fileOffset'],0)
        self.assertFalse(self.call(1)['changed']);self.assertEqual(path.read_bytes(),data)
    def test_scroll_and_edits_rebuild_layout(self):
        path=self.open(b'abcdefghijklmnopqrstuvwxyz\nend\n');self.wrap(12)
        self.editor.send('gg0i!\x1b');self.assertTrue(self.call(1)['changed'])
        self.assertEqual(self.click(0,2)['fileOffset'],12)
        self.editor.send('gg0u');self.assertFalse(self.call(1)['changed'])
        self.assertEqual(self.click(1,3)['fileOffset'],25)
        self.call(2);self.assertEqual(path.read_bytes(),b'abcdefghijklmnopqrstuvwxyz\nend\n')
    def test_resize_uses_viewport_width(self):
        self.open(b'0123456789'*20+b'\n');self.wrap(100)
        fcntl.ioctl(self.editor.fd,termios.TIOCSWINSZ,struct.pack('HHHH',30,40,0,0))
        self.call(1);s=self.call(1);self.assertEqual(s['wrapWidth'],40)
        self.assertEqual(self.click(0,2)['fileOffset'],40)
    def test_visual_selection_across_wrapped_rows(self):
        self.open(b'abcdefghijklmnopqrstuvwxyz\n');self.wrap()
        self.editor.send('gg09lv5l')
        self.assertEqual(self.call(4)['text'],'jklmno')
        self.editor.send('y');self.assertFalse(self.call(1)['changed'])
    def test_large_wrapped_line_cache_remains_bounded_and_cursor_reachable(self):
        self.open(b'x'*200000+b'\nlast\n');self.wrap(12)
        self.editor.send('gg$');self.assertEqual(self.call(1)['fileOffset'],199999)
        self.editor.send('20h');self.assertEqual(self.call(1)['fileOffset'],199979)
        self.editor.send('gg0');self.assertEqual(self.call(1)['fileOffset'],0)
        self.assertFalse(self.call(1)['changed'])

class LazySoftWrapTests(GrepCase):
    options=('-large_threshold','1')
    def test_lazy_wrapping_preserves_readonly_bounded_window(self):
        path=self.directory/'lazy.txt'
        with path.open('wb') as output:
            for _ in range(200):output.write(b'x'*200000+b'\n')
        self.editor.ex('e '+str(path));s=self.call(1)
        self.assertTrue(s['largePending']);self.call(17,'lazy')
        self.editor.send('\x14R12\r:set wrap\r\x1b[6~')
        s=self.call(1)
        self.assertTrue(s['lazy']);self.assertTrue(s['readonly']);self.assertTrue(s['softwrap'])
        self.assertGreater(s['fileOffset'],100);self.assertLess(s['fileOffset'],1000)
        self.assertLess(s['loadedBytes'],16*1024*1024)
        self.assertFalse(self.call(2)['ok']);self.assertEqual(path.stat().st_size,200*200001)

if __name__=='__main__':unittest.main(verbosity=2)
