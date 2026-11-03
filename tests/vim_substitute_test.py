#!/usr/bin/env python3
"""Actual editor Ex substitution checks, with system Vim as a result oracle."""
import shutil
import subprocess
import unittest
from grep_control_test import GrepCase, bridge

class SubstituteTests(GrepCase):
    options=("-large_threshold","1")
    def text(self):
        self.assertTrue(self.call(2)['ok'])
        return self.path.read_bytes()
    def source(self,data,name="input.txt"): self.path=self.open(data,name)
    def compare(self,data,command):
        self.source(data);self.editor.ex(command)
        oracle=self.directory/'oracle.txt';oracle.write_bytes(data)
        result=subprocess.run(['vim','-Nu','NONE','-i','NONE','-n','-es',str(oracle),
            '-c','normal! gg0','-c',command,'-c','wq'],capture_output=True,timeout=5)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(self.text(),oracle.read_bytes())
    def test_default_first_on_current_line(self):self.compare(b'foo foo\nfoo foo\n','s/foo/bar/')
    def test_global_all_lines(self):self.compare(b'foo foo\nfoo food\n','%s/foo/bar/g')
    def test_explicit_range(self):self.compare(b'foo\nfoo foo\nfoo\nfoo\n','2,3s/foo/bar/g')
    def test_end_address_and_offset(self):self.compare(b'foo\nfoo\nfoo\nfoo\n','.+1,$-1s/foo/bar/')
    def test_empty_replacement(self):self.compare(b'foo foo\n','s/foo//g')
    def test_ampersand_and_captures(self):self.compare(b'abc-123 xyz-4\n',r's/\([a-z]\+\)-\([0-9]\+\)/[\2:&:\1]/g')
    def test_adjacent_greedy_capture_groups(self):self.compare(b'abcdef\n',r's/\([a-z]\+\)\([a-z]\+\)/[\1][\2]/')
    def test_literal_parentheses_and_plus(self):self.compare(b'(a+b) (aab)\n',r's/(a+b)/ok/g')
    def test_escaped_delimiter_and_alternative(self):self.compare(b'/a/b /a/b\n',r's#/a/b#/x/y#g')
    def test_slash_delimiter_escape(self):self.compare(b'/a/b /a/b\n',r's/\/a\/b/\/x\/y/g')
    def test_literal_ampersand_and_backslash(self):self.compare(b'foo foo\n',r's/foo/\&\\/g')
    def test_case_insensitive(self):self.compare(b'Foo FOO foo\n','s/foo/x/gi')
    def test_force_case_sensitive(self):self.compare(b'Foo FOO foo\n','s/foo/x/gI')
    def test_inline_case_modifier(self):self.compare(b'Foo FOO foo\n',r's/\cfoo/x/g')
    def test_unicode(self):self.compare('中文 中文\n其他中文\n'.encode(),' %s/中文/替换/g')
    def test_anchors_and_empty_match(self):self.compare(b'abc\ndef\n','%s/^/>/g')
    def test_eol_empty_match(self):self.compare(b'abc\ndef\n','%s/$/!/g')
    def test_empty_match_progress(self):self.compare(b'baac\n',r's/a*/X/g')
    def test_insert_newline_no_revisit(self):self.compare(b'foo foo\nfoo\n',r'%s/foo/x\ry/g')
    def test_repeated_quantifier(self):self.compare(b'a aa aaa aaaa\n',r's/a\{2,3}/X/g')
    def test_word_boundaries(self):self.compare(b'cat cats bobcat cat\n',r's/\<cat\>/dog/g')
    def test_no_final_newline(self):
        self.source(b'foo foo');self.editor.ex('%s/foo/x/g');self.assertEqual(self.text(),b'x x')
    def test_empty_file(self):
        self.source(b'');self.editor.ex('s/^/hello/');self.assertEqual(self.text(),b'hello')
    def test_trailing_newline_not_an_extra_line(self):self.compare(b'a\n',r'%s/^/X/g')
    def test_undo_redo_entire_command(self):
        self.source(b'foo foo\nfoo\n');self.editor.ex('%s/foo/bar/g')
        self.editor.send('u');self.assertEqual(self.text(),b'foo foo\nfoo\n')
        self.editor.send('\x12');self.assertEqual(self.text(),b'bar bar\nbar\n')
    def test_visual_line_range(self):
        self.source(b'foo\nfoo\nfoo\nfoo\n');self.editor.send('jVj:s/foo/bar/g\r')
        self.assertEqual(self.text(),b'foo\nbar\nbar\nfoo\n')
    def test_confirm_yes_no_all(self):
        self.source(b'foo foo foo\nfoo\n');self.editor.ex('%s/foo/bar/gc')
        self.assertIn(b'Replace?',self.editor.output);self.assertFalse(self.call(2)['ok'])
        self.editor.send('yna');self.assertEqual(self.text(),b'bar foo bar\nbar\n')
        self.editor.send('u');self.assertEqual(self.text(),b'foo foo foo\nfoo\n')
    def test_confirm_last(self):
        self.source(b'foo foo\nfoo\n');self.editor.ex('%s/foo/bar/gc');self.editor.send('l')
        self.assertEqual(self.text(),b'bar foo\nfoo\n')
    def test_confirm_cancel_does_not_exit_or_edit(self):
        for key in ['q','\x1b','\x03']:
            self.source(b'foo foo\n', 'cancel-%d.txt'%ord(key));self.editor.ex('s/foo/bar/gc');self.editor.send(key)
            self.assertFalse(self.call(1)['changed']);self.assertEqual(self.text(),b'foo foo\n')
    def test_confirmation_blocks_background_native_actions(self):
        self.source(b'foo\n');self.editor.ex('s/foo/bar/c')
        for action,path in [(2,''),(3,str(self.directory/'bad.txt')),(14,'1;5;1'),(15,'1'),(11,'')]:
            self.assertFalse(self.call(action,path)['ok'])
        self.editor.send('n');self.assertEqual(self.text(),b'foo\n')
    def test_no_match_reports_without_changes(self):
        self.source(b'foo\n');self.editor.ex('s/missing/bar/')
        self.assertIn(b'Pattern not found',self.editor.output);self.assertFalse(self.call(1)['changed'])
    def test_bad_regex_or_flags_never_changes(self):
        self.source(b'foo\n')
        for cmd in ['%s/[foo/bar/g','%s/foo/bar/z','%s/foo','9,10s/foo/bar/',r's/foo/\=42/']:
            self.editor.ex(cmd);self.assertFalse(self.call(1)['changed'],cmd)
        self.assertEqual(self.text(),b'foo\n')
    def test_readonly(self):
        self.source(b'foo\n');self.editor.send('\x14O');self.editor.ex('s/foo/bar/')
        self.assertFalse(self.call(1)['changed']);self.editor.send('\x14O');self.assertEqual(self.text(),b'foo\n')
    def test_repeat_and_empty_search_pattern(self):
        self.source(b'foo foo\nfoo\n');self.editor.ex('s/foo/bar/');self.editor.send('j');self.editor.ex('s')
        self.assertEqual(self.text(),b'bar foo\nbar\n')
        self.editor.send('gg/foo\r');self.editor.ex('s//X/');self.assertEqual(self.text(),b'bar X\nbar\n')
    def test_unsupported_advanced_syntax_is_rejected(self):
        self.source(b'foo foo\n')
        for cmd in [r's/foo\|bar/X/g',r's/\(foo\)\1/X/g',r's/foo/\U&/g',r's/foo/\=42/',r's/\vfoo/X/']:
            self.editor.ex(cmd);self.assertFalse(self.call(1)['changed'],cmd)
        self.assertEqual(self.text(),b'foo foo\n')
    def test_hex_is_protected(self):
        self.source(b'foo\x00foo\n','binary.bin');self.assertTrue(self.call(1)['hex'])
        self.editor.ex('s/foo/bar/');self.assertFalse(self.call(1)['changed'])
        self.assertEqual(self.path.read_bytes(),b'foo\x00foo\n')
    def test_lazy_is_protected_without_reading_entire_source(self):
        self.source(b'foo\n'*600000,'lazy.log');self.assertTrue(self.call(17,'lazy')['ok'])
        before=self.call(1);self.assertTrue(before['lazy'])
        self.editor.ex('%s/foo/bar/g');after=self.call(1)
        self.assertFalse(after['changed']);self.assertTrue(after['readonly'])
        self.assertEqual(after['readBytes'],before['readBytes'])
        self.assertEqual(self.path.stat().st_size,2400000)
    def test_complex_regex_does_not_terminate_the_editor(self):
        self.source(b'abc\n');self.editor.ex(r's/a\{0,1000}/X/g')
        state=self.call(1)
        if state['changed']:self.assertEqual(self.text(),b'XbXc\n')
        else:self.assertIn(b'too complex',self.editor.output)
    def test_crlf(self):self.compare(b'foo foo\r\nfoo\r\n',r'%s/foo/x\ry/g')

if __name__=='__main__':unittest.main()
