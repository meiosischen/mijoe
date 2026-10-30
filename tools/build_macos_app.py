#!/usr/bin/env python3
"""Build the native AppKit MiJoe.app with a self-contained editor backend."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import shutil
import subprocess
import sys
import tempfile
from link_macos_command import ensure_link, setup_command

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--skip-core", action="store_true", help="Reuse the already built native terminal package")
parser.add_argument("--install", action="store_true", help="Install the app into ~/Applications")
parser.add_argument("--no-command-link", action="store_true", help="Explicitly build without checking /usr/local/bin/mj")
args = parser.parse_args()
if platform.system() != "Darwin":
    parser.error("Build this AppKit application on macOS")
arch = platform.machine()
package = ROOT / "build/macos" / ("MiJoe-macOS-" + arch)
if not args.skip_core:
    command = [sys.executable, str(ROOT / "tools/build_macos.py")]
    if args.no_command_link:
        command.append("--no-command-link")
    subprocess.run(command, cwd=ROOT, check=True)
info = json.loads((package / "build-info.json").read_text())
for name, sha in info["sources"].items():
    if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != sha:
        parser.error("Editor package is stale; rebuild without --skip-core: " + name)
if not args.no_command_link:
    try:
        ensure_link(package)
    except PermissionError:
        parser.exit(1, "Creating mj requires administrator authentication once. Run:\n" + setup_command(package) + "\n")
    except (OSError, ValueError, KeyError) as error:
        parser.error("mj verification failed: " + str(error))
objects = ROOT / "build/macos/app-objects"
objects.mkdir(parents=True, exist_ok=True)
app = ROOT / "build/macos/MiJoe.app"
processes = subprocess.check_output(["ps", "-axo", "command="], text=True, errors="replace")
if str(app / "Contents/MacOS/MiJoe") in processes:
    parser.error("Close the build-directory MiJoe application before rebuilding it")
contents = app / "Contents"
macos = contents / "MacOS"
resources = contents / "Resources"
macos.mkdir(parents=True, exist_ok=True)
resources.mkdir(parents=True, exist_ok=True)
vendor = ROOT / "macos/vendor/libvterm"
compiler = subprocess.check_output(["xcrun", "--find", "clang"], text=True).strip()
sdk = subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip()
base = [compiler, "-isysroot", sdk, "-O2", "-g", "-mmacosx-version-min=11.0", "-I" + str(vendor / "include")]
outputs = []
def compile_command(command, log):
    result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        log.flush()
        print((objects / "build.log").read_text(errors="replace")[-6000:])
        raise SystemExit(result.returncode)

with (objects / "build.log").open("w") as log:
    for source in sorted((vendor / "src").glob("*.c")):
        output = objects / ("vterm-" + source.stem + ".o")
        compile_command([*base, "-std=c99", "-c", str(source), "-o", str(output)], log)
        outputs.append(str(output))
    for source in sorted((ROOT / "macos/gui").glob("*.m")):
        output = objects / (source.stem + ".o")
        compile_command([*base, "-fobjc-arc", "-fblocks", "-Wall", "-Wextra", "-Wno-unused-parameter",
                         "-c", str(source), "-o", str(output)], log)
        outputs.append(str(output))
    compile_command([*base, *outputs, "-framework", "Cocoa", "-o", str(macos / "MiJoe")], log)
editor = resources / "editor"
for name in ("bin", "etc", "share"):
    shutil.copytree(package / name, editor / name, dirs_exist_ok=True)
shutil.copy2(package / "操作说明.md", resources / "Vim操作说明.md")
shutil.copy2(ROOT / "macos/README.md", resources / "macOS说明.md")
shutil.copy2(ROOT / "LICENSE", resources / "LICENSE")
shutil.copy2(vendor / "LICENSE", resources / "libvterm-LICENSE")
iconset = objects / "MiJoe.iconset"
iconset.mkdir(exist_ok=True)
logo = ROOT / "windows/src/exe/icon/joe_logo_100_256x.png"
for size in (16, 32, 128, 256, 512):
    for factor in (1, 2):
        output = iconset / (f"icon_{size}x{size}" + ("@2x" if factor == 2 else "") + ".png")
        subprocess.run(["sips", "-z", str(size * factor), str(size * factor), str(logo), "--out", str(output)], stdout=subprocess.DEVNULL, check=True)
subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(resources / "MiJoe.icns")], check=True)
plist = dict(CFBundleName="MiJoe", CFBundleDisplayName="MiJoe", CFBundleIdentifier="org.mijoe.MiJoe",
             CFBundleExecutable="MiJoe", CFBundlePackageType="APPL", CFBundleShortVersionString="1.0",
             CFBundleVersion="1", CFBundleIconFile="MiJoe.icns", NSPrincipalClass="NSApplication",
             LSMinimumSystemVersion="11.0", NSHighResolutionCapable=True, NSSupportsAutomaticGraphicsSwitching=True,
             CFBundleDocumentTypes=[dict(CFBundleTypeName="Text document", CFBundleTypeRole="Editor",
                                        LSHandlerRank="Alternate", LSItemContentTypes=["public.text", "public.data"])])
(contents / "Info.plist").write_bytes(plistlib.dumps(plist))
subprocess.run(["codesign", "--force", "--deep", "--sign", "-", str(app)], check=True)
subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True)
for binary in (macos / "MiJoe", editor / "bin/joe"):
    deps = subprocess.check_output(["otool", "-L", str(binary)], text=True)
    for line in deps.splitlines()[1:]:
        if not line.strip().startswith(("/usr/lib/", "/System/Library/")):
            parser.error("Unexpected external runtime dependency: " + line)
manifest = dict(architecture=arch, minimum_macos="11.0", backend=info,
                binary_sha256=hashlib.sha256((macos / "MiJoe").read_bytes()).hexdigest(),
                sources={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in sorted((ROOT / "macos/gui").glob("*")) if p.is_file()})
(ROOT / "build/macos/app-build-info.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
archive = ROOT / "build/deliveries" / ("mijoe-native-macos-" + arch + ".zip")
subprocess.run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(app), str(archive)], check=True)
installed = None
if args.install:
    installed = Path.home() / "Applications/MiJoe.app"
    if installed.exists():
        existing = plistlib.loads((installed / "Contents/Info.plist").read_bytes())
        if existing.get("CFBundleIdentifier") != "org.mijoe.MiJoe":
            parser.error("Refusing to replace an unrelated application: " + str(installed))
        # Do not replace resources underneath an editor with unsaved work.
        processes = subprocess.check_output(["ps", "-axo", "command="], text=True, errors="replace")
        if str(installed / "Contents/MacOS/MiJoe") in processes:
            parser.error("Close the installed MiJoe application before updating it")
    installed.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".mijoe-install-", dir=installed.parent) as temporary:
        stage = Path(temporary) / "MiJoe.app"
        previous = Path(temporary) / "previous.app"
        shutil.copytree(app, stage)
        if installed.exists():
            installed.rename(previous)
        try:
            stage.rename(installed)
        except OSError:
            if previous.exists():
                previous.rename(installed)
            raise
    subprocess.run(["codesign", "--verify", "--deep", "--strict", str(installed)], check=True)
print(json.dumps(dict(app=str(app), installed=str(installed) if installed else None, zip=str(archive)), ensure_ascii=False, indent=2))
