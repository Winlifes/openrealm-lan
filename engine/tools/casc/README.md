# WC3 CASC → MPQ data tools (rgnv fork, wasm-port)

Prep tools for turning the modern WC3 CASC install into engine-readable MPQ sets.

## Build

```bash
# deps (Debian): zlib1g-dev libtomcrypt-dev libtommath-dev libbz2-dev cmake g++
git clone --depth 1 https://github.com/ladislav-zezula/CascLib.git
git clone --depth 1 https://github.com/ladislav-zezula/StormLib.git
# CascLib current master needs a one-line Linux fix: s/LPDWORD/PDWORD/ in src/CascFiles.cpp
cmake -B CascLib/build -S CascLib -DCMAKE_BUILD_TYPE=Release && cmake --build CascLib/build -j
cmake -B StormLib/build -S StormLib -DCMAKE_BUILD_TYPE=Release && cmake --build StormLib/build -j

g++ -O2 -std=c++17 wc3casc.cpp -I CascLib/src  -L CascLib/build  -lcasc -o wc3casc
g++ -O2 -std=c++17 wc3mpq.cpp  -I StormLib/src -L StormLib/build -lstorm -ltomcrypt -ltommath -lbz2 -lz -o wc3mpq
```

## wc3casc — read/extract from the CASC tree

```
wc3casc list <tree> [outListFile]           # enumerate (writes <size>	<name> lines)
wc3casc extract <tree> <name> <outFile>
wc3casc extract-list <tree> <nameFile> <outDir>   # batch; names mapped ':'/'' -> '/'
```

- `tree` = the install dir containing `.build.info` (your Warcraft III installation).
- Names use the `war3.w3mod:<path>` virtual-prefix scheme (backslashes).
- Classic SD set = plain `war3.w3mod:` root + `_locales\<lang>.w3mod:` + `_tilesets\<letter>.w3mod:`.

## wc3mpq — bulk-pack an MPQ from a manifest

```
wc3mpq <archive.mpq> <manifest.tsv>          # lines: <src-path>	<archive-name>
```

- **PITFALL: MPQ v1 archives cap at 4 GiB.** Keep each archive under it (e.g. ship the
  `movies/` tree as its own `War3Movies.mpq`; the engine mounts every `*.mpq` found in
  the data dir, so splitting is free).
- Engine archive contract: `war3.mpq` (+ `War3Local.mpq`), per-tileset `A..Z.mpq`
  (uppercase letter = the map's tileset byte, `R_W3OpenTilesetArchive`).
