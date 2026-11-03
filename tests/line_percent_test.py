#!/usr/bin/env python3
"""Line-based status progress: actual editor, no file-wide lazy scan."""
import unittest
from grep_control_test import GrepCase

class LinePercentTests(GrepCase):
    options=('-large_threshold','1')
    def percent(self,keys=''):
        if keys:self.editor.send(keys)
        return self.call(1)['linePercent']
    def test_unequal_line_lengths_use_lines_not_bytes(self):
        self.open(b'x'*2000+b'\na\nb\nc\n')
        self.assertEqual(self.percent(),25)
        self.assertEqual(self.percent('j'),50)
        self.assertEqual(self.percent('j'),75)
        self.assertEqual(self.percent('G'),100)
    def test_no_trailing_newline(self):
        self.open(b'a\nb\nc');self.assertEqual(self.percent(),33)
        self.assertEqual(self.percent('j'),66);self.assertEqual(self.percent('G'),100)
    def test_empty_and_single_line(self):
        for n,data in enumerate([b'',b'abc',b'abc\n',b'\n']):
            self.open(data,'one-%d.txt'%n);self.assertEqual(self.percent(),100)
    def test_blank_lines_are_real_lines(self):
        self.open(b'\n\n\n\n');self.assertEqual(self.percent(),25)
        self.assertEqual(self.percent('2j'),75)
    def test_visual_insert_and_edits_refresh_total(self):
        self.open(b'a\nb\n');self.assertEqual(self.percent('v'),50)
        self.assertEqual(self.percent('\x1bi\r'),66)
        self.assertEqual(self.percent('\x1bu'),50)
        self.assertEqual(self.percent('dd'),100)
    def test_insert_eof_virtual_line_is_clamped(self):
        self.open(b'a\nb\n');self.assertEqual(self.percent('GA\r'),100)
    def test_softwrap_does_not_count_display_rows(self):
        self.open(b'x'*2000+b'\na\nb\nc\n')
        self.editor.send('\x14R12\r:set wrap\r')
        self.assertEqual(self.percent(),25)
        self.assertEqual(self.percent('\x1b[6~'),25)
    def test_hex_counts_sixteen_byte_rows(self):
        self.open(b'\x00'*33,'binary.bin');self.assertEqual(self.percent(),33)
        self.assertEqual(self.percent('\x1b[B'),66)
        self.assertEqual(self.percent('\x1b[B'),100)
    def test_lazy_hex_uses_known_size_without_line_scanning(self):
        self.open(b'\x00'*2400000,'lazy.bin');self.call(17,'lazy')
        before=self.call(1);self.assertTrue(before['hex']);self.assertEqual(before['linePercent'],0)
        self.assertEqual(self.percent(),0);self.assertEqual(self.call(1)['readBytes'],before['readBytes'])
        self.assertEqual(self.percent('G'),100)
        last=self.call(1);self.assertEqual(self.percent(),100)
        self.assertEqual(self.call(1)['readBytes'],last['readBytes'])
    def test_lazy_unknown_total_does_not_scan_for_percentage(self):
        self.open(b'foo\n'*600000,'lazy.log');self.call(17,'lazy')
        before=self.call(1);self.assertEqual(before['linePercent'],-1)
        for _ in range(5):self.assertEqual(self.percent(),-1)
        after=self.call(1);self.assertEqual(after['readBytes'],before['readBytes'])
        self.assertLess(after['loadedBytes'],after['fileBytes'])
class SmallLazyLinePercentTests(GrepCase):
    options=('-lazy',)
    def test_small_lazy_source_can_use_known_total(self):
        self.call(17,'lazy')
        self.open(b'a\nb\nc\n','small-lazy.txt');self.call(17,'lazy')
        self.assertEqual(self.call(1)['linePercent'],33)
        self.editor.send('j');self.assertEqual(self.call(1)['linePercent'],66)
        self.editor.send('G');self.assertEqual(self.call(1)['linePercent'],100)

if __name__=='__main__':unittest.main()
