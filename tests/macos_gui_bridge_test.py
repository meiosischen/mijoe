#!/usr/bin/env python3
"""Exercise the app's private control pipe against the real child editor."""
import importlib.util
import json
import os
from pathlib import Path
import select
import struct
import sys
import time
import unittest

BINARY = str(Path(sys.argv.pop(1)).resolve())
spec = importlib.util.spec_from_file_location("pty_tests", Path(__file__).with_name("vim_pty_test.py"))
saved = sys.argv[:]
sys.argv = [sys.argv[0], BINARY]
pty_tests = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pty_tests)
sys.argv = saved
KEY = chr(0x10FFFD)


class BridgeTests(unittest.TestCase):
    options = ()

    def setUp(self):
        self.directory = pty_tests.ARTIFACTS / self.id().split(".")[-1]
        self.directory.mkdir()
        read_request, self.request = os.pipe()
        self.response, write_response = os.pipe()
        os.set_inheritable(read_request, True)
        os.set_inheritable(write_response, True)
        self.editor = pty_tests.Editor(self.directory, b"abcdef\n", options=self.options,
                                      extra_env={"MIJOE_GUI_REQUEST_FD":str(read_request),
                                                 "MIJOE_GUI_RESPONSE_FD":str(write_response)})
        os.close(read_request); os.close(write_response)
        os.set_blocking(self.response, False)
        self.buffer = bytearray()
        self.identifier = 0

    def tearDown(self):
        if self.editor.status is None:
            try:
                self.call(9)
                self.wait_exit()
            except (AssertionError, BrokenPipeError, OSError):
                pass
        self.editor.close()
        os.close(self.request); os.close(self.response)

    def packet(self, operation, path=""):
        self.identifier += 1
        name = os.fsencode(path)
        os.write(self.request, struct.pack("!III",len(name),self.identifier,operation)+name)
        return self.identifier

    def receive(self, identifier):
        deadline = time.monotonic()+5
        while time.monotonic()<deadline:
            while b"\n" in self.buffer:
                line,_,tail = self.buffer.partition(b"\n"); self.buffer = bytearray(tail)
                response = json.loads(line)
                if response["id"]==identifier:return response
            ready,_,_ = select.select([self.response,self.editor.fd],[],[],0.05)
            if self.editor.fd in ready:self.editor.drain(0.01)
            if self.response in ready:
                data=os.read(self.response,65536)
                if not data:break
                self.buffer.extend(data)
        raise AssertionError("Missing GUI response; terminal="+repr(bytes(self.editor.output)[-1000:]))

    def call(self, operation, path=""):
        identifier=self.packet(operation,path)
        self.editor.send(KEY)
        return self.receive(identifier)

    def wait_exit(self):
        deadline=time.monotonic()+5
        while self.editor.status is None and time.monotonic()<deadline:
            self.editor.drain(0.02)
            pid,status=os.waitpid(self.editor.pid,os.WNOHANG)
            if pid:self.editor.status=status
        self.assertEqual(self.editor.status,0)

    def test_save_preserves_insert_mode_and_undo_boundary(self):
        self.editor.send("i中文")
        result=self.call(2)
        self.assertTrue(result["ok"]);self.assertEqual(result["mode"],"I")
        self.assertEqual(self.editor.path.read_bytes(),"中文abcdef\n".encode())
        self.editor.send("X\x1b")
        self.assertEqual(self.editor.finish(),"中文Xabcdef\n".encode())

    def test_save_as_handles_quotes_spaces_and_newline_as_filename(self):
        self.editor.send("i中文\x1b")
        target=self.directory/'中文 quote\'" and\nnewline.txt'
        result=self.call(3,target)
        self.assertTrue(result["ok"],result)
        self.assertEqual(result["file"],str(target))
        self.assertEqual(target.read_bytes(),"中文abcdef\n".encode())
        self.assertEqual(self.editor.path.read_bytes(),b"abcdef\n")

    def test_save_as_overwrite_is_explicit(self):
        target=self.directory/'exists.txt';target.write_text('old')
        self.assertTrue(self.call(3,target)["ok"])
        self.assertEqual(target.read_bytes(),b"abcdef\n")

    def test_failed_save_preserves_dirty_buffer(self):
        self.editor.send(b"iX\x1b")
        result=self.call(3,self.directory/'missing/file.txt')
        self.assertFalse(result["ok"]);self.assertTrue(result["changed"])
        self.assertEqual(self.editor.finish(),b"Xabcdef\n")

    def test_copy_and_cut_return_real_selection(self):
        self.editor.send(b"vll")
        copied=self.call(4);self.assertTrue(copied["ok"]);self.assertEqual(copied["text"],"abc")
        cut=self.call(5);self.assertTrue(cut["ok"]);self.assertEqual(cut["text"],"abc")
        self.assertEqual(self.editor.finish(),b"def\n")

    def test_select_all_copy_and_undo_redo(self):
        self.assertTrue(self.call(13)["ok"])
        self.assertEqual(self.call(4)["text"],"abcdef\n")
        self.assertTrue(self.call(5)["ok"])
        self.assertTrue(self.call(6)["ok"])
        self.assertTrue(self.call(7)["ok"])
        self.assertEqual(self.editor.finish(),b"")

    def test_close_refuses_unsaved_buffer(self):
        self.editor.send(b"iX\x1b")
        result=self.call(8)
        self.assertFalse(result["ok"]);self.assertEqual(result["dirtyBuffers"],1)
        self.assertTrue(self.call(2)["ok"])
        self.assertTrue(self.call(8)["ok"])
        self.wait_exit()

    def test_close_checks_other_buffers(self):
        self.editor.send(b"iX\x1b:enew\riY\x1b")
        self.assertEqual(self.call(1)["dirtyBuffers"],2)
        self.assertTrue(self.call(3,self.directory/'second.txt')["ok"])
        self.assertFalse(self.call(8)["ok"])
        self.assertEqual(self.call(1)["dirtyBuffers"],1)

    def test_pipe_action_does_not_interpret_pasted_private_key(self):
        self.editor.send(b"i\x1b[200~A")
        identifier=self.packet(2)
        self.editor.send(KEY+"B\x1b[201~"+KEY)
        self.assertTrue(self.receive(identifier)["ok"])
        self.assertEqual(self.editor.path.read_text(),"A"+KEY+"Babcdef\n")

    def test_mouse_click_positions_cursor(self):
        self.assertTrue(self.call(14,"1;2;1")["ok"])
        self.assertTrue(self.call(14,"3;2;1")["ok"])
        self.editor.send(b"i!\x1b")
        self.assertEqual(self.editor.finish(),b"ab!cdef\n")

    def test_native_help_leaves_file_unchanged(self):
        self.assertTrue(self.call(12)["ok"])
        self.editor.until(b"MiJoe :help")
        self.editor.send(b"q")
        self.assertEqual(self.editor.finish(),b"abcdef\n")

    def test_readonly_cut_and_save_are_rejected(self):
        self.editor.send(b"vll\x14O")
        self.assertTrue(self.call(1)["readonly"])
        self.assertFalse(self.call(5)["ok"])
        self.assertFalse(self.call(2)["ok"])
        self.assertEqual(self.editor.path.read_bytes(),b"abcdef\n")

    def test_menu_blocks_file_actions_but_allows_status(self):
        self.editor.send(b"\x14")
        self.assertTrue(self.call(1)["ok"])
        self.assertFalse(self.call(3,self.directory/'other.txt')["ok"])
        self.editor.send(b"\x03")
        self.assertTrue(self.call(2)["ok"])


if __name__ == "__main__":
    print("GUI bridge transcripts:",pty_tests.ARTIFACTS,flush=True)
    unittest.main(verbosity=2)
