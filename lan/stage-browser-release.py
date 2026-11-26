#!/usr/bin/env python3
"""Freeze one browser build, including bootstrap and map catalog, before publishing."""
from pathlib import Path, PurePosixPath
import os
import argparse, gzip, hashlib, json, re, shutil, struct, tempfile

parser = argparse.ArgumentParser()
parser.add_argument('release')
parser.add_argument('--server-build', help='Native build for a wire-compatible browser-only update')
parser.add_argument('--engine-build', type=Path, required=True,
                    help='Exact Emscripten bin directory produced by build-browser-engine.sh')
parser.add_argument('--preview-dir', type=Path, help='Prepared HTML and bootstrap directory')
parser.add_argument('--asset-index', type=Path, help='Manifest of files in the served data mirror, if it is a subset')
parser.add_argument('--preload-index', type=Path, help='Optional JSON listing the measured lobby and per-map startup assets')
parser.add_argument('--data-dir', type=Path, help='Build startup bundles and check map archives against this served data mirror')
args = parser.parse_args()
version_name = r'[A-Za-z0-9][A-Za-z0-9._-]*'
server_build = args.server_build or args.release
if not re.fullmatch(version_name, args.release) or not re.fullmatch(version_name, server_build):
    parser.error('Invalid browser release or server build')
root = Path(os.environ.get('WC3_LAN_ROOT', Path.home() / '.local/share/wc3-lan')).resolve()
dev = root / 'development'
preview = args.preview_dir or dev / 'browser-preview'
target = root / 'browser-releases' / args.release
if target.exists(): raise FileExistsError(target)
engine_files = ['openwarcraft3.js', 'openwarcraft3.wasm', 'openwarcraft3.data']
files = ['index.html', 'wc3-input.js', 'wc3-movies.js', 'bootstrap.mpq', 'bootstrap-manifest.json',
         'playable-maps.index.txt']
shared = Path(__file__).resolve().parent.parent / 'engine/port/web/site'
shared_files = ['fullscreen-resize.js', 'early-map-loading.js']
browser = Path(__file__).resolve().parent / 'browser'
browser_helpers = ['wc3-room-assets.js', 'wc3-chat-input.js']
asset_index = args.asset_index or preview / 'assets.index.txt'
if not asset_index.is_file(): raise FileNotFoundError(asset_index)
for name in files:
    if not (preview / name).is_file(): raise FileNotFoundError(name)
for name in shared_files:
    if not (shared / name).is_file(): raise FileNotFoundError(shared / name)
for name in engine_files:
    if not (args.engine_build / name).is_file(): raise FileNotFoundError(args.engine_build / name)
bootstrap = json.loads((preview / 'bootstrap-manifest.json').read_text())
if hashlib.sha256((preview / 'bootstrap.mpq').read_bytes()).hexdigest() != bootstrap['sha256']:
    raise ValueError('Bootstrap hash mismatch')
preload = json.loads(args.preload_index.read_text()) if args.preload_index else {'lobby': [], 'maps': {}}
served_paths = set(asset_index.read_text().splitlines())
playable_maps = set((preview / 'playable-maps.index.txt').read_text().splitlines())
extra = preload.get('extra', {})
extra_lobby = extra.get('lobby', [])
extra_maps = extra.get('maps', {})
if not set(preload['maps']).union(extra_maps) <= playable_maps:
    raise ValueError('Preload index names an unlisted playable map')
if extra and not args.data_dir:
    raise ValueError('Supplemental preloads require a data directory')
for group in [preload['lobby'], *preload['maps'].values(), extra_lobby, *extra_maps.values()]:
    if not set(group) <= served_paths:
        raise ValueError('Preload index names an asset absent from the served data mirror')
retained = preload.get('retainAfterLoad', {})
for name, paths in retained.items():
    if name not in preload['maps'] or not set(paths) <= set(preload['lobby'] + preload['maps'][name]):
        raise ValueError('Retained assets must be included in the selected map or lobby preload')


def describe(path):
    digest = hashlib.sha256()
    with path.open('rb') as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b''):
            digest.update(chunk)
    return dict(bytes=path.stat().st_size, sha256=digest.hexdigest())


def asset_source(path):
    relative = PurePosixPath(path)
    if relative.is_absolute() or '..' in relative.parts or '\\' in path or relative.as_posix() != path:
        raise ValueError('Invalid asset path: ' + path)
    return args.data_dir / relative


def write_pack(directory, name, paths):
    if not paths:
        return None
    with (directory / name).open('wb') as bundle:
        for path in paths:
            source = asset_source(path)
            if not source.is_file() and source.suffix == '.blp':
                source = source.with_suffix('.dds')
            size = source.stat().st_size
            if not 0 < size < 2**32:
                raise ValueError('Invalid bundled asset size: ' + path)
            raw = source.read_bytes()
            compressed = gzip.compress(raw, compresslevel=4, mtime=0)
            stored = compressed if len(compressed) < len(raw) else raw
            bundle.write(struct.pack('<II', len(stored), len(raw)))
            bundle.write(stored)
    return name


def pack_room_audio(directory, lobby, maps):
    formats = {'.wav', '.ogg', '.mp3', '.flac'}
    common_roots = ('sound/interface/', 'sound/time/', 'sound/buildings/', 'sound/units/', 'abilities/weapons/')
    audio_by_directory = {}
    common = set()
    for name in served_paths:
        path = PurePosixPath(name)
        if path.suffix not in formats:
            continue
        audio_by_directory.setdefault(path.parent, set()).add(name)
        if name.startswith(common_roots):
            common.add(name)

    def model_audio(paths):
        sounds = set()
        for name in paths:
            path = PurePosixPath(name)
            if path.suffix in {'.mdx', '.mdl'}:
                sounds.update(audio_by_directory.get(path.parent, ()))
        return sounds

    common.update(model_audio(lobby))
    packed_assets = set(lobby).union(*maps.values())
    shared = sorted(common - packed_assets)
    shared_pack = write_pack(directory, 'room-audio-shared.bin', shared)
    map_packs = {}
    for name, paths in maps.items():
        files = sorted((common | model_audio(paths)).difference(shared, lobby, paths))
        if files:
            key = hashlib.sha256(name.encode()).hexdigest()[:16]
            pack = write_pack(directory, f'room-audio-{key}.bin', files)
            map_packs[name] = {'pack': pack, 'files': files}
    return {'shared': {'pack': shared_pack, 'files': shared} if shared else None, 'maps': map_packs}


def pack_startup_assets(directory):
    lobby = list(dict.fromkeys(preload['lobby']))
    shared_paths = set(lobby)
    maps = {name: [path for path in dict.fromkeys(paths) if path not in shared_paths]
            for name, paths in sorted(preload['maps'].items())}
    archives = {}
    for name in maps:
        archive = asset_source(name)
        if name not in served_paths or not archive.is_file() or not archive.stat().st_size:
            raise ValueError('Playable map is absent from the served data mirror: ' + name)
        archives[name] = describe(archive)
    packs = {'lobby': write_pack(directory, 'room-lobby.bin', lobby), 'maps': {}}
    for name, paths in maps.items():
        key = hashlib.sha256(name.encode()).hexdigest()[:16]
        packs['maps'][name] = write_pack(directory, f'room-map-{key}.bin', paths)
    plan = dict(lobby=lobby, maps=maps, packs=packs, archives=archives)
    if retained:
        plan['retainAfterLoad'] = retained
    audio = pack_room_audio(directory, lobby, maps)
    if audio['shared'] or audio['maps']:
        plan['audio'] = audio
    if extra:
        shared = list(dict.fromkeys(extra_lobby))
        extra_plans = {}
        for name, paths in sorted(extra_maps.items()):
            unique = list(dict.fromkeys(paths))
            if unique:
                key = hashlib.sha256(name.encode()).hexdigest()[:16]
                extra_plans[name] = {'pack': write_pack(directory, f'room-extra-{key}.bin', unique), 'files': unique}
        plan['extra'] = {
            'shared': {'pack': write_pack(directory, 'room-extra-shared.bin', shared), 'files': shared} if shared else None,
            'maps': extra_plans,
        }
    return plan


if args.data_dir and set(preload['maps']) != playable_maps:
    raise ValueError('A packed release needs a preload list for every playable map')
target.parent.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix=f'.{args.release}-', dir=target.parent) as staging_path:
    staging = Path(staging_path)
    for name in files: shutil.copy2(preview / name, staging / name)
    for name in shared_files: shutil.copy2(shared / name, staging / name)
    for name in browser_helpers: shutil.copy2(browser / name, staging / name)
    shutil.copy2(asset_index, staging / 'assets.index.txt')
    map_ids = {hashlib.sha256(name.encode()).hexdigest()[:16]: name for name in sorted(playable_maps)}
    (staging / 'room-maps.index.json').write_text(json.dumps(map_ids, ensure_ascii=False) + '\n', encoding='utf-8')
    plan = pack_startup_assets(staging) if args.data_dir else preload
    (staging / 'preload.index.json').write_text(json.dumps(plan, ensure_ascii=False) + '\n', encoding='utf-8')
    for name in engine_files:
        shutil.copy2(args.engine_build / name, staging / name)
    (staging / 'version.json').write_text(json.dumps(dict(build=server_build, release=args.release)) + '\n')
    for name in ['NOTICE.md', 'LICENSE']:
        file = preview / name
        if name == 'LICENSE' and not file.exists(): file = dev / 'src/LICENSE'
        if file.exists(): shutil.copy2(file, staging / name)
    manifest = {p.name: describe(p) for p in sorted(staging.iterdir()) if p.is_file()}
    for name in engine_files:
        if describe(args.engine_build / name) != manifest[name]:
            raise ValueError('Compiled engine copy mismatch: ' + name)
    (staging / 'release-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    staging.chmod(0o755)
    staging.rename(target)
play = root / 'www/play'
play.mkdir(parents=True, exist_ok=True)
link = play / 'releases'
if not link.exists(): link.symlink_to(root / 'browser-releases', target_is_directory=True)
print(json.dumps(dict(release=args.release, files=len(manifest), bootstrapBytes=bootstrap['bytes'],
                      engineBuild=str(args.engine_build.resolve()),
                      engineArtifacts={name: manifest[name] for name in engine_files})))
