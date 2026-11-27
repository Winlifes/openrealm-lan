# Run a LAN room server

The native process runs the match. Each browser connects over WebSocket to the room service, which relays UDP datagrams to that process. Publish matching native and browser builds. The service binds its HTTP API to loopback. Put Caddy in front of it to serve a single HTTPS origin for the room page, game assets and WebSockets.

The room service starts the dedicated engine with `+lobby_start <map>`. The room loads its lobby metadata at that point. The host's Start command loads the world and runs its game scripts. The browser reports `Ping` next to FPS and Drawcalls using recent acknowledgements from the game connection.

With `scr_showfps 1`, the in-game overlay also reports three timing values over the last half second. `Frame max` is the longest frame interval; a large value is an engine hitch, such as an asset download blocking a frame. `Buffer` is how far the world is drawn behind the fastest recent snapshot delivery, which grows with network jitter. `Wait` is how long the world waited for a late snapshot, which indicates network starvation. To record server frame timing, start the room service with `WC3_ENGINE_PROFILE=1`. Each room log then gets one `[COM_FRAME]` line per second, where `server_max_ms` above 100 means a server frame overran its tick.

On desktop browsers with Pointer Lock, entering fullscreen also locks the mouse to the game. Escape releases it. The mouse can be locked again with the input control after release.

The room service uses 8 KiB sign-on datagrams on its loopback UDP hop. Larger batches reduce the number of WebSocket round trips needed to initialize the map.

The browser rooms retain the match loading screen until each connected human has finished registering the scene. The server keeps sign-on and disconnect handling active while it waits, then starts the match scripts and clock. The loading screen shows how many players are ready.

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

The hosted room client targets wasm32 for browsers such as Safari that lack Memory64. Its static libraries and client files are built under `build/em-web` to keep the browser and Node builds isolated.

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

If the server hosts a trimmed data mirror, pass `--asset-index /path/to/served-assets.txt` when staging the browser release. Generate the list from the actual server directory with lowercase paths relative to `data-current`, one per line. This keeps the browser from requesting files omitted from the mirror. The browser loads the bootstrap and the room's selected archive in parallel with the client engine, then loads other assets on demand.

For room downloads, pass `--preload-index /path/to/preload.index.json` with a JSON `lobby` array of shared paths and a `maps` object keyed by playable archive path. Capture successful `/play/gamedata/` requests in a fresh browser from joining each map until its first rendered game frame. Use lowercase paths relative to the mirror, strip any `.b64` suffix, and omit each map archive and the encoding index. Move paths shared by every map into `lobby`. Pass `--data-dir "$WC3_LAN_ROOT/data-current"` to stage the shared and per-map files as compressed bundles and include the byte count and SHA-256 of each playable archive. Save the measured index alongside the deployment so later browser releases can reuse it.

The download screen checks the client, shared bundle, selected map archive, and selected map bundle before joining. The browser downloads only the room's selected map and expands individual bundled files as the engine opens them. Creating a room starts the native server after the host's download succeeds. An unpacked release with a preload index downloads the listed files individually before joining. Files needed later in a match still load on demand. The in-game loading bar covers server world initialization, sign-on, and scene registration.

The room client keeps verified startup files in the browser's Cache Storage. A later room on the same site can reuse the selected map and shared files. The release manifest selects the expected SHA-256 for each logical file, so unchanged files survive a release update and changed files replace their earlier copies. Startup falls back to the normal download when browser storage is unavailable or the cached copy fails verification.

Packed releases give shared effects and the selected map's model-associated audio their own bundles. Sound files already listed in the map or lobby preload stay in those bundles. Record any additional loose soundtrack or scripted effects in the same preload index. The browser expands the selected audio files before joining, stores the encoded audio for case-insensitive reads during play, and decodes PCM on first use. Imported audio in the map archive arrives with the archive. An unlisted loose sound uses a background fetch on first use.

Include measured command-card and other interface assets in the selected map bundle. For the bulk model and texture bundles, the browser keeps entries compressed until the engine opens them. Once an entry is mounted, it drops the temporary copy. Unopened entries stay available for the match.

Packed releases preload shared minimap effects, terrain decals, creep sleep, and unit death effects. When `development/maps-catalog.json` is available, the release also preloads standard unit models, portraits, textures, and voices for maps marked as melee. The stager reads the four races' unit tables from the served data directory and follows the model texture references. The browser downloads the melee bundle only for those maps and can keep its existing startup cache.

Measure in-match resource requests and frame intervals before extending a map preload. A chain of requests for a new unit's model, portrait, textures and effects can suspend engine frames while the page continues to respond. Add the measured files to the selected map's preload, then repeat the same cold-cache play sequence. Keep unrelated maps out of the bundle.

For an existing deployment, add newly found paths to `extra.lobby` or `extra.maps` in the same preload JSON. The browser downloads the supplemental shared bundle and the selected map's supplement before joining and can reuse its cached base bundles. Keep files already present in the base art and audio bundles out of the supplements.

Use `gameplay.maps` in the preload source for assets found during later phases of a specific map. The stager follows their model dependencies and builds a separate bundle for each map, so adding combat assets preserves the existing lobby and startup bundle hashes. The browser expands these effects and the shared gameplay assets before admission to keep their first use from suspending a rendered frame.

Caddy can serve optional `.gz` sidecars for measured preload files. Keep their uncompressed originals in the served mirror.

For a browser-only update that has been tested against the running native version, stage the release with `--server-build CURRENT_NATIVE_BUILD`. The browser then uses that build ID during the WebSocket handshake while the launcher points to the new browser release directory.

Add player-facing changes to `site/release-notes.json` under the new client release. The room list checks `play/game/version.json` for the published client release and offers a manual refresh when it changes. `stage-lan-site.py` copies this file beside the launcher. When a public proxy uses a separate launcher, route its version file to the same directory. Keep both files revalidated at the proxy.

Simplified Chinese map display overrides live in the Warcraft III share directory under `Localization/Maps/zhCN`. Each file matches an archive filename and uses the 16-digit FNV-1a hash of the resolved UTF-8 text as its lookup key. Translations are applied when publishing map text and tooltips to the interface. Chat commands and JASS string lookups use the archive values.

Use `performance.getEntriesByType('mark')` in the browser to inspect the `wc3-resources-start` through `wc3-resources-mounted` interval and the later `wc3-match-loading` through `wc3-match-ready` interval. In the corresponding `development/room-logs/<room-id>.log`, `SV_LobbyStartMatch:` and `Server initialized.` mark the server's world load. The browser's match interval also includes game sign-on and scene registration.

For Linux builds, pass the absolute libmenu shared library path with `--library` to `build-room-catalog.py` and set `WC3_MPQ_LIBRARY` to the same path for `build-browser-bootstrap.py`.

In separate terminals with the runtime environment above:

```sh
npm --prefix lan/server ci
node lan/server/rooms.mjs
```

```sh
caddy run --config lan/Caddyfile.example --adapter caddyfile
```

The example listens on loopback and uses Caddy's internal TLS issuer. Open `https://localhost:8444/rooms.html` with a browser that supports cross-origin isolation, WebAssembly, and WebGL 2. For use on the LAN, set the Caddy site address, bind address, TLS certificate, and `WC3_ORIGIN` to the LAN origin. Make the certificate trusted on the clients and limit site access to your LAN.

To use both a LAN origin and a public HTTPS site under `/wc3/`, set `WC3_PUBLIC_ORIGIN` to the public origin (scheme and host only) and `WC3_PUBLIC_PREFIX=/wc3` in the room service. The public proxy strips `/wc3` before forwarding and preserves the public `Host` header. Serve the launcher, release assets and `/api` on that same public origin. Forward WebSocket upgrades and keep the cross-origin isolation headers on the game page. The configured host scopes room listings, room details, and WebSocket admission. Both sites use the same map catalog and share the workstation's room capacity.

The public edge can redirect the versioned `bootstrap.mpq`, `openwarcraft3.wasm`, and `openwarcraft3.data` downloads to object storage. Serve the same release files directly from the game origin when `local=1` appears in the query, so the browser can retry a failed or incomplete download. Verify both copies against `release-manifest.json`, allow CORS for the public site, and serve Wasm as `application/wasm`. Keep the launcher and engine script available from the game origin for cross-origin isolation.

The bridge validates that room clients use the server's build name. Its R90 backpressure policy drops UDP datagrams for a congested connection while allowing the engine's own acknowledgements and retransmission to recover. Room transport checking uses `npm --prefix lan/server test`.

The room service sends a WebSocket ping every ten seconds and closes a connection that misses the reply before the next ping. Once the last player disconnects, the room disappears from the list and the native match shuts down. The port is available for a new room once that process exits. A newly created room stays available while the first player connects.

Enter opens an in-game chat field in the browser. The page owns input method composition and hands the finished UTF-8 line to the engine's existing chat command on the game loop. A candidate confirmation keeps the field open. Enter sends after composition finishes, and Escape closes the field.

In Zombie Island III New Century 1.0, the map enables its `我要钱` reward at five-minute intervals. An eligible player can claim it once per interval. The scripts for the first two slots add 176 gold even though the notice describes 88 gold and lumber. The same map uses `pointbreak` and `whosyourdaddy` as defeat triggers.
