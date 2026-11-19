#!/usr/bin/env python3
"""Pack exact source assets and map UI metadata; never change playable maps."""
from pathlib import Path
import os
import ctypes as C
import hashlib
import json
import urllib.parse

root = Path(os.environ.get('WC3_LAN_ROOT', Path.home() / '.local/share/wc3-lan')).resolve()
data = root / 'data-current'
dev = root / 'development'
output = dev / 'browser-preview'
staging = dev / 'map-ui-cache'
library = Path(os.environ.get('WC3_MPQ_LIBRARY', dev / 'src/build/classic/lib/libmenu.dylib'))
lib = C.CDLL(str(library.resolve()))
handle = C.c_void_p
u32 = C.c_uint32
signatures = {
    'SFileOpenArchive': ([C.c_char_p, u32, u32, C.POINTER(handle)], C.c_bool),
    'SFileCreateArchive': ([C.c_char_p, u32, u32, C.POINTER(handle)], C.c_bool),
    'SFileCloseArchive': ([handle], C.c_bool),
    'SFileOpenFileEx': ([handle, C.c_char_p, u32, C.POINTER(handle)], C.c_bool),
    'SFileGetFileSize': ([handle, C.POINTER(u32)], u32),
    'SFileReadFile': ([handle, handle, u32, C.POINTER(u32), handle], C.c_bool),
    'SFileCloseFile': ([handle], C.c_bool),
    'SFileAddFileFromBuffer': ([handle, C.c_char_p, handle, u32], C.c_bool),
}
for name, (args, result) in signatures.items():
    fn = getattr(lib, name)
    fn.argtypes, fn.restype = args, result


def read_entry(archive, name):
    file = handle()
    if not lib.SFileOpenFileEx(archive, name.encode(), 0, C.byref(file)):
        return None
    try:
        size = lib.SFileGetFileSize(file, None)
        buffer, count = C.create_string_buffer(size), u32()
        assert lib.SFileReadFile(file, buffer, size, C.byref(count), None)
        assert count.value == size
        return buffer.raw
    finally:
        lib.SFileCloseFile(file)


def pack(path, entries):
    path.parent.mkdir(parents=True, exist_ok=True)
    archive = handle()
    assert lib.SFileCreateArchive(str(path).encode(), 0, len(entries) + 1, C.byref(archive))
    try:
        for name, body in entries.items():
            buffer = C.create_string_buffer(body)
            assert lib.SFileAddFileFromBuffer(archive, name.replace('/', '\\').encode(), buffer, len(body)), name
    finally:
        assert lib.SFileCloseArchive(archive)


names = set()
log = root / 'log/web.log'
for line in (log.read_text(errors='replace').splitlines() if log.is_file() else []):
    try:
        event = json.loads(line)
    except ValueError:
        continue
    request = event.get('request', {})
    uri = request.get('uri', '')
    if event.get('status') != 200 or request.get('method') != 'GET' or not uri.startswith('/play/gamedata/'):
        continue
    name = urllib.parse.unquote(uri.split('?', 1)[0].removeprefix('/play/gamedata/')).lower().removesuffix('.b64')
    if not name.startswith(('maps/', 'campaign/')) and (data / name).is_file():
        names.add(name)
for prefix in ('ui', 'units', 'scripts', 'doodads'):
    for file in (data / prefix).rglob('*'):
        if file.suffix.lower() in ('.txt', '.slk', '.fdf', '.j', '.ai', '.wai', '.toc', '.json'):
            names.add(str(file.relative_to(data)))
for font in ('arheigb_bd.ttf', 'blq55web.ttf', 'blq85web.ttf', 'frizqt__.ttf'):
    names.add('fonts/' + font)

entries = {name: (data / name).read_bytes() for name in sorted(names)}
maps = (data / 'maps.index.txt').read_text().splitlines()
metadata_names = []
for name in maps:
    archive = handle()
    assert lib.SFileOpenArchive(str(data / name).encode(), 0, 0, C.byref(archive)), name
    try:
        parts = {}
        for part in ('war3map.w3i', 'war3map.wts', 'war3mapPreview.tga', 'war3mapMap.blp', 'war3mapMap.tga'):
            body = read_entry(archive, part)
            if body is not None:
                parts[part] = body
        assert 'war3map.w3i' in parts, name
        parts['source.sha256'] = hashlib.sha256((data / name).read_bytes()).hexdigest().encode()
    finally:
        lib.SFileCloseArchive(archive)
    cache_name = name + '.ui.mpq'
    target = staging / cache_name
    pack(target, parts)
    entries[cache_name] = target.read_bytes()
    metadata_names.append(cache_name)

audio_index = dev / 'src/games/warcraft-3/share/audio-assets.index'
if audio_index.exists(): entries['audio-assets.index'] = audio_index.read_bytes()
pack(output / 'bootstrap.mpq', entries)
source_names = sorted(str(p.relative_to(data)).lower() for p in data.rglob('*') if p.is_file())
(output / 'assets.index.txt').write_text('\n'.join(source_names) + '\n')
manifest = dict(source='Warcraft III 3.0.1.24371 zhCN SD', entries=len(entries),
                rawBytes=sum(map(len, entries.values())), maps=len(metadata_names),
                bytes=(output / 'bootstrap.mpq').stat().st_size,
                sha256=hashlib.sha256((output / 'bootstrap.mpq').read_bytes()).hexdigest(),
                assets={name: hashlib.sha256(body).hexdigest() for name, body in entries.items()})
(output / 'bootstrap-manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
print(json.dumps({key: value for key, value in manifest.items() if key != 'assets'}), flush=True)
