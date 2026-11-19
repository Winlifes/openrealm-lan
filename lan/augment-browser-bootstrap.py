#!/usr/bin/env python3
"""Preload the original score-screen media without changing any existing entry."""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
for name in ('data', 'base', 'base-manifest', 'library', 'output', 'report'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
manifest = json.loads(a.base_manifest.read_text())
digest = lambda body: hashlib.sha256(body).hexdigest()
assert digest(a.base.read_bytes()) == manifest['sha256']
lib = C.CDLL(str(a.library))
handle, u32 = C.c_void_p, C.c_uint32
for name, args in {
    'SFileOpenArchive': [C.c_char_p, u32, u32, C.POINTER(handle)],
    'SFileCreateArchive': [C.c_char_p, u32, u32, C.POINTER(handle)],
    'SFileCloseArchive': [handle],
    'SFileOpenFileEx': [handle, C.c_char_p, u32, C.POINTER(handle)],
    'SFileReadFile': [handle, handle, u32, C.POINTER(u32), handle],
    'SFileCloseFile': [handle],
    'SFileAddFileFromBuffer': [handle, C.c_char_p, handle, u32],
}.items():
    fn = getattr(lib, name); fn.argtypes = args; fn.restype = C.c_bool
lib.SFileGetFileSize.argtypes = [handle, C.POINTER(u32)]
lib.SFileGetFileSize.restype = u32

def read(archive, name):
    file = handle()
    assert lib.SFileOpenFileEx(archive, name.encode(), 0, C.byref(file)), name
    try:
        size = lib.SFileGetFileSize(file, None)
        body, count = C.create_string_buffer(size), u32()
        assert lib.SFileReadFile(file, body, size, C.byref(count), None) and count.value == size, name
        return body.raw
    finally:
        assert lib.SFileCloseFile(file)

archive = handle()
assert lib.SFileOpenArchive(str(a.base).encode(), 0, 0, C.byref(archive))
try:
    entries = {name: read(archive, name) for name in manifest['assets']}
finally:
    assert lib.SFileCloseArchive(archive)
for name, expected in manifest['assets'].items():
    assert digest(entries[name]) == expected, name
added = {}
for path in sorted((a.data / 'ui/glues/scorescreen').rglob('*')):
    if path.is_file():
        name = path.relative_to(a.data).as_posix().lower()
        body = path.read_bytes()
        if name in entries:
            assert entries[name] == body, name
        else:
            entries[name] = body; added[name] = digest(body)
assert added and sum(len(entries[name]) for name in added) < 100 * 1024 ** 2
assert not a.output.exists()
a.output.parent.mkdir(parents=True, exist_ok=True)
assert lib.SFileCreateArchive(str(a.output).encode(), 0, len(entries) + 1, C.byref(archive))
try:
    for name, body in entries.items():
        buffer = C.create_string_buffer(body)
        assert lib.SFileAddFileFromBuffer(archive, name.replace('/', '\\').encode(), buffer, len(body)), name
finally:
    assert lib.SFileCloseArchive(archive)
assert lib.SFileOpenArchive(str(a.output).encode(), 0, 0, C.byref(archive))
try:
    for name, body in entries.items():
        assert read(archive, name) == body, name
finally:
    assert lib.SFileCloseArchive(archive)
manifest.update(entries=len(entries), rawBytes=sum(map(len, entries.values())),
    bytes=a.output.stat().st_size, sha256=digest(a.output.read_bytes()),
    assets={name: digest(body) for name, body in entries.items()}, preloadedScoreScreenAssets=added)
a.report.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
print(json.dumps(dict(entries=len(entries), unchangedBaseEntries=len(entries)-len(added),
    scoreAssetsAdded=len(added), addedBytes=sum(len(entries[name]) for name in added),
    bytes=manifest['bytes'], sha256=manifest['sha256'])))
