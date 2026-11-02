#!/usr/bin/env python3
"""Exercise the real editor's internal pipelines; use system awk as an oracle."""
import importlib.util
import json
from pathlib import Path
import subprocess
import unittest

spec=importlib.util.spec_from_file_location('grep_controls',Path(__file__).with_name('grep_control_test.py'))
grep=importlib.util.module_from_spec(spec);spec.loader.exec_module(grep)


class PipelineTests(grep.GrepCase):
    def tearDown(self):
        if hasattr(self,'transfer'):self.transfer.unlink(missing_ok=True)
        super().tearDown()

    def export(self,command):
        if hasattr(self,'transfer'):self.transfer.unlink(missing_ok=True)
        self.editor.ex(command)
        for _ in range(1000):
            event=self.receive(0)
            if 'openTextFile' in event:
                self.transfer=Path(event['openTextFile'])
                return event,self.transfer.read_bytes()
        self.fail('No generated result event')

    def output(self,command):
        return self.export(command+' | less -f')[1]

    def test_awk_matches_system_reference(self):
        data=b'  WARN alpha 10\nINFO beta 2\nWARN gamma 30\nERROR delta -5\n\n'
        self.open(data);evidence=[]
        for program in ['{print $2,$3}', '{print NR,NF,$0}', '$3 >= 10 {print $2}',
                        '$1 == "WARN" {print $0}', '$1 != "WARN" {print NR,$1}',
                        '/WARN|ERROR/ {print $2}', '$2 ~ /a$/ {print $1}',
                        '$2 !~ /^alpha$/ {print $2}', 'NR <= 3',
                        '{print "[" $1 "]",$99}', '{print}', '{print 42,"ok";}',
                        'NF == 0 {print "empty"}', '{print NR,$NF}', '{print$1}',
                        '/^$/ {print "blank"}', '$0~// {print NR}',
                        '/[A-Z|]+/ {print $1}', '/^WARN|ERROR/ {print $1}',
                        '/(WARN|INFO)/ {print $2}']:
            with self.subTest(program=program):
                expected=subprocess.check_output(['/usr/bin/awk',program],input=data)
                actual=self.output("awk '"+program+"'")
                self.assertEqual(actual,expected)
                evidence.append({'program':program,'expected':expected.hex(),'actual':actual.hex()})
        (self.directory/'awk-oracle.json').write_text(json.dumps(evidence,indent=2)+'\n')

    def test_grep_awk_grep_order_and_stage_nr(self):
        self.open(b'WARN alpha 10\nINFO beta 20\nWARN gamma 30\nERROR alpha 40\n')
        self.assertEqual(self.output("grep WARN | awk '{print NR,$2}' | grep gamma"),b'2 gamma\n')
        self.assertEqual(self.output("awk '{print $2}' | grep '^alpha$' | awk '{print NR,$0}'"),b'1 alpha\n2 alpha\n')
        self.assertEqual(self.output("grep alpha | grep ERROR | awk '{print $3}'"),b'40\n')

    def test_single_character_delimiters_and_empty_fields(self):
        data='name,,value,\n中文,甲,30,尾\n'.encode();self.open(data)
        for program in ['{print NF,$1,$2,$4}', '$3 > 10 {print $1}', '{print $0}']:
            expected=subprocess.check_output(['/usr/bin/awk','-F',',',program],input=data)
            self.assertEqual(self.output("awk -F',' '"+program+"'"),expected)
        self.open('甲😀乙😀丙\n'.encode(),'emoji.txt')
        self.assertEqual(self.output("awk -F '😀' '{print NF,$2}'"),'3 乙\n'.encode())
        self.open(b'a\t\tb\t\n','tabs.txt')
        self.assertEqual(self.output("awk -F '\\t' '{print NF,$1,$2,$3,$4}'"),b'4 a  b \n')

    def test_numeric_field_comparisons(self):
        data=b'10 2\n2 10\na b\n';self.open(data)
        for program in ['$1 > $2 {print}', '$1 <= $2 {print}', '$1 > 3 {print}', '$1 == "2" {print}']:
            self.assertEqual(self.output("awk '"+program+"'"),subprocess.check_output(['/usr/bin/awk',program],input=data))

    def test_pipe_inside_quotes_regex_and_unquoted_action(self):
        self.open(b'WARN alpha\nINFO beta\nERROR gamma\n')
        self.assertEqual(self.output('awk \'/WARN|ERROR/ {print $2 "|" $1}\''),b'alpha|WARN\ngamma|ERROR\n')
        self.assertEqual(self.output('awk {print $1 "|" $2} | grep alpha'),b'WARN|alpha\n')
        self.assertEqual(self.output("awk '{print $2}' | grep 'alpha|gamma'"),b'alpha\ngamma\n')

    def test_head_default_counts_and_early_filter_stop(self):
        self.open(b''.join(f'item {n}\n'.encode() for n in range(1,21)))
        self.assertEqual(self.output('head'),b''.join(f'item {n}\n'.encode() for n in range(1,11)))
        self.assertEqual(self.output('head -n3 | head -n 2'),b'item 1\nitem 2\n')
        self.assertEqual(self.output('head|head -n1'),b'item 1\n')
        self.assertEqual(self.output("head -n 2 | grep 2 | awk '{print NR,$2}'"),b'1 2\n')
        before=self.call(1);self.editor.ex('head -n 2 | grep 20');after=self.call(1)
        self.assertFalse(after['grepActive']);self.assertEqual(after['fileOffset'],before['fileOffset'])

    def test_head_zero_and_empty_input(self):
        for data in [b'',b'a\nb\n']:
            self.open(data,'zero-'+str(len(data))+'.txt');before=self.call(1)
            for command in ['head -n 0','awk \'{print}\' | head -n0 | less']:
                self.editor.ex(command);after=self.call(1)
                self.assertFalse(after['grepActive']);self.assertFalse(after['changed'])
                self.assertEqual(after['fileOffset'],before['fileOffset'])

    def test_more_less_passthrough_and_paging(self):
        self.open(b''.join(f'row {n}\n'.encode() for n in range(100)))
        self.assertEqual(self.output("more | awk '{print $2}' | less | head -n 2"),b'0\n1\n')
        self.editor.send('gg02l');before=self.call(1)
        self.editor.ex('less');self.assertEqual(self.call(1)['grepCount'],100)
        self.editor.send(' ');self.assertGreater(self.call(1)['grepSelected'],10)
        self.editor.send('b');self.assertEqual(self.call(1)['grepSelected'],0)
        self.editor.send('\x0e');self.assertEqual(self.call(1)['grepSelected'],1)
        self.editor.send('\x03');after=self.call(1)
        self.assertFalse(after['grepActive']);self.assertEqual(after['fileOffset'],before['fileOffset'])

    def test_transformed_results_jump_to_original_lines(self):
        data=b'WARN alpha\nINFO beta\nWARN gamma\n';self.open(data)
        self.editor.ex("grep WARN | awk '{print $2}' | more")
        self.assertEqual(self.call(1)['grepCount'],2)
        self.editor.send('\x0e');self.assertEqual(self.call(1)['fileOffset'],data.index(b'WARN gamma'))
        self.editor.send('\r');self.assertFalse(self.call(1)['grepActive'])

    def test_export_final_text_and_keep_unsaved_source(self):
        source=self.open(b'WARN alpha\nINFO beta\n');self.editor.send('A!\x1b');before=self.call(1)
        event,data=self.export("grep -f WARN | awk '{print $2}' | head -n 1")
        self.assertEqual(data,b'alpha!\n');self.assertTrue(event['changed'])
        self.assertEqual(event['fileOffset'],before['fileOffset']);self.assertEqual(source.read_bytes(),b'WARN alpha\nINFO beta\n')
        self.assertTrue(self.call(18,str(self.transfer))['ok']);r=self.call(1)
        self.assertEqual(r['file'],'');self.assertTrue(r['changed']);self.assertFalse(r['readonly'])

    def test_full_transformed_lines_beyond_popup_preview(self):
        text='WARN '+('中文😀'*1000)+' END\n';self.open(text.encode())
        self.assertEqual(self.output("awk '{print $2,$3}'"),(('中文😀'*1000)+' END\n').encode())

    def test_crlf_bom_no_final_newline_and_empty_record(self):
        self.open(b'\xef\xbb\xbfWARN first\r\n\r\nWARN last')
        self.assertEqual(self.output("awk '{print NR,NF,$0}'"),b'1 2 WARN first\n2 0 \n3 2 WARN last\n')

    def test_syntax_errors_preserve_buffer_and_cursor(self):
        source=self.open(b'WARN alpha\n');self.editor.send('02li!\x1b');before=self.call(1)
        for command in ["grep WARN |", "head | unknown", "head -n -1", "head -n nope", "head -n 9999999999",
                        "awk '{system(\"echo BAD\")}'", "awk '{print $4097}'", "awk -F xx '{print}'",
                        "awk '{print $1'", "awk '$1 = 2 {print}'", "awk '{print $1,}'", "more extra",
                        "awk! '{print}'", "awk '{print $1+1}'", "awk '{print $1-1}'", "awk '{print}' || less", "awk '/[/'", "head | grep ["]:
            with self.subTest(command=command):
                self.editor.ex(command);after=self.call(1)
                self.assertFalse(after['grepActive']);self.assertEqual(after['fileOffset'],before['fileOffset'])
                self.assertEqual(after['changed'],before['changed']);self.assertEqual(source.read_bytes(),b'WARN alpha\n')

    def test_field_limit_is_explicit(self):
        self.open(b'x '*4097+b'\n');self.editor.ex("awk '{print NF}'")
        self.assertFalse(self.call(1)['grepActive']);self.editor.until(b'exceeds 4096 fields')

    def test_source_record_limit_is_explicit(self):
        source=self.open(b'x'*(1048576+1)+b'\n');self.editor.ex('head -n 1')
        self.assertFalse(self.call(1)['grepActive']);self.editor.until(b'exceeds 1 MiB')
        self.assertEqual(source.stat().st_size,1048578)

    def test_result_limit_is_applied_after_filters(self):
        self.open(b'no\n'*10020+b'YES target\n')
        self.assertEqual(self.output("awk '{print $0}' | grep YES | head -n1"),b'YES target\n')
        self.open(b'x\n'*10001,'cap.txt');data=self.output('more')
        self.assertEqual(data.count(b'x\n'),10000);self.assertTrue(data.endswith(b'[pipeline: results limited to 10000 lines]\n'))

    def test_total_output_limit_cancels_without_creating_result(self):
        source=self.open((b'x'*4096+b'\n')*300,'output-limit.txt');before=self.call(1)
        program='{print '+','.join(['$0']*64)+'}'
        self.editor.ex("awk -f '"+program+"'");after=self.call(1)
        self.assertFalse(after['grepActive']);self.assertEqual(after['file'],str(source))
        self.assertFalse(after['changed']);self.assertEqual(after['fileOffset'],before['fileOffset'])
        self.editor.until(b'output exceeds 64 MiB')

    def test_stage_and_print_item_boundaries(self):
        self.open(b'one\ntwo\n');command='head -f -n1'+'|more'*15
        self.assertEqual(self.export(command)[1],b'one\n')
        self.editor.ex(command+'|less');self.assertFalse(self.call(1)['grepActive'])
        self.editor.until(b'at most 16 commands')
        self.editor.ex("awk '{print "+','.join(['$0']*65)+"}'")
        self.assertFalse(self.call(1)['grepActive']);self.editor.until(b'at most 64 print items')

    def test_ctrl_c_interrupts_scan_and_restores_cursor(self):
        self.open(b'quiet no match\n'*200000,'cancel.txt');self.editor.send('03l');before=self.call(1)
        self.editor.send(':grep NEVER_MATCH_THIS | head -n 1\r\x03')
        after=self.call(1);self.assertFalse(after['grepActive']);self.assertEqual(after['fileOffset'],before['fileOffset'])
        self.editor.until(b'Pipeline cancelled')


class LazyPipelineTests(PipelineTests):
    options=('-large_threshold','1','-lazy')
    def id(self):
        return super().id()+'_lazy'
    def open(self,data,name='input.txt'):
        path=super().open(data,name)
        if self.call(1).get('largePending'):self.assertTrue(self.call(17,'lazy')['ok'])
        return path
    def test_export_final_text_and_keep_unsaved_source(self):
        source=self.open(b'WARN alpha\nINFO beta\n');self.editor.send('02l');before=self.call(1)
        self.assertTrue(before['readonly'])
        event,data=self.export("grep -f WARN | awk '{print $2}' | head -n 1")
        self.assertEqual(data,b'alpha\n');self.assertFalse(event['changed'])
        self.assertEqual(event['fileOffset'],before['fileOffset']);self.assertEqual(source.read_bytes(),b'WARN alpha\nINFO beta\n')
        self.assertTrue(self.call(18,str(self.transfer))['ok']);self.assertFalse(self.call(1)['readonly'])
    def test_lazy_utf16_and_head_bounded_reads(self):
        for encoding in ['utf-8','utf-16-le','utf-16-be']:
            text='WARN 中文😀,30\r\nINFO other,2\r\n'+'quiet pad\r\n'*180000
            raw=text.encode(encoding)
            if encoding=='utf-16-le':raw=b'\xff\xfe'+raw
            if encoding=='utf-16-be':raw=b'\xfe\xff'+raw
            path=self.open(raw,'lazy-'+encoding+'.txt');before=self.call(1);self.assertTrue(before['readonly'])
            self.assertEqual(self.output("head -n 2 | awk -F ',' '$2>10 {print $1}'"),'WARN 中文😀\n'.encode())
            after=self.call(1);self.assertLess(after['readBytes']-before['readBytes'],200000)
            self.assertLess(after['loadedBytes'],2*1048576);self.assertEqual(path.read_bytes(),raw)
            before=after;self.editor.ex('head -n 0');after=self.call(1)
            self.assertEqual(before['readBytes'],after['readBytes'])

    def test_lazy_utf16_surrogate_across_io_chunk(self):
        line='WARN '+('x'*32761)+'😀,30'
        raw=(line+'\n'+'pad\n'*180000).encode('utf-16')
        self.open(raw,'surrogate.txt')
        self.assertEqual(self.output("head -n 1 | awk -F ',' '{print $1}'"),(line.rsplit(',',1)[0]+'\n').encode())


class TerminalPipelineTests(unittest.TestCase):
    def test_console_export_is_an_editable_buffer_in_same_window(self):
        directory=grep.bridge.pty_tests.ARTIFACTS/'terminal-pipeline';directory.mkdir()
        editor=grep.bridge.pty_tests.Editor(directory,b'WARN alpha\nINFO beta\n')
        try:
            editor.ex("awk -f '{print $2}' | head -n 1")
            editor.ex('ls');editor.until(b'[No Name');editor.until(b'sample.tx');editor.send('q')
            target=directory/'result.txt';editor.ex('saveas '+str(target))
            self.assertEqual(target.read_bytes(),b'alpha\n')
            editor.ex('bp');editor.send('iX\x1b');editor.ex('w')
            self.assertEqual(editor.path.read_bytes(),b'XWARN alpha\nINFO beta\n')
            editor.ex('bn');editor.ex('bd');editor.finish(save=False)
        finally:editor.close()


if __name__=='__main__':
    print('Pipeline transcripts:',grep.bridge.pty_tests.ARTIFACTS,flush=True)
    unittest.main(verbosity=2)
