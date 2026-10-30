#!/usr/bin/env python3
"""Check user paging aliases through the real terminal input decoder."""
import importlib.util
from pathlib import Path
import sys
import unittest

spec=importlib.util.spec_from_file_location("paging_pty",Path(__file__).with_name("vim_pty_test.py"))
pty=importlib.util.module_from_spec(spec)
spec.loader.exec_module(pty)
SOURCE=b"".join(f"line {i:03d} payload\n".encode() for i in range(1,301))


class PagingTests(unittest.TestCase):
    serial=0
    def run_keys(self,keys):
        PagingTests.serial+=1
        directory=pty.ARTIFACTS/str(PagingTests.serial);directory.mkdir()
        editor=pty.Editor(directory,SOURCE)
        try:
            editor.send(b"150G0"+keys)
            return editor.finish()
        finally:
            editor.close()

    def test_control_u_is_full_page_up_in_three_modes(self):
        for enter,finish in ((b"",b"i!\x1b"),(b"i",b"!\x1b"),(b"v",b"di!\x1b")):
            with self.subTest(mode=enter):
                self.assertEqual(self.run_keys(enter+b"\x15"+finish),self.run_keys(enter+b"\x1b[5~"+finish))

    def test_shift_control_u_encodings_are_page_down(self):
        for key in (b"\x1b[117;6u",b"\x1b[85;6u",b"\x1b[27;6;117~",b"\x1b[27;6;85~"):
            for enter,finish in ((b"",b"i!\x1b"),(b"i",b"!\x1b"),(b"v",b"di!\x1b")):
                with self.subTest(key=key,mode=enter):
                    self.assertEqual(self.run_keys(enter+key+finish),self.run_keys(enter+b"\x1b[6~"+finish))

    def test_paging_counts_and_top_bottom_boundaries(self):
        for start in (b"gg",b"G",b"150G"):
            for alias,page in ((b"\x15",b"\x1b[5~"),(b"\x1b[117;6u",b"\x1b[6~")):
                with self.subTest(start=start,key=alias):
                    self.assertEqual(self.run_keys(start+b"2"+alias+b"i!\x1b"),self.run_keys(start+b"2"+page+b"i!\x1b"))


if __name__=="__main__":
    print(f"Paging transcripts: {pty.ARTIFACTS}",flush=True)
    unittest.main(verbosity=2)
