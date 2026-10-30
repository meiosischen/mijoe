#!/usr/bin/env python3
"""Keep mj linked to the validated, relocatable macOS TUI build."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MARKER = "# MiJoe command launcher; managed by tools/install_macos.py\n"


def ensure_link(package, command=Path("/usr/local/bin/mj")):
    package = Path(package).resolve()
    # Do not resolve command itself: it may already be the desired symlink.
    command = Path(os.path.abspath(command))
    binary = package / "bin/joe"
    info = json.loads((package / "build-info.json").read_text())
    if hashlib.sha256(binary.read_bytes()).hexdigest() != info["binary_sha256"]:
        raise ValueError("TUI binary differs from build-info.json; rebuild before linking")
    if not os.access(binary, os.X_OK) or not (package / "etc/joe/joerc").is_file() or not (package / "share/joe/colors").is_dir():
        raise ValueError("TUI executable or configuration resources are missing")
    if command.is_symlink():
        if command.resolve() == binary:
            return str(binary)
        # A link from this checkout may be refreshed after an architecture change.
        previous = Path(os.path.abspath(command.parent / os.readlink(command)))
        managed = previous.parent.name == "bin" and previous.name == "joe" and previous.parent.parent.parent == ROOT / "build/macos"
        if not managed:
            raise ValueError("Refusing to replace an unrelated symlink: " + str(command))
    elif command.exists():
        if not command.is_file() or MARKER not in command.read_text(errors="replace"):
            raise ValueError("Refusing to replace an unrelated command: " + str(command))
    command.parent.mkdir(parents=True, exist_ok=True)
    # Atomic replacement also keeps an existing managed launcher intact on failure.
    with tempfile.TemporaryDirectory(prefix=".mijoe-link-", dir=command.parent) as temporary:
        link = Path(temporary) / "mj"
        link.symlink_to(binary)
        os.replace(link, command)
    if not command.is_symlink() or command.resolve() != binary:
        raise ValueError("mj link verification failed")
    return str(binary)


def setup_command(package, command=Path("/usr/local/bin/mj")):
    return shlex.join(["sudo", sys.executable, str(Path(__file__).resolve()),
                       "--package", str(Path(package).resolve()), "--command", str(command)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=ROOT / "build/macos" / ("MiJoe-macOS-" + platform.machine()))
    parser.add_argument("--command", type=Path, default=Path("/usr/local/bin/mj"))
    args = parser.parse_args()
    try:
        target = ensure_link(args.package, args.command)
    except PermissionError:
        parser.exit(1, "Creating mj requires administrator authentication once. Run:\n" + setup_command(args.package, args.command) + "\n")
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, str(error) + "\n")
    print(json.dumps({"command": str(args.command), "target": target}, indent=2))


if __name__ == "__main__":
    main()
