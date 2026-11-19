#!/usr/bin/env python3
"""Build a multiplayer catalog from explicit runtime audits and original W3I/WTS data."""
from pathlib import Path
import os
import argparse
import ctypes as C
import hashlib
import json
import re

root = Path(os.environ.get('WC3_LAN_ROOT', Path.home() / '.local/share/wc3-lan')).resolve()
dev = root / 'development'
data = root / 'data-current'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--library', type=Path, default=root / 'engine-current/lib/libmenu.dylib')
parser.add_argument('--audit', type=Path, action='append', help='Runtime result array; later inputs override matching paths')
parser.add_argument('--catalog', type=Path, default=dev / 'proof/map-catalog.jsonl')
parser.add_argument('--output', type=Path, default=dev / 'maps-catalog.json')
parser.add_argument('--browser-index', type=Path, default=dev / 'browser-preview/playable-maps.index.txt')
parser.add_argument('--compatibility-output', type=Path, default=dev / 'proof/map-compatibility.json')
parser.add_argument('--include-lua', action='store_true', help='Require a matching deployed Lua-capable runtime')
args = parser.parse_args()
lib = C.CDLL(str(args.library))
handle, u32 = C.c_void_p, C.c_uint32
for name, argument_types, result in [
    ('SFileOpenArchive', [C.c_char_p, u32, u32, C.POINTER(handle)], C.c_bool),
    ('SFileCloseArchive', [handle], C.c_bool),
    ('SFileOpenFileEx', [handle, C.c_char_p, u32, C.POINTER(handle)], C.c_bool),
    ('SFileGetFileSize', [handle, C.POINTER(u32)], u32),
    ('SFileReadFile', [handle, handle, u32, C.POINTER(u32), handle], C.c_bool),
    ('SFileCloseFile', [handle], C.c_bool),
]:
    fn = getattr(lib, name)
    fn.argtypes, fn.restype = argument_types, result


def read_entry(archive, name):
    file = handle()
    if not lib.SFileOpenFileEx(archive, name.encode(), 0, C.byref(file)):
        return None
    try:
        size = lib.SFileGetFileSize(file, None)
        if size > 16 * 1024 * 1024:
            raise ValueError('Map metadata exceeds 16 MiB')
        body, count = C.create_string_buffer(size), u32()
        if not lib.SFileReadFile(file, body, size, C.byref(count), None) or count.value != size:
            raise ValueError('Incomplete MPQ read')
        return body.raw
    finally:
        lib.SFileCloseFile(file)


audits = {}
for path in args.audit or [dev / 'proof/map-audit/results.json']:
    rows = json.loads(path.read_text())
    if not isinstance(rows, list): raise ValueError('Audit must contain a result array: ' + str(path))
    for row in rows: audits[row['map']] = row
maps, rejected = [], []
for line in args.catalog.read_text().splitlines():
    item = json.loads(line)
    # Scenario maps may reserve noncontiguous players for humans and computers.
    # A computer slot does not make a single-player scenario multiplayer.
    if not 2 <= sum(not slot['computer'] for slot in item['slots']) <= len(item['slots']) <= 12:
        continue
    audit = audits.get(item['path'])
    archive = handle()
    if not lib.SFileOpenArchive(str(data / item['path']).encode(), 0, 0, C.byref(archive)):
        raise ValueError('Cannot open ' + item['path'])
    try:
        jass = read_entry(archive, 'war3map.j') or read_entry(archive, 'Scripts\\war3map.j')
        lua = read_entry(archive, 'war3map.lua') or read_entry(archive, 'Scripts\\war3map.lua')
        wts = (read_entry(archive, 'war3map.wts') or b'').decode('utf-8-sig')
    finally:
        lib.SFileCloseArchive(archive)
    language = 'Lua' if lua and not jass else 'JASS'
    if not audit or not audit['passed'] or not (jass or args.include_lua and lua):
        rejected.append(dict(path=item['path'], language='Lua' if lua and not jass else 'JASS',
                             reason=['Lua runtime not enabled'] if lua and not jass and not args.include_lua
                             else (audit or {}).get('errors', ['Not audited']),
                             exitCode=(audit or {}).get('exitCode')))
        continue
    strings = {int(m[0]): m[1].strip() for m in re.findall(
        r'(?ms)^STRING\s+(\d+)\s*\r?\n\s*\{\s*\r?\n(.*?)^\}\s*$', wts)}
    title = item['name']
    if title.startswith('TRIGSTR_'):
        title = strings.get(int(title[8:]), '')
    title = re.sub(r'\|c[0-9a-fA-F]{8}|\|r', '', title).strip()
    if not title:
        title = re.sub(r'^\(\d+\)', '', Path(item['path']).stem)
    item['name'] = title + (' · 冰封王座' if '/frozenthrone/' in item['path'] else ' · 混乱之治')
    item['id'] = hashlib.sha256(item['path'].encode()).hexdigest()[:16]
    item['language'] = language
    maps.append(item)

maps.sort(key=lambda x: (len(x['slots']), x['name'], x['path']))
args.output.write_text(json.dumps(maps, ensure_ascii=False, indent=2) + '\n')
args.browser_index.write_text('\n'.join(x['path'] for x in maps) + '\n')
counts = dict(playableMultiplayer=len(maps), playableMelee=sum(bool(x['flags'] & 4) for x in maps),
              playableScenario=sum(not x['flags'] & 4 for x in maps))
args.compatibility_output.write_text(json.dumps(dict(**counts, rejected=rejected), ensure_ascii=False, indent=2) + '\n')
print(json.dumps(dict(**counts, rejected=len(rejected), samples=[x['name'] for x in maps[:4]]), ensure_ascii=False))
