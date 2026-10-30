#!/usr/bin/env python3
"""Compare offscreen renderer draw costs using the current app build."""
from pathlib import Path
import subprocess,tempfile,sys,os
root=Path(__file__).resolve().parents[1];obj=root/'build/macos/app-objects';out=root/'build/render-benchmark';out.mkdir(exist_ok=True)
exe=out/'render-benchmark'
subprocess.run(['xcrun','clang','-fobjc-arc','-fblocks','-mmacosx-version-min=11.0','-I'+str(root/'macos/gui'),'-I'+str(root/'macos/vendor/libvterm/include'),str(root/'tests/macos_render_benchmark.m'),str(obj/'MJEditorSession.o'),str(obj/'MJEditorView.o'),*map(str,sorted(obj.glob('vterm-*.o'))),'-framework','Cocoa','-o',str(exe)],check=True)
directory=tempfile.mkdtemp(prefix='sample-',dir=out);report=Path(sys.argv[1] if len(sys.argv)>1 else out/'report.json').resolve()
subprocess.run([str(exe),str(root/'build/macos/MiJoe.app/Contents/Resources/editor/bin/joe'),directory,str(report)],check=True,env=dict(os.environ,HOME=directory),timeout=60)
