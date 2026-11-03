#!/usr/bin/env python3
"""External edits detected while idle, without replacing a live buffer silently."""
import os
import time
import unittest
from grep_control_test import GrepCase

class ExternalChangeHelpers:
    def change(self,data=b'new text\n',replace=False,stamp=None):
        start=len(self.editor.output)
        if replace:
            tmp=self.editor.path.with_suffix('.replacement');tmp.write_bytes(data)
            if stamp is not None:os.utime(tmp,ns=stamp)
            tmp.replace(self.editor.path)
        else:
            self.editor.path.write_bytes(data)
            if stamp is not None:os.utime(self.editor.path,ns=stamp)
        return start
    def notice(self,start,text=b'Reload',timeout=4):
        end=time.monotonic()+timeout
        while text not in self.editor.output[start:] and time.monotonic()<end:self.editor.drain(.1)
        self.assertIn(text,self.editor.output[start:])
    def quiet(self,start,seconds=1.3):
        self.editor.drain(seconds)
        self.assertNotIn(b'File changed on disk.',self.editor.output[start:])
    def save(self):self.assertTrue(self.call(2)['ok'])

class ExternalChangeTests(ExternalChangeHelpers,GrepCase):
    def test_idle_accept_reads_new_text(self):
        start=self.change();self.notice(start);self.editor.send('y');self.call(1)
        self.editor.send('0iX\x1b');self.save();self.assertEqual(self.editor.path.read_bytes(),b'Xnew text\n')
    def test_decline_keeps_buffer_and_save_protection(self):
        start=self.change();self.notice(start);self.editor.send('n');self.call(1)
        self.assertFalse(self.call(2)['ok']);start=len(self.editor.output);self.quiet(start)
        self.editor.ex('w!');self.call(1);self.assertEqual(self.editor.path.read_bytes(),b'abcdef\n')
    def test_dirty_prompt_and_accept(self):
        self.editor.send('iLOCAL\x1b');self.call(1)
        start=self.change();self.notice(start,b'DISCARD unsaved changes?');self.editor.send('y');self.call(1)
        self.assertFalse(self.call(1)['changed']);self.editor.send('0iX\x1b');self.save()
        self.assertEqual(self.editor.path.read_bytes(),b'Xnew text\n')
    def test_dirty_cancel_keeps_edits(self):
        self.editor.send('iLOCAL\x1b');self.call(1)
        start=self.change();self.notice(start);self.editor.send('\x03');self.assertTrue(self.call(1)['changed'])
        self.editor.ex('w!');self.call(1);self.assertEqual(self.editor.path.read_bytes(),b'LOCALabcdef\n')
    def test_another_version_prompts_again(self):
        start=self.change();self.notice(start);self.editor.send('n');self.call(1)
        start=self.change(b'third version\n');self.notice(start);self.editor.send('y');self.call(1)
    def test_atomic_replace_preserving_time_and_length(self):
        st=self.editor.path.stat();start=self.change(b'ghijkl\n',True,(st.st_atime_ns,st.st_mtime_ns));self.notice(start)
        self.editor.send('y');self.call(1);self.editor.send('iX\x1b');self.save()
        self.assertEqual(self.editor.path.read_bytes(),b'Xghijkl\n')
    def test_same_time_different_length(self):
        st=self.editor.path.stat();start=self.change(stamp=(st.st_atime_ns,st.st_mtime_ns));self.notice(start);self.editor.send('n');self.call(1)
    def test_older_timestamp_is_detected(self):
        st=self.editor.path.stat();start=self.change(stamp=(st.st_atime_ns,st.st_mtime_ns-2000000000));self.notice(start);self.editor.send('n');self.call(1)
    def test_deletion_keeps_buffer_then_recreation_prompts(self):
        start=len(self.editor.output);self.editor.path.unlink();self.notice(start,b'File removed from disk.');self.editor.send('y');self.call(1)
        self.assertFalse(self.call(2)['ok']);self.assertFalse(self.editor.path.exists())
        start=self.change();self.notice(start);self.editor.send('y');self.call(1)
    def test_successful_save_does_not_trigger_notice(self):
        self.editor.send('iX\x1b');self.save();self.quiet(len(self.editor.output))
    def test_defer_command_bar(self):
        self.editor.send(':set-hl abc');start=self.change();self.quiet(start)
        self.editor.send('\x1b');self.notice(start);self.editor.send('n');self.call(1)
    def test_defer_insert_and_visual(self):
        for enter in ['i','v']:
            self.editor.send(enter);start=self.change((enter+' changed\n').encode());self.quiet(start)
            self.editor.send('\x1b');self.notice(start);self.editor.send('n');self.call(1)
    def test_defer_help_and_grep(self):
        for cmd in ['help','grep abc']:
            self.editor.ex(cmd);start=self.change((cmd+' changed\n').encode());self.quiet(start)
            self.editor.send('\x03');self.notice(start);self.editor.send('n');self.call(1)
    def test_readonly_stays_readonly(self):
        self.editor.send('\x14O');self.assertTrue(self.call(1)['readonly'])
        start=self.change();self.notice(start);self.editor.send('y');self.assertTrue(self.call(1)['readonly'])
        self.editor.send('iBAD\x1b');self.assertFalse(self.call(1)['changed'])
    def test_file_switch_uses_original_opened_version(self):
        original=self.editor.path;other=self.directory/'other.txt';other.write_bytes(b'other\n')
        self.editor.ex('e '+str(other));self.call(1);start=self.change();self.quiet(start)
        self.editor.ex('bp');self.notice(start);self.editor.send('y');self.call(1)
        self.editor.send('0iX\x1b');self.save();self.assertEqual(original.read_bytes(),b'Xnew text\n')

class LazyChangeTests(ExternalChangeHelpers,GrepCase):
    options=('-large_threshold','1')
    def test_lazy_refresh_remains_bounded_and_readonly(self):
        path=self.directory/'large.txt';path.write_bytes(b'old row\n'*200000)
        self.editor.ex('e '+str(path));self.assertTrue(self.call(1)['largePending'])
        self.assertTrue(self.call(17,'lazy')['ok'])
        start=len(self.editor.output);path.write_bytes(b'new row\n'*200000)
        self.notice(start);self.editor.send('y');r=self.call(1)
        self.assertTrue(r['lazy']);self.assertTrue(r['readonly']);self.assertFalse(r['largePending'])
        self.assertLessEqual(r['loadedBytes'],1048576*2);self.assertLess(r['readBytes'],1200000)
        self.assertIn(b'new row',self.editor.output[start:])
    def test_normal_file_growing_large_refreshes_lazily(self):
        start=self.change(b'new row\n'*200000);self.notice(start);self.editor.send('y');r=self.call(1)
        self.assertTrue(r['lazy']);self.assertTrue(r['readonly']);self.assertFalse(r['largePending'])
        self.assertLessEqual(r['loadedBytes'],1048576*2)


class DisabledCheckTests(GrepCase):
    options=('-nomodcheck',)
    def test_disabled_poll_keeps_explicit_save_guard(self):
        self.editor.path.write_bytes(b'changed\n');start=len(self.editor.output);self.editor.drain(1.5)
        self.assertNotIn(b'File changed on disk.',self.editor.output[start:]);self.assertFalse(self.call(2)['ok'])

if __name__=='__main__':unittest.main()
