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
args = parser.parse_args()
if not re.fullmatch(r'[a-zA-Z0-9._-]+', args.release): parser.error('Invalid release name')
root = Path(os.environ.get('WC3_LAN_ROOT', Path.home() / '.local/share/wc3-lan')).resolve()
dev = root / 'development'
preview = args.preview_dir or dev / 'browser-preview'
target = root / 'browser-releases' / args.release
if target.exists(): raise FileExistsError(target)
engine_files = ['openwarcraft3.js', 'openwarcraft3.wasm', 'openwarcraft3.data']
files = ['index.html', 'wc3-input.js', 'wc3-movies.js', 'bootstrap.mpq', 'bootstrap-manifest.json',
         'assets.index.txt', 'playable-maps.index.txt']
for name in files:
    if not (preview / name).is_file(): raise FileNotFoundError(name)
for name in engine_files:
    if not (args.engine_build / name).is_file(): raise FileNotFoundError(args.engine_build / name)
bootstrap = json.loads((preview / 'bootstrap-manifest.json').read_text())
if hashlib.sha256((preview / 'bootstrap.mpq').read_bytes()).hexdigest() != bootstrap['sha256']:
    raise ValueError('Bootstrap hash mismatch')
target.mkdir(parents=True)
for name in files: shutil.copy2(preview / name, target / name)
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
