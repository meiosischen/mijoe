#!/usr/bin/env python3
"""Recent-file persistence, literal names, modal navigation and mode restore."""
import ast
import os
from pathlib import Path
import struct
import unittest
from grep_control_test import GrepCase, bridge

class RecentTests(GrepCase):
    def restart(self,options=()):
        self.call(9);self.wait_exit();self.editor.close()
        os.close(self.request);os.close(self.response)
        read_request,self.request=os.pipe();self.response,write_response=os.pipe()
        os.set_inheritable(read_request,True);os.set_inheritable(write_response,True)
        self.editor=bridge.pty_tests.Editor(self.directory,b'abcdef\n',options=options,
            extra_env={'MIJOE_GUI_REQUEST_FD':str(read_request),'MIJOE_GUI_RESPONSE_FD':str(write_response)})
        os.close(read_request);os.close(write_response);os.set_blocking(self.response,False)
        self.buffer=bytearray();self.identifier=0
    def profile(self):return self.directory/'.home/.mj'
    def entries(self):
        lines=(self.profile()/'.recent').read_text().splitlines()
        return [(int(line.split('\t',1)[0]),ast.literal_eval(line.split('\t',1)[1])) for line in lines if line and line[0].isdigit()]
    def recent(self):
        self.editor.ex('recent');s=self.call(1);self.assertTrue(s['recentActive']);return s
    def test_startup_record_latest_first_deduplicated(self):
        one=self.open(b'one\n','one.txt');two=self.open(b'two\n','two.txt')
        self.assertEqual([p for _,p in self.entries()][:2],[str(two),str(one)])
        self.editor.ex('e '+str(one));self.call(1)
        values=[p for _,p in self.entries()]
        self.assertEqual(values[0],str(one));self.assertEqual(values.count(str(one)),1)
    def test_choose_reopens_previous_file_and_keeps_unsaved_buffer(self):
        one=self.open(b'one\n','one.txt');two=self.open(b'two\n','two.txt')
        self.editor.send('i!\x1b');s=self.recent();self.assertGreaterEqual(s['recentCount'],3)
        self.editor.send('\x0e\r');s=self.call(1)
        self.assertFalse(s['recentActive']);self.assertEqual(s['file'],str(one));self.assertEqual(s['dirtyBuffers'],1)
        self.editor.ex('bn');self.assertGreaterEqual(self.call(1)['dirtyBuffers'],1)
        self.assertEqual(two.read_bytes(),b'two\n')
    def test_missing_file_is_reported_and_removed(self):
        path=self.open(b'deleted\n','missing.txt');path.unlink()
        count=len(self.entries());self.recent();self.editor.send('\r');s=self.call(1)
        self.assertTrue(s['recentActive']);self.assertEqual(s['recentCount'],count-1)
        self.assertNotIn(str(path),[p for _,p in self.entries()])
        self.assertIn(b'no longer exists',self.editor.output)
    def test_readonly_hex_and_softwrap_modes_restore(self):
        path=self.open(b'abcdef\n','mode.txt')
        self.editor.send('\x14O\x14\'');self.editor.ex('set wrap');self.call(1)
        flags,record=self.entries()[0];self.assertEqual(record,str(path));self.assertEqual(flags&11,11)
        self.editor.ex('enew');self.recent();self.editor.send('\r');s=self.call(1)
        self.assertEqual(s['file'],str(path));self.assertTrue(s['readonly']);self.assertTrue(s['softwrap']);self.assertTrue(s['hex'])
        self.assertFalse(self.call(2)['ok'])
    def test_binary_auto_detection_is_recorded(self):
        path=self.open(b'a\x00b\xff\n','binary.bin')
        self.assertEqual(self.entries()[0][0]&2,2)
        self.recent();self.editor.send('\r');self.assertEqual(self.call(1)['file'],str(path))
    def test_modal_blocks_body_editing_and_native_save(self):
        self.recent();self.editor.send('iNOT-TEXT')
        self.assertFalse(self.call(1)['changed']);self.assertFalse(self.call(2)['ok'])
        self.editor.send('\x03');self.assertFalse(self.call(1)['recentActive'])
        self.assertEqual(self.editor.path.read_bytes(),b'abcdef\n')
    def test_long_paths_wrap_and_pages_scroll(self):
        for i in range(35):
            path=self.directory/(('%02d-'%i)+'x'*100+'.txt');path.write_text('payload\n')
            self.editor.ex('e '+str(path));self.call(1)
        s=self.recent();self.assertGreater(s['recentRows'],s['recentCount'])
        self.editor.send('\x1b[6~');s=self.call(1);self.assertGreater(s['recentTop'],0)
        self.assertGreater(s['recentSelected'],0)
        self.editor.send('\x1b[5~');self.assertEqual(self.call(1)['recentTop'],0)
        self.assertIn(b'#',self.editor.output)
    def test_history_cap_200_and_recent_promotion(self):
        commands=[]
        for i in range(205):
            path=self.directory/('f%03d.txt'%i);path.write_text('data\n');commands.append(':e '+str(path)+'\r')
        for command in commands:
            self.editor.send(command);self.call(1)
        values=self.entries();self.assertEqual(len(values),200)
        self.assertTrue(values[0][1].endswith('f204.txt'));self.assertTrue(values[-1][1].endswith('f005.txt'))
    def test_default_config_comments_do_not_open_a_startup_error_window(self):
        self.assertIn('# MiJoe personal global options',(self.profile()/'config').read_text())
        self.editor.ex('q');self.wait_exit()
    def test_config_and_state_are_under_profile(self):
        self.assertTrue((self.profile()/'.recent').exists())
        self.assertFalse((self.directory/'.home/.joe_state').exists())
    def test_persisted_history_restores_readonly_after_restart(self):
        path=self.open(b'one\n','persistent.txt');self.editor.send('\x14O');self.call(1)
        self.restart();self.recent();self.editor.send('j\r');s=self.call(1)
        self.assertEqual(s['file'],str(path));self.assertTrue(s['readonly'])
    def test_personal_local_defaults_and_state_use_new_root(self):
        (self.profile()/'config').write_text('-softwrap\n-rmargin 12\n')
        self.restart(options=('-joe_state',));s=self.call(1)
        self.assertTrue(s['softwrap']);self.assertEqual(s['wrapWidth'],12)
        self.call(9);self.wait_exit()
        self.assertTrue((self.profile()/'state').exists())
        self.assertFalse((self.directory/'.home/.joe_state').exists())
    def test_legacy_joerc_is_copied_without_deleting_original(self):
        import shutil
        source=Path(bridge.BINARY).parent.parent/'etc/joe/joerc'
        old=self.directory/'.home/.joerc';shutil.copy2(source,old)
        before=old.read_bytes();(self.profile()/'.migrated').unlink(missing_ok=True);self.restart()
        self.assertEqual((self.profile()/'joerc').read_bytes(),before)
        self.assertEqual(old.read_bytes(),before)
    def test_personal_colors_migrate_on_use(self):
        import shutil
        source=Path(bridge.BINARY).parent.parent/'share/joe/colors/teasp.jcf'
        old=self.directory/'.home/.joe/colors';old.mkdir(parents=True)
        shutil.copy2(source,old/'personal.jcf')
        self.editor.ex('colors personal');self.assertEqual(self.call(1)['scheme'],'personal')
        self.assertEqual((self.profile()/'colors/personal.jcf').read_bytes(),source.read_bytes())
        self.assertTrue((old/'personal.jcf').exists())
    def test_native_double_click_opens_without_a_stray_body_mouseup(self):
        path=self.open(b'target\n','target.txt');self.editor.ex('enew');self.recent()
        x=(140-140*3//4)//2+2;y=(40-40*2//3)//2+1
        self.assertTrue(self.call(14,f'4;{x};{y}')['ok'])
        self.assertTrue(self.call(14,f'3;{x};{y}')['ok'])
        s=self.call(1);self.assertFalse(s['recentActive']);self.assertEqual(s['file'],str(path))
        self.assertEqual(s['fileOffset'],0);self.assertFalse(s['changed'])
    def test_terminal_mouse_wheel_navigates_only_the_picker(self):
        self.open(b'one\n','one.txt');self.open(b'two\n','two.txt')
        s=self.recent();before=s['fileOffset']
        self.editor.send('\x1b[<65;5;5M');s=self.call(1)
        self.assertEqual(s['recentSelected'],1);self.assertEqual(s['fileOffset'],before)
        self.editor.send('\x1b[<64;5;5M');self.assertEqual(self.call(1)['recentSelected'],0)
    def test_invalid_history_is_not_overwritten(self):
        bad='# future recent version\nprivate user data\n'
        (self.profile()/'.recent').write_text(bad)
        self.open(b'new\n','new.txt');self.recent()
        self.assertIn(b'Could not read',self.editor.output)
        self.assertEqual((self.profile()/'.recent').read_text(),bad)
    def test_parallel_processes_merge_history_without_losing_paths(self):
        import subprocess,sys
        worker="""import importlib.util,sys
from pathlib import Path
binary,directory,profile,test=sys.argv[1:]
sys.argv=['worker',binary]
spec=importlib.util.spec_from_file_location('ptytest',test);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
e=m.Editor(Path(directory),b'worker\\n',extra_env={'MIJOE_CONFIG_HOME':profile})
try:e.finish(save=False)
finally:e.close()
"""
        processes=[];paths=[]
        for i in range(6):
            directory=self.directory/('worker-%d'%i);directory.mkdir();paths.append(str(directory/'sample.txt'))
            processes.append(subprocess.Popen([sys.executable,'-c',worker,bridge.BINARY,str(directory),str(self.profile()),str(Path(__file__).with_name('vim_pty_test.py').resolve())],stdout=subprocess.PIPE,stderr=subprocess.STDOUT))
        for process in processes:
            output=process.communicate(timeout=20)[0];self.assertEqual(process.returncode,0,output)
        recorded={p for _,p in self.entries()}
        self.assertTrue(set(paths)<=recorded,recorded)
    def test_literal_special_filename_never_becomes_a_command(self):
        path=self.directory/'中文 quote";:q!\n.txt'
        encoded=str(path).replace('\\','\\\\')
        self.assertTrue(self.call(3,encoded)['ok'])
        self.assertEqual(self.entries()[0][1],str(path))
        self.editor.ex('enew');self.recent();self.editor.send('\r');s=self.call(1)
        self.assertFalse(s['recentActive']);self.assertEqual(s['file'],str(path))
        self.assertEqual(path.read_bytes(),b'abcdef\n')

class LazyRecentTests(GrepCase):
    options=('-large_threshold','1')
    def test_lazy_readonly_mode_is_restored_without_another_prompt(self):
        path=self.directory/'large.txt';path.write_bytes(b'payload data\n'*100000)
        self.editor.ex('e '+str(path));self.assertTrue(self.call(1)['largePending'])
        self.call(17,'lazy');self.editor.ex('enew');self.call(1)
        self.editor.ex('recent');self.assertTrue(self.call(1)['recentActive'])
        self.editor.send('\r');s=self.call(1)
        self.assertEqual(s['file'],str(path));self.assertTrue(s['lazy']);self.assertTrue(s['readonly'])
        self.assertFalse(s['largePending']);self.assertFalse(s['recentActive'])
        self.assertLess(s['loadedBytes'],16*1024*1024)

if __name__=='__main__':unittest.main(verbosity=2)
