#!/usr/bin/env python3
"""Run offscreen AppKit input-client/session integration tests."""
from pathlib import Path
import os
import subprocess
import tempfile
import argparse

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("--softwrap",action="store_true",help="Run display-only line wrapping checks")
parser.add_argument("--recent",action="store_true",help="Run recent-file modal and profile preference checks")
parser.add_argument("--substitute",action="store_true",help="Run rendered Ex replacement confirmation checks")
parser.add_argument("--line-percent",action="store_true",help="Run rendered logical-line status percentage checks")
parser.add_argument("--temp-highlight",action="store_true",help="Run temporary literal overlay and lifetime checks")
parser.add_argument("--history",action="store_true",help="Run command history provenance, paging and persistence checks")
args=parser.parse_args()
name="macos_command_history_test" if args.history else "macos_temp_highlight_test" if args.temp_highlight else "macos_line_percent_test" if args.line_percent else "macos_substitute_test" if args.substitute else "macos_recent_test" if args.recent else "macos_softwrap_test" if args.softwrap else "macos_native_view_test"
report="command-history.json" if args.history else "temp-highlight.json" if args.temp_highlight else "line-percent.json" if args.line_percent else "substitute.json" if args.substitute else "recent.json" if args.recent else "softwrap.json" if args.softwrap else "native-view.json"
root = Path(__file__).resolve().parents[1]
objects = root / "build/macos/app-objects"
output = root / "build/native-gui-tests"
output.mkdir(parents=True, exist_ok=True)
sdk = subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip()
compiler = subprocess.check_output(["xcrun", "--find", "clang"], text=True).strip()
binary = output / name
subprocess.run([compiler, "-isysroot", sdk, "-mmacosx-version-min=11.0", "-fobjc-arc", "-fblocks",
                "-I" + str(root / "macos/gui"), "-I" + str(root / "macos/vendor/libvterm/include"),
                str(root / ("tests/"+name+".m")), str(objects / "MJEditorSession.o"),
                str(objects / "MJEditorView.o"), str(objects / "MJPreferences.o"), *map(str, sorted(objects.glob("vterm-*.o"))),
                "-framework", "Cocoa", "-o", str(binary)], check=True)
directory = tempfile.mkdtemp(prefix="offscreen-", dir=output)
subprocess.run([str(binary), str(root / "build/macos/MiJoe.app/Contents/Resources/editor/bin/joe"),
                directory, str(output / report)], check=True, timeout=60,
               env=dict(os.environ, HOME=directory, MIJOE_CONFIG_HOME=str(Path(directory)/".mj")))
