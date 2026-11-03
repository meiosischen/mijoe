#!/usr/bin/env python3
"""Temporary literal overlays belong to an open buffer, never its disk history."""
import ast
import unittest
from grep_control_test import GrepCase

class TemporaryHighlightTests(GrepCase):
    options=('-large_threshold','1')
    def count(self,command=None):
        if command is not None:self.editor.ex(command)
        return self.call(1)['temporaryHighlights']
    def test_line_numbers_mix_with_literals_and_deduplicate(self):
        self.assertEqual(self.count('set-hl :1 :005 ERROR'),3)
        self.assertEqual(self.count('set-hl :01 :5'),3)
        self.assertEqual(self.count('set-hl ":1"'),4)
        self.assertEqual(self.count('clear-hl'),0)
    def test_line_query_identifies_lines_and_quoted_literals(self):
        self.count('set-hl :01 ":1"');start=len(self.editor.output)
        self.count('set-hl');output=self.editor.output[start:]
        self.assertIn(b' :1',output);self.assertIn(b'":1"',output)
    def test_invalid_line_is_atomic(self):
        self.count('set-hl keep :1')
        for token in [':0',':-1',':+1',':2x',':999999999999999999999999']:
            self.assertEqual(self.count('set-hl :3 '+token),2,token)
    def test_lines_use_shared_limit(self):
        self.assertEqual(self.count('set-hl '+' '.join(':'+str(n+1) for n in range(32))),32)
        self.assertEqual(self.count('set-hl :33'),32)
        self.assertEqual(self.count('set-hl word'),32)
    def test_line_commands_do_not_change_file_or_undo(self):
        self.editor.send('iX\x1b');self.count('set-hl :1 :9999999999');self.editor.send('u')
        self.assertFalse(self.call(1)['changed']);self.assertEqual(self.count(),2)
        self.assertTrue(self.call(2)['ok']);self.assertEqual(self.editor.path.read_bytes(),b'abcdef\n')
    def test_line_only_works_with_single_byte_map(self):
        self.editor.send('\x14Ec\r');self.assertEqual(self.count('set-hl :1'),1)
        self.assertEqual(self.count('set-hl ERROR :2'),3)
    def test_quoted_phrase_and_separate_words(self):
        self.assertEqual(self.count('set-hl "gogogo 123"'),1)
        self.assertEqual(self.count('clear-hl'),0)
        self.assertEqual(self.count('set-hl gogogo 123'),2)
    def test_append_deduplicate_and_mixed_quotes(self):
        self.assertEqual(self.count('set-hl one "two three" four'),3)
        self.assertEqual(self.count('set-hl one four'),3)
        self.assertEqual(self.count('set-hl "two three" five'),4)
    def test_literal_quotes_backslashes_and_regex_punctuation(self):
        self.assertEqual(self.count(r'set-hl "a\"b" "c\\d" a.*[x]|(y)$'),3)
        self.assertFalse(self.call(1)['changed'])
    def test_query_is_readonly(self):
        self.assertEqual(self.count('set-hl'),0)
        self.count('set-hl "gogogo 123"');self.assertEqual(self.count('set-hl'),1)
        self.assertFalse(self.call(1)['changed'])
    def test_bad_arguments_leave_previous_set_intact(self):
        self.count('set-hl keep')
        for cmd in ['set-hl valid "unterminated','set-hl ""','set-hl "a" ""','set-hl! bad','clear-hl bad', 'set-hl '+('x'*1025)]:
            self.assertEqual(self.count(cmd),1,cmd[:70])
            self.assertFalse(self.call(1)['changed'])
    def test_term_and_total_byte_limits_are_atomic(self):
        self.assertEqual(self.count('set-hl '+' '.join('term%02d'%n for n in range(32))),32)
        self.assertEqual(self.count('set-hl term32'),32)
        self.count('clear-hl')
        for n in range(8):self.assertEqual(self.count('set-hl '+chr(97+n)*1024),n+1)
        self.assertEqual(self.count('set-hl extra'),8)
    def test_unicode_literals(self):
        self.assertEqual(self.count('set-hl "中文 日志" 词语 😀'),3)
        self.assertEqual(self.count('set-hl "中文 日志"'),3)
    def test_unrepresentable_unicode_is_rejected_atomically(self):
        self.count('set-hl keep');self.editor.send('\x14Ec\r')
        self.assertEqual(self.count('set-hl A中'),1)
        self.assertEqual(self.count('set-hl "A?"'),2)
        self.assertFalse(self.call(1)['changed'])
    def test_representable_single_byte_literals(self):
        self.open(b'caf\xe9?\n','latin.txt');self.editor.send('\x14Eiso-8859-1\r')
        self.assertEqual(self.count('set-hl café'),1)
    def test_switch_preserves_own_file_but_never_leaks_to_another(self):
        original=self.editor.path;self.count('set-hl unique-token')
        self.open(b'other\n','other.txt');self.assertEqual(self.count(),0)
        self.editor.ex('e '+str(original));self.assertEqual(self.count(),1)
        self.editor.ex('e '+str(self.directory/'other.txt'));self.assertEqual(self.count(),0)
    def test_closed_buffer_reopened_from_recent_has_no_terms(self):
        original=self.editor.path;self.count('set-hl unique-temporary-phrase')
        self.editor.ex('bd');self.assertEqual(self.count(),0)
        self.editor.ex('recent');self.editor.send('\r')
        self.assertEqual(self.call(1)['file'],str(original));self.assertEqual(self.count(),0)
        history=(self.directory/'.home/.mj/.recent').read_text()
        self.assertIn(str(original),history);self.assertNotIn('unique-temporary-phrase',history)
    def test_readonly_allows_highlight_commands(self):
        self.editor.send('\x14O');before=self.call(1);self.assertTrue(before['readonly'])
        self.assertEqual(self.count('set-hl abc'),1);self.assertEqual(self.count('clear-hl'),0)
        self.assertTrue(self.call(1)['readonly']);self.assertFalse(self.call(1)['changed'])
    def test_commands_do_not_touch_undo_or_file_bytes(self):
        self.editor.send('iX\x1b');self.count('set-hl abc');self.editor.send('u')
        self.assertFalse(self.call(1)['changed']);self.assertEqual(self.count(),1)
        self.assertTrue(self.call(2)['ok']);self.assertEqual(self.editor.path.read_bytes(),b'abcdef\n')
    def test_nohlsearch_does_not_clear_temporary_terms(self):
        self.count('set-hl abc');self.editor.send('/def\r');self.editor.ex('noh')
        self.assertEqual(self.count(),1);self.count('clear-hl');self.assertFalse(self.call(1)['changed'])
    def test_saveas_keeps_open_buffer_terms(self):
        self.count('set-hl abc');new=self.directory/'renamed.txt'
        self.assertTrue(self.call(3,new)['ok']);self.assertEqual(self.count(),1)
        self.editor.ex('bd');self.editor.ex('e '+str(new));self.assertEqual(self.count(),0)
    def test_hex_rejects_adding_terms(self):
        self.open(b'abc\x00def','binary.bin');self.assertTrue(self.call(1)['hex'])
        self.assertEqual(self.count('set-hl abc'),0);self.assertFalse(self.call(1)['changed'])
    def test_lazy_terms_survive_region_reload_without_full_file_scan(self):
        path=self.open(b'ERROR payload\n'*300000,'lazy.log');self.call(17,'lazy')
        before=self.call(1);self.assertTrue(before['lazy'])
        self.assertEqual(self.count('set-hl ERROR'),1)
        self.assertEqual(self.call(1)['readBytes'],before['readBytes'])
        self.editor.send('G');last=self.call(1);self.assertEqual(last['temporaryHighlights'],1)
        self.assertGreater(last['fileOffset'],before['fileOffset'])
        self.assertLess(last['loadedBytes'],last['fileBytes'])
        self.editor.ex('bd')
        entries=[ast.literal_eval(line.split('\t',1)[1]) for line in (self.directory/'.home/.mj/.recent').read_text().splitlines() if line and line[0].isdigit()]
        self.editor.ex('recent');self.editor.send('j'*entries.index(str(path))+'\r')
        restored=self.call(1);self.assertEqual(restored['file'],str(path));self.assertTrue(restored['lazy'])
        self.assertEqual(restored['temporaryHighlights'],0)

if __name__=='__main__':unittest.main()
