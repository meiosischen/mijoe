#!/usr/bin/env python3
"""Run offscreen AppKit input-client/session integration tests."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
objects = root / "build/macos/app-objects"
output = root / "build/native-gui-tests"
output.mkdir(parents=True, exist_ok=True)
sdk = subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip()
compiler = subprocess.check_output(["xcrun", "--find", "clang"], text=True).strip()
binary = output / "native-view-test"
subprocess.run([compiler, "-isysroot", sdk, "-mmacosx-version-min=11.0", "-fobjc-arc", "-fblocks",
                "-I" + str(root / "macos/gui"), "-I" + str(root / "macos/vendor/libvterm/include"),
                str(root / "tests/macos_native_view_test.m"), str(objects / "MJEditorSession.o"),
                str(objects / "MJEditorView.o"), *map(str, sorted(objects.glob("vterm-*.o"))),
                "-framework", "Cocoa", "-o", str(binary)], check=True)
directory = tempfile.mkdtemp(prefix="offscreen-", dir=output)
subprocess.run([str(binary), str(root / "build/macos/MiJoe.app/Contents/Resources/editor/bin/joe"),
                directory, str(output / "native-view.json")], check=True, timeout=60,
               env=dict(os.environ, HOME=directory))
