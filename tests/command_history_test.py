#!/usr/bin/env python3
"""Submitted command-bar history, macro provenance, persistence and bounded merging."""
import ast
import os
from pathlib import Path
import stat
import unittest
from grep_control_test import GrepCase, bridge
F5='\x1b[15~';F6='\x1b[17~'

class CommandHistoryTests(GrepCase):
    def path(self):return self.directory/'.home/.mj/.history'
    def rows(self,path=None):
        p=path or self.path()
        if not p.exists():return []
        result=[]
        for line in p.read_text().splitlines()[1:]:
            if not line:continue
            number,origin,text=line.split('\t',2)
            result.append((int(number),int(origin),ast.literal_eval(text)))
        return result
    def restart(self):
        self.call(9);self.wait_exit();self.editor.close();os.close(self.request);os.close(self.response)
        read_request,self.request=os.pipe();self.response,write_response=os.pipe()
        os.set_inheritable(read_request,True);os.set_inheritable(write_response,True)
        self.editor=bridge.pty_tests.Editor(self.directory,b'abcdef\n',extra_env={
            'MIJOE_GUI_REQUEST_FD':str(read_request),'MIJOE_GUI_RESPONSE_FD':str(write_response)})
        os.close(read_request);os.close(write_response);os.set_blocking(self.response,False)
        self.buffer=bytearray();self.identifier=0
    def test_only_enter_submitted_command_bar_inputs(self):
        self.editor.send('jliX\x1bu')
        self.editor.send(':set-hl cancelled\x1b')
        self.assertEqual(self.rows(),[])
        self.editor.ex('set-hl "gogogo 123"')
        self.editor.send('/abc\r?def\r')
        self.assertEqual([r[2] for r in self.rows()],['?def','/abc',':set-hl "gogogo 123"'])
    def test_empty_inputs_and_history_queries_do_not_pollute(self):
        self.editor.ex('');self.editor.ex('  ');self.editor.ex('history')
        self.assertEqual(self.rows(),[]);self.editor.send('q')
        self.editor.ex('set-hl keep');before=self.path().read_bytes()
        self.editor.ex('history');self.editor.send('q');self.editor.ex('history');self.editor.send('\x03')
        self.assertEqual(self.path().read_bytes(),before)
    def test_errors_are_recorded(self):
        self.editor.ex('not-a-command');self.assertEqual(self.rows()[0][2],':not-a-command')
        self.editor.ex('history bad');self.assertEqual(self.rows()[0][2],':history bad')
        self.assertFalse(self.call(1)['changed'])
    def test_keep_last_200_in_submission_order_without_deduplication(self):
        for n in range(225):
            self.editor.ex('noop-%03d'%n);self.call(1)
        rows=self.rows();self.assertEqual(len(rows),200)
        self.assertEqual(rows[0],(225,0,':noop-224'));self.assertEqual(rows[-1],(26,0,':noop-025'))
        self.editor.ex('noop-224');rows=self.rows();self.assertEqual(rows[0][2],rows[1][2]);self.assertNotEqual(rows[0][0],rows[1][0])
    def test_utf8_quotes_backslashes_and_pipe_roundtrip(self):
        cmd=r'set-hl "中文 日志" "a\"b" "C:\path" a|b'
        self.editor.ex(cmd);self.assertEqual(self.rows()[0][2],':'+cmd)
        self.assertEqual(stat.S_IMODE(self.path().stat().st_mode),0o600)
    def test_register_expansion_records_submitted_value(self):
        self.editor.send('v2ly:set-hl \x120\r')
        self.assertEqual(self.rows()[0][2],':set-hl abc')
    def test_macro_record_and_replay_submit_commands_with_origins(self):
        self.editor.send(F5+':set-hl abc\r'+F5)
        self.assertEqual(self.rows()[0][1:],(1,':set-hl abc'))
        self.editor.send(F6);self.call(1);self.editor.send(F6);self.call(1)
        self.assertEqual([(r[1],r[2]) for r in self.rows()],[(2,':set-hl abc'),(2,':set-hl abc'),(1,':set-hl abc')])
    def test_macro_editor_keys_are_not_command_history(self):
        self.editor.send(F5+'iX\x1bl'+F5+F6);self.call(1)
        self.assertEqual(self.rows(),[])
    def test_macro_search_command_is_recorded(self):
        self.editor.send(F5+'/def\r'+F5+F6);self.call(1)
        self.assertEqual([(r[1],r[2]) for r in self.rows()],[(2,'/def'),(1,'/def')])
    def test_native_actions_do_not_add_synthetic_commands(self):
        self.call(2);self.call(14,'1;1;1');self.call(14,'3;1;1')
        self.assertEqual(self.rows(),[])
    def test_readonly_paged_view_preserves_document_and_position(self):
        for n in range(50):self.editor.ex('noop-%03d'%n)
        self.editor.send('02l');before=self.call(1);stored=self.path().read_bytes()
        self.editor.ex('history');self.assertIn(b'MiJoe :history',self.editor.output)
        self.editor.send('iXYZdd\x1b[6~\x1b[5~\x03')
        after=self.call(1);self.assertEqual(after['fileOffset'],before['fileOffset']);self.assertFalse(after['changed'])
        self.assertEqual(self.path().read_bytes(),stored);self.assertEqual(self.editor.path.read_bytes(),b'abcdef\n')
    def test_history_survives_restart_and_ignores_joe_state_switch(self):
        self.editor.ex('set-hl previous');saved=self.rows()
        self.restart();self.assertEqual(self.rows(),saved)
        self.editor.ex('set-hl next');self.assertEqual(self.rows()[0][0],saved[0][0]+1)
        self.assertEqual(self.rows()[1],saved[0])
    def test_cross_buffer_inputs_are_global(self):
        self.editor.ex('set-hl original');self.editor.ex('enew');self.editor.ex('set-hl second')
        self.assertEqual([r[2] for r in self.rows()],[':set-hl second',':enew',':set-hl original'])
    def test_corrupt_file_is_preserved_and_commands_still_execute(self):
        bad=b'not-command-history\n';self.path().write_bytes(bad)
        self.editor.ex('set-hl abc');self.assertEqual(self.call(1)['temporaryHighlights'],1)
        self.assertEqual(self.path().read_bytes(),bad)
        self.editor.ex('history');self.assertIn(b'original file kept',self.editor.output)
    def test_invalid_utf8_file_is_preserved(self):
        bad=b'# MiJoe command history v1\n1\t0\t":bad\xff"\n';self.path().write_bytes(bad)
        self.editor.ex('set-hl abc');self.assertEqual(self.path().read_bytes(),bad)
    def test_embedded_nul_history_is_preserved(self):
        bad=b'# MiJoe command history v1\n1\t0\t":abc\\x00hidden"\n';self.path().write_bytes(bad)
        self.editor.ex('set-hl abc');self.assertEqual(self.path().read_bytes(),bad)
    def test_long_command_is_preserved_without_display_execution(self):
        command='unknown-'+('x'*7000)
        self.editor.send(':')
        for at in range(0,len(command),64):
            self.editor.send(command[at:at+64]);self.call(1)
        self.editor.send('\r');self.call(1);self.assertEqual(self.rows()[0][2],':'+command)
        self.editor.ex('history');self.editor.send('\x1b[6~\x1b[5~q');self.assertFalse(self.call(1)['changed'])
    def test_multiple_processes_merge_atomically(self):
        shared=self.directory/'shared';shared.mkdir();editors=[]
        try:
            for n in range(5):
                d=self.directory/('writer-%d'%n);d.mkdir()
                editors.append(bridge.pty_tests.Editor(d,b'abc\n',extra_env={'MIJOE_CONFIG_HOME':str(shared)}))
            for k in range(6):
                for n,e in enumerate(editors):e.ex('set-hl worker-%d-%d'%(n,k))
            rows=self.rows(shared/'.history');self.assertEqual(len(rows),30)
            self.assertEqual({r[2] for r in rows},{':set-hl worker-%d-%d'%(n,k) for n in range(5) for k in range(6)})
            self.assertEqual([r[0] for r in rows],list(range(30,0,-1)))
        finally:
            for e in editors:e.close()

if __name__=='__main__':unittest.main()
