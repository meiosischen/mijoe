#!/usr/bin/env python3
"""Check complete buffer metadata, paged lists and the app-list handshake."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('grep_controls',Path(__file__).with_name('grep_control_test.py'))
grep=importlib.util.module_from_spec(spec);spec.loader.exec_module(grep)


class BufferListTests(grep.GrepCase):
    def test_all_local_buffers_have_complete_names_and_flags(self):
        paths=[]
        for n in range(24):
            name=f'{n:02d}-'+('long-name-'*8)+'.txt'
            paths.append(self.open(b'abcdef\n',name))
        self.editor.send('03li!\x1b');before=self.call(1)
        result=self.call(19);self.assertTrue(result['ok']);text=result['text']
        for path in paths:self.assertIn(str(path),text)
        self.assertIn('%+',text);self.assertGreater(len(text),2500)
        after=self.call(1);self.assertEqual(after['file'],before['file']);self.assertEqual(after['fileOffset'],before['fileOffset']);self.assertEqual(after['changed'],before['changed'])

    def test_ls_emits_app_request_without_mutating_source(self):
        self.editor.send('03l');before=self.call(1);self.editor.ex('ls')
        for _ in range(12):
            event=self.receive(0)
            if event.get('bufferListRequest'):break
        else:self.fail('No all-window buffer-list request')
        self.assertEqual(event['fileOffset'],before['fileOffset']);self.assertEqual(event['changed'],before['changed'])

    def test_global_list_is_paged_readonly_and_closes_to_original_cursor(self):
        self.editor.send('03l');before=self.call(1)
        path=self.directory/'list.txt';path.write_text('All open buffers in MiJoe\n'+''.join(f'Window {n}\n {n}: full-path-{n:03d}.txt\n' for n in range(80))+'FINAL_BUFFER_SENTINEL_xyz\n')
        self.assertTrue(self.call(20,str(path))['ok'])
        self.editor.until(b'All open buffers in MiJo')
        self.editor.send('\x1b[6~'*12);self.editor.until(b'FINAL_BUFFER_SENTINEL_xy')
        self.editor.send('iddcc\x1b[5~q')
        after=self.call(1);self.assertEqual(after['fileOffset'],before['fileOffset']);self.assertEqual(after['changed'],before['changed'])
        self.editor.send('x');self.assertTrue(self.call(2)['ok']);self.assertEqual(self.editor.path.read_bytes(),b'abcef\n')

    def test_listing_metadata_does_not_close_grep(self):
        self.open(b'match first\nmatch second\n');self.editor.ex('grep match')
        before=self.call(1);self.assertTrue(before['grepActive'])
        self.assertTrue(self.call(19)['ok']);after=self.call(1)
        self.assertTrue(after['grepActive']);self.assertEqual(after['fileOffset'],before['fileOffset']);self.editor.send('\x1b')

    def test_control_characters_in_filenames_are_escaped(self):
        path=self.directory/'name\nwith\tcontrols.txt';path.write_text('abc\n')
        self.assertTrue(self.call(3,str(path))['ok'])
        text=self.call(19)['text'];self.assertIn('name<0x0a>with<0x09>controls.txt',text)

    def test_bad_inspection_file_is_rejected_without_changes(self):
        before=self.call(1);self.assertFalse(self.call(20,str(self.directory/'missing'))['ok'])
        after=self.call(1);self.assertEqual(after['file'],before['file']);self.assertEqual(after['changed'],before['changed'])


class LazyBufferListTests(grep.GrepCase):
    options=('-large_threshold','1')
    def test_lazy_buffers_are_listed_without_reading_more_file_data(self):
        path=self.open(b'large text\n'*160000,'lazy-buffer.txt')
        self.assertTrue(self.call(1)['largePending']);self.assertTrue(self.call(17,'lazy')['ok'])
        before=self.call(1);text=self.call(19)['text']
        self.assertIn(str(path),text);self.assertIn('RL',text)
        after=self.call(1);self.assertEqual(after['readBytes'],before['readBytes']);self.assertEqual(after['fileOffset'],before['fileOffset'])


class TerminalBufferListTests(unittest.TestCase):
    def test_ls_shows_a_paged_list_instead_of_a_clipped_status_message(self):
        directory=grep.bridge.pty_tests.ARTIFACTS/'terminal-list';directory.mkdir()
        editor=grep.bridge.pty_tests.Editor(directory,b'abcdef\n')
        try:
            paths=[]
            for n in range(24):
                path=directory/(chr(65+n)*12+'-long-file-name.txt');path.write_bytes(b'abcdef\n');paths.append(path);editor.ex('e '+str(path))
            import fcntl,termios,struct,os,signal
            fcntl.ioctl(editor.fd,termios.TIOCSWINSZ,struct.pack('HHHH',14,220,0,0));os.kill(editor.pid,signal.SIGWINCH)
            editor.send('03l:ls\r');editor.until(b'MiJoe buffers')
            editor.send('\x1b[6~'*12);editor.until(b'XXXXXXXXXXXX')
            self.assertTrue(b'AAAAAAAAAAAA' in editor.output)
            editor.send('qxi!\x1b');editor.ex('w')
            self.assertEqual(paths[-1].read_bytes(),b'abc!ef\n')
            editor.finish(save=False)
        finally:editor.close()


if __name__=='__main__':
    print('Buffer list transcripts:',grep.bridge.pty_tests.ARTIFACTS,flush=True)
    unittest.main(verbosity=2)
