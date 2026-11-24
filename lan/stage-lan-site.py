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
native = root/'engine-releases'/args.release/'bin/openwarcraft3'
web = root/'www'
linked_releases = web/'play/releases'
if (json.loads((release/'version.json').read_text())['build'] != args.release or
        not native.is_file() or linked_releases.resolve() != release.parent.resolve()):
    raise SystemExit('Stage the native and browser runtimes under the same release name first')

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
print(web/'rooms.html')
