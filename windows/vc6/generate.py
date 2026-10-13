#!/usr/bin/env python3
"""Regenerate checked-in VC6 inputs. Python is not needed to build them."""
from pathlib import Path
import re
import zlib
import html
import struct
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
PERSONALITIES = ("joe", "jmacs", "jstar", "jpico", "jice", "cjoe")


def write_bytes(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.is_file() or path.read_bytes() != content:
        path.write_bytes(content)


def write(name, content):
    write_bytes(HERE / name, content.replace("\r\n", "\n").replace("\n", "\r\n").encode("ascii"))


def relative(path):
    import os
    return os.path.relpath(path, HERE).replace("/", "\\")


def sources():
    variables = {
        "JoeExternalSrcDir": ROOT / "joe",
        "JoeSrcDir": ROOT / "windows/src/joe/win",
        "PuttyExternalSrcDir": ROOT / "windows/src/putty/putty",
        "PuttySrcDir": ROOT / "windows/src/putty/win",
    }
    result = []
    for group in ("joe", "putty", "common"):
        project = ROOT / "windows/src" / group
        filename = "joewincommon.vcxproj" if group == "common" else group + ".vcxproj"
        for item in ET.parse(project / filename).findall(".//{*}ClCompile[@Include]"):
            name = item.attrib["Include"].replace("\\", "/")
            for key, path in variables.items():
                name = name.replace("$(" + key + ")", path.as_posix())
            path = Path(name)
            if not path.is_absolute():
                path = project / path
            assert path.is_file(), path
            result.append((group, path))
    result.append(("common", ROOT / "windows/src/common/vc6.c"))
    return result


def resource_line(name, path):
    assert path.is_file(), path
    # VC6 RC treats quotes around a resource name as part of that name.
    return 'F:*%s RCDATA "%s"\n' % (name, relative(path).replace("\\", "\\\\"))


def resources():
    project = ET.parse(ROOT / "windows/src/configuration/configuration.vcxproj")
    variables = {
        "JoeConfDir": ROOT / "windows/rc",
        "JoeDocsDir": ROOT / "windows/docs",
        "JoeSyntaxDir": ROOT / "syntax",
        "JoeColorsDir": ROOT / "colors",
        "JoeWinColorsDir": ROOT / "windows/colors",
    }
    def resolve(item):
        name = item.attrib["Include"].replace("\\", "/")
        for key, path in variables.items():
            name = name.replace("$(" + key + ")", path.as_posix())
        path = Path(name)
        if not path.is_absolute():
            path = ROOT / "windows/src/configuration" / path
        assert path.is_file(), path
        return path

    def compressed(path):
        data = path.read_bytes()
        # Match the original JWDeflateFiles header and raw Deflate stream.
        compressor = zlib.compressobj(9, zlib.DEFLATED, -15)
        payload = b"\x05\x01" + compressor.compress(data) + compressor.flush()
        assert zlib.decompress(payload[2:], -15) == data
        target = HERE / "resources" / (path.name + ".dfl")
        write_bytes(target, payload)
        return target

    common = '#include "jwres.h"\n'
    for path in [ROOT / "windows/rc/ftyperc", ROOT / "windows/docs/license.txt",
                 ROOT / "windows/rc/joerc.zh_TW", ROOT / "po/zh_TW.po"]:
        common += resource_line(path.name, compressed(path))
    for tag in ("SyntaxFile", "ColorSchemeFile"):
        for item in project.findall(".//{*}" + tag + "[@Include]"):
            path = resolve(item)
            common += resource_line(path.name, compressed(path))
    write("builtins.rc", common)
    # VC6's resource compiler accepts ANSI source; the original dialog is UTF-16.
    dialog = (ROOT / "windows/src/common/resources.rc").read_text(encoding="utf-16")
    write("dialogs.rc", dialog)
    for personality in PERSONALITIES:
        text = '#define PERSONALITY %s\n#include "../src/exe/joewin.rc"\n' % personality
        text += resource_line("joerc", compressed(ROOT / "windows/rc" / ("joerc.zh_TW" if personality == "cjoe" else personality + "rc")))
        text += '#ifndef JOEWIN_DLL_FRONTEND\n#include "dialogs.rc"\n#include "builtins.rc"\n#endif\n'
        write(personality + ".rc", text)
    write("joedll.rc", '#define JOEWIN_DLL joewin.dll\n#include "../src/exe/joewin.rc"\n'
                      '#include "dialogs.rc"\n#include "builtins.rc"\n')
    # Keep the existing DIB icon images unchanged; VC6 RC cannot read PNG entries.
    icon = (ROOT / "windows/src/exe/joewin.ico").read_bytes()
    images = []
    for index in range(struct.unpack_from("<H", icon, 4)[0]):
        entry = struct.unpack_from("<BBBBHHII", icon, 6 + 16 * index)
        data = icon[entry[-1]:entry[-1] + entry[-2]]
        if data[:4] == b"\x28\x00\x00\x00" and 0 < entry[0] <= 48:
            images.append((entry[:6], data))
    assert images, "No VC6-compatible DIB icon entries"
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    payload = b""
    for entry, data in images:
        header += struct.pack("<BBBBHHII", *entry, len(data), offset)
        payload += data
        offset += len(data)
    write_bytes(HERE / "joewin-vc6.ico", header + payload)


def version():
    tree = ET.parse(ROOT / "windows/src/build/version.props")
    version = tree.find(".//{*}JoeWinVersion").text
    fields = [int(x) for x in version.split(".")]
    fields += [0] * (4 - len(fields))
    text = "/* Generated from ../src/build/version.props. */\n"
    text += "#ifndef __JWVERSION_H__\n#define __JWVERSION_H__\n"
    for name, value in zip(("MAJ", "MIN", "REV", "BLD"), fields):
        text += "#define JW_VERSION_%s %d\n" % (name, value)
    text += '#define JW_VERSION_STR "%s"\n' % ".".join(map(str, fields))
    text += '#define JW_VERSION_DESC "%s VC6 build (no pop-up terminals)"\n' % version
    text += '#define JW_SHORTVERSION "%s"\n' % version
    text += '#define VERSION "%s"\n' % version
    text += '#define JW_VERSION_BANNER "** Joe\'s Own Editor for Windows v%s (VC6) **"\n' % version
    text += '#define JW_COPYRIGHT "Copyright \\xa9 2018 John J. Jordan, and others (see About...)"\n'
    write("jwversion.h", text + "#endif\n")


def documents():
    # Keep offline help self-contained, including on machines without Python.
    target = HERE / "docs"
    target.mkdir(exist_ok=True)
    for name in ("man", "windows"):
        source = (ROOT / "docs" / (name + ".md")).read_text(encoding="utf-8")
        page = ('<html><head><meta http-equiv="Content-Type" content="text/html; charset=utf-8">'
                '<title>JOE 4.6 - ' + name + '</title>'
                '<style>body{max-width:70em;margin:2em auto;padding:0 1em;}'
                'pre{white-space:pre-wrap;word-wrap:break-word;font-size:14px;}</style>'
                '</head><body><p>Windows VC6 edition: pop-up terminals and F1-F4 are disabled.</p>'
                '<pre>' + html.escape(source) + '</pre></body></html>')
        write_bytes(target / (name + ".html"), page.encode("utf-8"))


def make_sources(entries):
    groups = {group: [] for group in ("joe", "putty", "common")}
    rules = []
    for group, path in entries:
        # Include the parent name to keep window.c, main.c, builtin.c etc. separate.
        obj = '$(OBJDIR)\\%s_%s_%s.obj' % (group, path.parent.name, path.stem)
        groups[group].append(obj)
        flags = {"joe": "$(JOEFLAGS)", "putty": "$(PUTTYFLAGS)", "common": "$(COMMONFLAGS)"}[group]
        rules.append('"%s": "%s" $(HEADERS)\n\t$(CC) $(CFLAGS) %s /Fo"$@" /c "%s"\n' %
                     (obj, relative(path), flags, relative(path)))
    text = "# Generated by generate.py from the existing Visual Studio source lists.\n"
    for group, objects in groups.items():
        text += group.upper() + "_OBJS = \\\n" + " \\\n".join('    "' + obj + '"' for obj in objects) + "\n\n"
    write("sources.mak", text + "\n".join(rules))


def ide(entries):
    # Keep the Developer Studio layout, including blank record separators.
    # The project name and configuration names must match joewin.dsp.
    text = '# Microsoft Developer Studio Project File - Name="joewin" - Package Owner=<4>\n'
    text += '# Microsoft Developer Studio Generated Build File, Format Version 6.00\n'
    text += '# ** DO NOT EDIT **\n\n'
    text += '# TARGTYPE "Win32 (x86) External Target" 0x0106\n\n'
    text += 'CFG=joewin - Win32 Release\n'
    text += '!MESSAGE This is not a valid makefile. To build this project using NMAKE,\n'
    text += '!MESSAGE run NMAKE /f "joewin.mak" CFG=Release or CFG=Debug.\n'
    text += '!MESSAGE \n!MESSAGE Possible choices for configuration are:\n!MESSAGE \n'
    for config in ("Release", "Debug"):
        text += '!MESSAGE "joewin - Win32 %s" (based on "Win32 (x86) External Target")\n' % config
    text += '!MESSAGE \n\n# Begin Project\n# PROP AllowPerConfigDependencies 0\n'
    text += '# PROP Scc_ProjName ""\n# PROP Scc_LocalPath ""\n\n'
    for index, config in enumerate(("Release", "Debug")):
        text += ('!IF' if index == 0 else '!ELSEIF') + '  "$(CFG)" == "joewin - Win32 %s"\n\n' % config
        properties = [
            ('Use_Debug_Libraries', str(int(config == "Debug"))),
            ('Output_Dir', '"..\\bin\\vc6\\%s"' % config),
            ('Intermediate_Dir', '"..\\bin\\vc6\\%s\\obj"' % config),
            ('Cmd_Line', '"nmake /nologo /f joewin.mak CFG=%s package"' % config),
            ('Rebuild_Opt', '"/a"'),
            ('Target_File', '"..\\bin\\vc6\\%s\\joe.exe"' % config),
            ('Bsc_Name', '""'),
            ('Target_Dir', '""'),
        ]
        for prefix in ("BASE ", ""):
            for name, value in properties:
                text += '# PROP %s%s %s\n' % (prefix, name, value)
        text += '\n'
    text += '!ENDIF\n\n# Begin Target\n\n# Name "joewin - Win32 Release"\n# Name "joewin - Win32 Debug"\n'
    text += '# Begin Group "Source Files"\n\n# PROP Default_Filter "c;h;rc;mak;bat"\n'
    paths = [path for group, path in entries]
    paths += [ROOT / "windows/src/exe/standalone/main.c", ROOT / "windows/src/exe/main.c",
              ROOT / "windows/src/exe/joedll/joedll.c"]
    for path in paths:
        text += '# Begin Source File\n\nSOURCE=%s\n# End Source File\n' % relative(path)
    text += '# End Group\n# End Target\n# End Project\n'
    write("joewin.dsp", text)
    write("joewin.dsw", '''Microsoft Developer Studio Workspace File, Format Version 6.00
# WARNING: DO NOT EDIT OR DELETE THIS WORKSPACE FILE!

###############################################################################

Project: "joewin"=.\\joewin.dsp - Package Owner=<4>

Package=<5>
{{{
}}}

Package=<4>
{{{
}}}

###############################################################################

Global:

Package=<5>
{{{
}}}

Package=<3>
{{{
}}}

###############################################################################

''')


if __name__ == "__main__":
    entries = sources()
    version()
    documents()
    resources()
    make_sources(entries)
    ide(entries)
    print("Generated VC6 project, source rules, and builtin resources (%d sources)." % len(entries))
