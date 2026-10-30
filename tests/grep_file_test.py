#!/usr/bin/env python3
"""Exercise grep -f export/import and console buffer management."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('grep_controls',Path(__file__).with_name('grep_control_test.py'))
grep=importlib.util.module_from_spec(spec);spec.loader.exec_module(grep)


class GrepFileTests(grep.GrepCase):
    def export(self,expression='hit'):
        if hasattr(self,'transfer'):self.transfer.unlink(missing_ok=True)
        self.editor.ex('grep -f '+expression)
        for _ in range(30):
            message=self.receive(0)
            if 'openTextFile' in message:
                self.transfer=Path(message['openTextFile'])
                return message,self.transfer.read_bytes()
        self.fail('No generated-window event')

    def tearDown(self):
        if hasattr(self,'transfer'):self.transfer.unlink(missing_ok=True)
        super().tearDown()

    def test_full_lines_export_and_unnamed_editable_import(self):
        original='origin\nhit '+('中文'*1200)+' END\nquiet\nhit last\n'
        source=self.open(original.encode());self.editor.send('gg02l')
        event,data=self.export();expected=('2: hit '+('中文'*1200)+' END\n4: hit last\n').encode()
        self.assertEqual(data,expected);self.assertEqual(event['fileOffset'],2);self.assertFalse(event['changed']);self.assertFalse(event['grepActive'])
        self.assertTrue(self.call(18,str(self.transfer))['ok'])
        r=self.call(1);self.assertEqual(r['file'],'');self.assertTrue(r['changed']);self.assertFalse(r['readonly'])
        self.assertFalse(self.call(8)['ok'])
        self.editor.send('iEDIT \x1b')
        saved=self.directory/'result.txt';self.assertTrue(self.call(3,str(saved))['ok'])
        self.assertEqual(saved.read_bytes(),b'EDIT '+expected)
        self.editor.ex('e '+str(source));self.assertEqual(self.call(1)['fileOffset'],2)
        self.assertEqual(source.read_text(),original)

    def test_quotes_flag_termination_and_empty_invalid_queries(self):
        source=self.open(b'hit one\nhit two\n-f literal\n')
        _,data=self.export('"hit one"');self.assertEqual(data,b'1: hit one\n')
        _,data=self.export('-- -f');self.assertEqual(data,b'3: -f literal\n')
        self.editor.send('gg02l');before=self.call(1)
        for command in ['grep -f missing','grep -f [','grep -f','grep -f "unfinished']:
            self.editor.ex(command);r=self.call(1)
            self.assertEqual(r['file'],str(source));self.assertEqual(r['fileOffset'],before['fileOffset']);self.assertFalse(r['grepActive']);self.assertFalse(r['changed'])

    def test_crlf_and_no_final_newline(self):
        self.open(b'hit first\r\nquiet\r\nhit last')
        _,data=self.export();self.assertEqual(data,b'1: hit first\n3: hit last\n')

    def test_export_preserves_unsaved_source_edits_and_cursor(self):
        source=self.open(b'hit alpha\nquiet\n');self.editor.send('A!\x1b')
        before=self.call(1);self.assertTrue(before['changed'])
        event,data=self.export();self.assertEqual(data,b'1: hit alpha!\n')
        self.assertTrue(event['changed']);self.assertEqual(event['fileOffset'],before['fileOffset'])
        self.assertEqual(source.read_bytes(),b'hit alpha\nquiet\n')

    def test_import_failure_preserves_original(self):
        before=self.call(1)
        self.assertFalse(self.call(18,str(self.directory/'missing.txt'))['ok'])
        oversized=self.directory/'oversized.txt'
        with oversized.open('wb') as output:output.truncate(64*1024*1024+1)
        self.assertFalse(self.call(18,str(oversized))['ok'])
        r=self.call(1);self.assertEqual(r['file'],before['file']);self.assertEqual(r['changed'],before['changed'])

    def test_result_count_limit_is_explicit_in_plain_text(self):
        self.open(b'hit\n'*10001)
        _,data=self.export()
        self.assertTrue(data.startswith(b'1: hit\n'));self.assertIn(b'10000: hit\n',data)
        self.assertNotIn(b'10001: hit',data)
        self.assertTrue(data.endswith(b'[grep: results limited to 10000 matching lines]\n'))


class LazyGrepFileTests(GrepFileTests):
    options=('-large_threshold','1')
    def id(self):
        return super().id()+"_lazy"
    def test_utf16_surrogate_at_export_chunk_boundary(self):
        line='hit '+('x'*32762)+'😀END'
        original=(line+'\n'+'quiet\n'*180000).encode('utf-16')
        source=self.open(original,'lazy.txt');self.assertTrue(self.call(1)['largePending'])
        self.assertTrue(self.call(17,'lazy')['ok'])
        before=self.call(1);event,data=self.export()
        self.assertEqual(data,('1: '+line+'\n').encode())
        self.assertEqual(event['fileOffset'],before['fileOffset']);self.assertTrue(event['readonly'])
        self.assertLess(event['loadedBytes'],2*1048576);self.assertEqual(source.read_bytes(),original)


class TerminalGrepFileTests(unittest.TestCase):
    def test_plain_terminal_opens_a_buffer_in_the_same_editing_window(self):
        directory=grep.bridge.pty_tests.ARTIFACTS/'terminal-export';directory.mkdir()
        editor=grep.bridge.pty_tests.Editor(directory,b'origin\nhit alpha\nquiet\nhit beta\n')
        try:
            target=directory/'results.txt'
            editor.ex('grep -f hit')
            editor.ex('ls');editor.until(b'MiJoe buffers');editor.until(b'[No Name')
            self.assertIn(b'sample.tx',editor.output)
            editor.send('q');editor.ex('bp');editor.ex('bn')
            editor.ex('saveas '+str(target))
            self.assertEqual(target.read_bytes(),b'2: hit alpha\n4: hit beta\n')
            # A single quit exits: grep -f must not leave a second JOE window.
            self.assertEqual(editor.finish(save=False),b'origin\nhit alpha\nquiet\nhit beta\n')
        finally:editor.close()

    def test_console_numbered_switches_and_delete_preserve_unsaved_source(self):
        directory=grep.bridge.pty_tests.ARTIFACTS/'terminal-buffer-manage';directory.mkdir()
        original=b'origin\nhit alpha\nquiet\nhit beta\n'
        editor=grep.bridge.pty_tests.Editor(directory,original)
        try:
            editor.send('03li!\x1b');editor.ex('grep -f hit')
            editor.ex('b 1');editor.send('i?\x1b');self.assertEqual(editor.path.read_bytes(),original)
            editor.ex('b 2');editor.ex('bd!')
            # Discarding the result must not clear the source's unsaved flag.
            editor.ex('q');editor.send('iX\x1b');editor.ex('w')
            self.assertEqual(editor.path.read_bytes(),b'oriX?!gin\nhit alpha\nquiet\nhit beta\n')
            editor.finish(save=False)
        finally:editor.close()

    def test_console_can_return_to_lazy_readonly_source(self):
        directory=grep.bridge.pty_tests.ARTIFACTS/'terminal-lazy-buffer';directory.mkdir()
        original=b'head\nhit alpha\n'+b'quiet line\n'*160000+b'hit beta\n'
        editor=grep.bridge.pty_tests.Editor(directory,original,options=('-large_threshold','1','-lazy'))
        try:
            editor.ex('grep -f hit');target=directory/'result.txt';editor.ex('saveas '+str(target))
            self.assertEqual(target.read_bytes(),b'2: hit alpha\n160003: hit beta\n')
            editor.ex('b 1');editor.send('iX\x1b');editor.ex('w')
            self.assertEqual(editor.path.read_bytes(),original)
            editor.ex('b 2');editor.ex('bd');self.assertEqual(editor.finish(save=False),original)
        finally:editor.close()


if __name__=='__main__':
    print('Grep file transcripts:',grep.bridge.pty_tests.ARTIFACTS,flush=True)
    unittest.main(verbosity=2)
