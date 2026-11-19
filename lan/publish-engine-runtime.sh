#!/bin/bash
set -euo pipefail
version=${1:?usage: publish-engine-runtime.sh VERSION}
[[ "$version" =~ ^[a-zA-Z0-9._-]+$ ]] || exit 2
root="${WC3_LAN_ROOT:-$HOME/.local/share/wc3-lan}"
source_tree="${WC3_ENGINE_SOURCE:-$root/development/src}"
source_build="${WC3_NATIVE_BUILD:-$source_tree/build/classic}"
target="$root/engine-releases/$version"
test ! -e "$target"
mkdir -p "$target/bin" "$target/lib" "$target/share"
cp -p "$source_build/bin/openwarcraft3" "$target/bin/"
rsync -a --exclude='*test*' "$source_build/lib/" "$target/lib/"
rsync -a "$source_build/share/" "$target/share/"
shasum -a 256 "$target/bin/openwarcraft3" > "$target/engine.sha256"
if [[ "${WC3_ACTIVATE_ENGINE:-1}" != 1 ]]; then
  echo "Staged immutable engine runtime $version"
  exit 0
fi
ln -s "engine-releases/$version" "$root/engine-current.next"
python3 - "$root" <<'PY'
import os, sys
from pathlib import Path
root = Path(sys.argv[1])
os.replace(root / 'engine-current.next', root / 'engine-current')
PY
echo "Installed immutable engine runtime $version"
