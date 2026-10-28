#!/usr/bin/env python3
"""Check build architecture, subsystem and known post-XP static imports.

This checks PE metadata, not actual loading on Windows XP.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys

POST_XP = {"CancelIoEx", "GetTickCount64", "GetFileInformationByHandleEx",
           "InitializeCriticalSectionEx", "InitializeConditionVariable",
           "SetThreadErrorMode", "GetSystemTimePreciseAsFileTime"}


def inspect(path):
    data = path.read_bytes()
    pe = struct.unpack_from("<I", data, 60)[0]
    assert data[pe:pe+4] == b"PE\0\0", path
    machine, sections = struct.unpack_from("<HH", data, pe + 4)
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    assert machine == 0x14C and magic == 0x10B, f"Not PE32/i386: {path}"
    version = struct.unpack_from("<HH", data, optional + 48)
    assert version <= (5, 1), f"Subsystem exceeds XP: {path} {version}"
    table = optional + optional_size

    def offset(rva):
        for n in range(sections):
            size, address, raw_size, raw = struct.unpack_from("<IIII", data, table + n*40 + 8)
            if address <= rva < address + max(size, raw_size):
                return raw + rva - address
        raise ValueError(f"Unmapped RVA {rva:x}")

    def string(rva):
        start = offset(rva)
        return data[start:data.index(b"\0", start)].decode("ascii")

    imports = {}
    rva, _ = struct.unpack_from("<II", data, optional + 104)
    position = offset(rva)
    while any(struct.unpack_from("<IIIII", data, position)):
        original, _, _, name, first = struct.unpack_from("<IIIII", data, position)
        module = string(name)
        thunk = offset(original or first)
        names = []
        while True:
            value = struct.unpack_from("<I", data, thunk)[0]
            if not value:
                break
            names.append("#" + str(value & 0xFFFF) if value & 0x80000000 else string(value + 2))
            thunk += 4
        imports[module] = names
        position += 20
    rejected = sorted(POST_XP.intersection(name for names in imports.values() for name in names))
    runtime = [name for name in imports if name.lower().startswith(("msvcr", "vcruntime", "api-ms-win-"))
               or name.lower() == "ucrtbase.dll"]
    assert not rejected and not runtime, f"Unexpected hard imports: {path}: {rejected} {runtime}"
    return {"path": str(path), "architecture": "PE32/i386", "subsystem": list(version),
            "sha256": hashlib.sha256(data).hexdigest(), "imports": imports}


if __name__ == "__main__":
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "windows/bin/vc6")
    paths = sorted(p for p in root.rglob("*") if p.suffix.lower() in (".exe", ".dll") and "obj" not in p.parts)
    assert paths, "No binaries found"
    print(json.dumps([inspect(path) for path in paths], indent=2))
