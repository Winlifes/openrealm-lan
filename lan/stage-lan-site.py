#!/usr/bin/env python3
"""Copy the room UI and write the matching browser game's launcher."""
import argparse
import json
import os
import re
from pathlib import Path
import shutil

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('release')
parser.add_argument('--root', type=Path,
                    default=Path(os.environ.get('WC3_LAN_ROOT', Path.home()/'.local/share/wc3-lan')))
args = parser.parse_args()
if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', args.release):
    parser.error('Invalid release name')

root = args.root.resolve()
release = root/'browser-releases'/args.release
version_file = release/'version.json'
version = json.loads(version_file.read_text())
server_build = version['build']
if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', server_build):
    raise SystemExit('Invalid server build in browser release')
native = root/'engine-releases'/server_build/'bin/openwarcraft3'
web = root/'www'
linked_releases = web/'play/releases'
if (version.get('release', server_build) != args.release or
        not native.is_file() or linked_releases.resolve() != release.parent.resolve()):
    raise SystemExit('Stage the browser release and its matching native build first')

game = web/'play/game'
game.mkdir(parents=True, exist_ok=True)
for path in (Path(__file__).resolve().parent/'site').iterdir():
    if path.is_file():
        shutil.copy2(path, web/path.name)
(web/'index.html').write_text(
    '<!doctype html><meta charset="utf-8"><title>OpenRealm LAN</title>'
    '<meta http-equiv="refresh" content="0;url=rooms.html">'
    '<a href="rooms.html">Open multiplayer rooms</a>\n', encoding='utf-8')
(game/'index.html').write_text(
    '<!doctype html><meta charset="utf-8"><title>OpenRealm LAN</title>'
    f'<script>location.replace("../releases/{args.release}/"+location.search+location.hash)</script>'
    '<p>Loading multiplayer client…</p>\n', encoding='utf-8')
shutil.copy2(version_file, game/'version.json')
print(web/'rooms.html')
