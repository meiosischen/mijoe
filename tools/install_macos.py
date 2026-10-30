#!/usr/bin/env python3
"""Install the native macOS package and mj/rmj/xmj command launchers."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import tempfile
from link_macos_command import ensure_link

ROOT = Path(__file__).resolve().parents[1]
MARKER = "# MiJoe command launcher; managed by tools/install_macos.py\n"


def atomic_copy(source, destination):
    destination = Path(destination)
    descriptor, temporary = tempfile.mkstemp(prefix=".mijoe-", dir=destination.parent)
    os.close(descriptor)
    try:
        shutil.copy2(source, temporary)
        os.replace(temporary, destination)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    return str(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path,
                        default=ROOT / "build/macos" / ("MiJoe-macOS-" + platform.machine()))
    parser.add_argument("--prefix", type=Path, default=Path("/usr/local"))
    args = parser.parse_args()
    package = args.package.resolve()
    prefix = args.prefix.resolve()
    payload = prefix / "libexec/mijoe"
    binaries = prefix / "bin"
    info = json.loads((package / "build-info.json").read_text())
    binary = package / "bin/joe"
    if hashlib.sha256(binary.read_bytes()).hexdigest() != info["binary_sha256"]:
        parser.error("Package binary differs from build-info.json; rebuild before installing")
    if not (package / "etc/joe/joerc").is_file() or not (package / "share/joe/colors").is_dir():
        parser.error("Package configuration or color resources are missing")
    if payload.is_symlink() or (payload.exists() and not (payload / "install-info.json").is_file()):
        parser.error("Refusing to replace an unrecognized installation: " + str(payload))
    for name in ("mj", "rmj", "xmj"):
        destination = binaries / name
        if name == "mj" and destination.is_symlink() and destination.resolve() == binary:
            continue
        if destination.is_symlink() or (destination.exists() and (
                not destination.is_file() or MARKER not in destination.read_text(errors="replace"))):
            parser.error("Refusing to replace an existing unrelated command: " + str(destination))
    binaries.mkdir(parents=True, exist_ok=True)
    payload.mkdir(parents=True, exist_ok=True)
    # Install application resources, without copying the user's practice file.
    for name in ("bin", "etc", "share"):
        shutil.copytree(package / name, payload / name, dirs_exist_ok=True, copy_function=atomic_copy)
    for name in ("build-info.json", "操作说明.md"):
        atomic_copy(package / name, payload / name)
    launchers = {"mj": "", "rmj": " -rdonly", "xmj": " -novim -hex"}
    for name, options in launchers.items():
        if name == "mj":
            ensure_link(package, binaries / name)
            continue
        script = ("#!/bin/sh\n" + MARKER + "set -eu\n"
                  + 'if [ "$#" -eq 0 ]; then set -- ""; fi\n'
                  + "exec " + shlex.quote(str(payload / "bin/joe")) + options + ' "$@"\n')
        descriptor, temporary = tempfile.mkstemp(prefix=".mijoe-", dir=binaries)
        try:
            with os.fdopen(descriptor, "w") as output:
                output.write(script)
            os.chmod(temporary, 0o755)
            os.replace(temporary, binaries / name)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)
    installed = dict(application="mijoe", architecture=info["architecture"],
                     binary_sha256=info["binary_sha256"], prefix=str(prefix),
                     mj_target=str(binary),
                     commands={name: str(binaries / name) for name in launchers})
    (payload / "install-info.json").write_text(json.dumps(installed, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(installed, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
