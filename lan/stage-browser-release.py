#!/usr/bin/env python3
"""Freeze one browser build, including bootstrap and map catalog, before publishing."""
from pathlib import Path
import os
import argparse, hashlib, json, re, shutil

parser = argparse.ArgumentParser()
parser.add_argument('release')
parser.add_argument('--engine-build', type=Path, required=True,
                    help='Exact Emscripten bin directory produced by build-browser-engine.sh')
parser.add_argument('--preview-dir', type=Path, help='Prepared HTML and bootstrap directory')
parser.add_argument('--asset-index', type=Path, help='Manifest of files in the served data mirror, if it is a subset')
parser.add_argument('--preload-index', type=Path, help='Optional JSON listing the measured lobby and per-map startup assets')
args = parser.parse_args()
if not re.fullmatch(r'[a-zA-Z0-9._-]+', args.release): parser.error('Invalid release name')
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
if not set(preload['maps']) <= playable_maps:
    raise ValueError('Preload index names an unlisted playable map')
for group in [preload['lobby'], *preload['maps'].values()]:
    if not set(group) <= served_paths:
        raise ValueError('Preload index names an asset absent from the served data mirror')
target.mkdir(parents=True)
for name in files: shutil.copy2(preview / name, target / name)
for name in shared_files: shutil.copy2(shared / name, target / name)
shutil.copy2(asset_index, target / 'assets.index.txt')
(target / 'preload.index.json').write_text(json.dumps(preload, ensure_ascii=False) + '\n', encoding='utf-8')
for name in engine_files:
    source = args.engine_build / name
    shutil.copy2(source, target / name)
    if source.read_bytes() != (target / name).read_bytes():
        raise ValueError('Compiled engine copy mismatch: ' + name)
(target / 'version.json').write_text(json.dumps(dict(build=args.release)) + '\n')
for name in ['NOTICE.md', 'LICENSE']:
    file = preview / name
    if name == 'LICENSE' and not file.exists(): file = dev / 'src/LICENSE'
    if file.exists(): shutil.copy2(file, target / name)
manifest = {p.name: dict(bytes=p.stat().st_size, sha256=hashlib.sha256(p.read_bytes()).hexdigest())
            for p in sorted(target.iterdir()) if p.is_file()}
(target / 'release-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
play = root / 'www/play'
play.mkdir(parents=True, exist_ok=True)
link = play / 'releases'
if not link.exists(): link.symlink_to(root / 'browser-releases', target_is_directory=True)
print(json.dumps(dict(release=args.release, files=len(manifest), bootstrapBytes=bootstrap['bytes'],
                      engineBuild=str(args.engine_build.resolve()),
                      engineArtifacts={name: manifest[name] for name in engine_files})))
