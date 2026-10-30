# Native display dependency

`libvterm/` contains the unmodified C sources and public headers from the
official libvterm 0.3.3 release, plus its license and build metadata.
Only the macOS AppKit frontend links these sources. The JOE editor, UNIX
terminal targets and Windows/VC6 products have no libvterm dependency.

Upstream: <https://www.leonerd.org.uk/code/libvterm/>

Release URL and SHA-256 are pinned in `libvterm/UPSTREAM.json`.
The library is built statically, with no Homebrew runtime dependency.
The MIT license is preserved in `libvterm/LICENSE` and copied into MiJoe.app.
