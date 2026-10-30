#!/usr/bin/env python3
"""Compare text object editing and cursor placement against real Vim."""
import importlib.util
from pathlib import Path
import sys
import unittest
import json
import os

spec = importlib.util.spec_from_file_location("motion_oracle", Path(__file__).with_name("vim_motion_test.py"))
motion = importlib.util.module_from_spec(spec)
spec.loader.exec_module(motion)
pty = motion.pty_tests
CASES = []
for obj in ("iw", "aw", "iW", "aW"):
    for name, text, position in (
        ("first", "alpha beta.gamma  tail\n", ""),
        ("middle", "alpha beta.gamma  tail\n", "2l"),
        ("space", "alpha  beta.gamma  tail\n", "5l"),
        ("punctuation", "alpha  beta.gamma  tail\n", "11l"),
        ("last", "alpha  beta.gamma  tail\n", "$"),
        ("tabs", "alpha\t\tbeta tail\n", "5l"),
        ("unicode", "中文 测试_foo。bar tail\n", "3l"),
        ("no-final-newline", "alpha  beta", "$"),
    ):
        for op, command in (("visual", "v"+obj+"d"), ("delete", "d"+obj),
                            ("change", "c"+obj+"Q\x1b"), ("yank", "y"+obj+"P")):
            CASES.append((f"{obj}-{name}-{op}", text, position+command))
    for prefix in ("v2", "v3", "d2", "d3", "2d2", "c2"):
        CASES.append((f"{obj}-{prefix}-count", "one  two.three four  five six seven eight\n",
                      prefix+obj+("d" if prefix.startswith("v") else "Q\x1b" if prefix.startswith("c") else "")))
    for name, keys in (("repeat", "v"+obj+obj+"d"),
                       ("extend-partial", "vli"+obj[-1]+"d"),
                       ("extend-boundary", "v2li"+obj[-1]+"d"),
                       ("backward", "wvhi"+obj[-1]+"d"),
                       ("backward-around", "wvh"+obj+"d"),
                       ("line-visual", "V"+obj+"d"),
                       ("dot", "d"+obj+"."),
                       ("undo", "d"+obj+"u"),
                       ("register", '"ad'+obj+'"aP'),
                       ("cancel", "d"+obj[0]+"\x1bix\x1b")):
        CASES.append((f"{obj}-{name}", "aaa  bbb.ccc  ddd eee\n", keys))


for opening,closing,alias in (("(",")","b"),("[","]","["),("{","}","B"),("<",">","<")):
    text=f"pre {opening}aa {opening}bb{closing} cc{closing} tail\n"
    for obj in ("i"+opening,"a"+opening,"i"+closing,"a"+closing,"i"+alias,"a"+alias):
        for name,position in (("before",""),("open","4l"),("inside","9l"),("close","11l")):
            for op,end in (("v","d"),("d",""),("c","Q\x1b"),("y","P")):
                CASES.append((f"block-{obj}-{name}-{op}",text,position+op+obj+end))
    for keys in ("9lv2i"+opening+"d", "9l2di"+opening, "9lvi"+opening+"i"+opening+"d",
                 "9lvi"+opening+"a"+opening+"d", "4lva"+opening+"d", "di"+opening+"u"):
        CASES.append(("block-nesting",text,keys))
    for text in (opening+"\n  abc\n  "+closing+" tail\n",opening+"  \n  abc\n"+closing+"\n",
                 opening+closing+" tail\n", opening+'a "'+closing+'" b'+closing+'\n',
                 '"'+opening+'aa'+closing+'" tail\n'):
        for op,end in (("v","d"),("d",""),("c","Q\x1b"),("y","P")):
            CASES.append(("block-multiline-empty-quoted",text,op+"i"+opening+end))
for quote in ('"',"'","`"):
    sample=f"pre {quote}alpha \\{quote} beta{quote}  mid {quote}second{quote} tail\n"
    for obj in ("i"+quote,"a"+quote):
        text=sample
        for position in ("","4l","7l","19l","23l","$"):
            for op,end in (("v","d"),("d",""),("c","Q\x1b"),("y","P")):
                CASES.append(("quote-position",text,position+op+obj+end))
        for keys in ("v2"+obj+"d","v3"+obj+"d","v"+obj+obj+"d","v"+obj+"\x1b"):
            CASES.append(("quote-count-repeat",text,keys))
        for text in (quote+quote+" tail\n",quote+"abc\nnext"+quote+"\n","no quote here\n"):
            for op,end in (("v","d"),("d",""),("c","Q\x1b"),("y","P")):
                CASES.append(("quote-empty-missing",text,op+obj+end))

for obj in ("iw","aw","iW","aW"):
    for text in ("aa bb\ncc dd\n", "aa  bb\n  cc\n", "aa\n\ncc\n", "  aa", "   \nabc\n", "\nabc\n"):
        for keys in ("v"+obj+"d", "$v2"+obj+"d", "$d3"+obj, "d99"+obj):
            CASES.append(("word-newline-eof",text,keys))
    CASES.append(("word-preserve-indent","  alpha\n","$d"+obj))
for quote in ('"',"'","`"):
    text=f"pre {quote}first{quote}  gap {quote}second{quote} tail\n"
    for keys in ("vi"+quote+"i"+quote+"i"+quote+"d",
                 "20lvhi"+quote+"d", "20lvha"+quote+"d",
                 "vla"+quote+"d"):
        CASES.append(("quote-existing-selection",text,keys))


class TextObjectTests(unittest.TestCase):
    def test_vim_reference_cases(self):
        directory = pty.ARTIFACTS / "textobjects"
        directory.mkdir()
        evidence=[]
        only=os.environ.get("MIJOE_OBJECT_CASES")
        selected=set(map(int,only.split(","))) if only else None
        for index,(name,text,keys) in enumerate(CASES):
            if selected is not None and index not in selected: continue
            with self.subTest(case=f"{index}-{name}"):
                # Avoid Vim filename expansion of brackets, quotes and braces.
                case=directory/str(index)
                case.mkdir()
                initial=text.encode()
                # Inserting a marker after each operation also checks cursor placement.
                expected=motion.MotionTests.oracle(self,case,initial,keys,continuation="i!\x1b")
                editor=pty.Editor(case,initial)
                try:
                    editor.send(keys)
                    editor.send("i!\x1b")
                    actual=editor.finish()
                    evidence.append(dict(case=name,keys=keys,expected=expected.hex(),actual=actual.hex()))
                    self.assertEqual(actual,expected)
                finally:
                    editor.close()
        (directory/"comparison.json").write_text(json.dumps(evidence,ensure_ascii=False,indent=2)+"\n")


if __name__=="__main__":
    print(f"Text object transcripts: {pty.ARTIFACTS}; Vim oracle cases: {len(CASES)}",flush=True)
    unittest.main(verbosity=2)
