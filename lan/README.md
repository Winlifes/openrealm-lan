# Run a LAN room server

The native process runs the match. Each browser connects over WebSocket to the room service, which relays UDP datagrams to that process. Use one release name for the native executable and browser build. The service binds its HTTP API to loopback. Put Caddy in front of it to serve a single HTTPS origin for the room page, game assets and WebSockets.

## Prepare the runtime directory

Set the following values from the repository root. Use a dedicated runtime directory outside the checkout.

```sh
export WC3_LAN_ROOT="$PWD/../openrealm-runtime"
export WC3_ENGINE_SOURCE="$PWD/engine"
export WC3_SDL_PREFIX=/path/to/sdl2
export WC3_NATIVE_BUILD="$WC3_ENGINE_SOURCE/build/classic"
export WC3_ORIGIN=https://localhost:8444
export WC3_RELEASE=r93-local
mkdir -p "$WC3_LAN_ROOT/development/browser-preview" "$WC3_LAN_ROOT/development/proof"
ln -s "$WC3_ENGINE_SOURCE" "$WC3_LAN_ROOT/development/src"
```

Put your extracted Classic SD data in `$WC3_LAN_ROOT/data-current`. It must include a lowercase `maps.index.txt` listing map archive paths relative to that directory. An existing locally generated SD resource ZIP can be unpacked outside the source checkout, then its `gamedata` directory used as `data-current`. For a fresh installation, see the [CASC tools](../engine/tools/casc/README.md) and `python3 lan/prepare-resources.py --help`. Keep your data, caches, and packaged browser bootstrap in the runtime directory.

## Build and stage both runtimes

```sh
make -C engine HOMEBREW_PREFIX="$WC3_SDL_PREFIX" BUILD=release \
  BIN_DIR=build/classic/bin LIB_DIR=build/classic/lib \
  SHARE_INSTALL=build/classic/share -j4 openwarcraft3 map_catalog
bash lan/build-browser-engine.sh
bash lan/publish-engine-runtime.sh "$WC3_RELEASE"
```

The catalog tools read the installed map files. Use the actual map initialization receipts rather than editing a map or labeling an untested map as passed. The audit runs a bounded initialization of each selected map.

```sh
"$WC3_NATIVE_BUILD/bin/map_catalog" "$WC3_LAN_ROOT/data-current" \
  < "$WC3_LAN_ROOT/data-current/maps.index.txt" \
  > "$WC3_LAN_ROOT/development/proof/map-catalog.jsonl"
python3 lan/audit-installed-maps.py --engine "$WC3_NATIVE_BUILD/bin/openwarcraft3"
python3 lan/build-room-catalog.py --include-lua
cp lan/browser/index.html lan/browser/wc3-input.js lan/browser/wc3-movies.js \
  "$WC3_LAN_ROOT/development/browser-preview/"
python3 lan/build-browser-bootstrap.py
python3 lan/stage-browser-release.py "$WC3_RELEASE" \
  --engine-build "$WC3_ENGINE_SOURCE/build/em-web/bin"
python3 lan/stage-lan-site.py "$WC3_RELEASE"
```

For Linux builds, pass the absolute libmenu shared library path with `--library` to `build-room-catalog.py` and set `WC3_MPQ_LIBRARY` to the same path for `build-browser-bootstrap.py`.

In separate terminals with the runtime environment above:

```sh
npm --prefix lan/server ci
node lan/server/rooms.mjs
```

```sh
caddy run --config lan/Caddyfile.example --adapter caddyfile
```

The example listens on loopback and uses Caddy's internal TLS issuer. Open `https://localhost:8444/rooms.html` with a browser that supports cross-origin isolation and WebAssembly MEMORY64. For use on the LAN, set the Caddy site address, bind address, TLS certificate, and `WC3_ORIGIN` to the LAN origin. Make the certificate trusted on the clients and limit site access to your LAN.

The bridge validates that room clients use the server's build name. Its R90 backpressure policy drops UDP datagrams for a congested connection while allowing the engine's own acknowledgements and retransmission to recover. Room transport checking uses `npm --prefix lan/server test`.
