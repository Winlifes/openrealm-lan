#!/usr/bin/env bash
set -euo pipefail
source_tree="${WC3_ENGINE_SOURCE:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../engine" && pwd)}"
command -v emcc >/dev/null || { echo "Activate Emscripten SDK 4.0.23 first" >&2; exit 2; }
command -v emar >/dev/null || { echo "Emscripten archiver is unavailable" >&2; exit 2; }
cd "$source_tree"
make EMSCRIPTEN=1 EM_FS=browser GL_BACKEND=gles3 BUILD=release -j"${WC3_BUILD_JOBS:-4}" \
  BIN_DIR=build/em-web/bin LIB_DIR=build/em/lib SHARE_INSTALL=build/em-web/share \
  AR=emar EM_PRELOAD='--preload-file build/em-web/share@/share' build/em-web/bin/openwarcraft3.js
