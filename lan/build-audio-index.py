#!/usr/bin/env python3
"""Resolve extensionless asset ids only when the supplied SD files are unique."""
from pathlib import Path
import os
from collections import defaultdict
import json

root = Path(os.environ.get('WC3_LAN_ROOT', Path.home() / '.local/share/wc3-lan')).resolve()
data = root / 'data-current'
candidates = defaultdict(list)
for path in data.rglob('*'):
    if path.is_file() and path.suffix.lower() in ('.ogg', '.wav', '.mp3', '.flac'):
        candidates[path.stem.lower()].append(path.relative_to(data).as_posix())
aliases = {name: paths[0] for name, paths in candidates.items() if len(paths) == 1}
output = root / 'development/src/games/warcraft-3/share/audio-assets.index'
output.write_text(''.join(f'{name}\t{path}\n' for name, path in sorted(aliases.items())))
report = {'aliases': len(aliases), 'ambiguous': {k: v for k, v in candidates.items() if len(v) > 1}}
(root / 'development/proof/audio-index.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
print(json.dumps({'aliases': len(aliases), 'ambiguous':len(report['ambiguous'])}))
