# Emscripten / WebAssembly port notes

Status: **the engine runs in the browser** — wasm64 build boots in Chromium, initializes
SDL2 + WebGL2, loads a map and runs the frame loop with real GL output. The full engine
test binary (149,706 assertions / 2,670 tests) is green under node. See git log on the
`wasm-port` branch for the change trail.

## Build modes (Makefile, EMSCRIPTEN=1)

| Mode | Flags | Use |
|---|---|---|
| `EM_FS=node` (default) | `-sNODERAWFS=1` | headless test binaries under node (real FS) |
| `EM_FS=browser` | wasm32, Asyncify, `-sEXIT_RUNTIME=0` | browser app (`openwarcraft3.js`) |
| `EM_FS=node-async` | asyncify + NODERAWFS | experiments only |

Common flags: `-sMEMORY64=1` (**required** — the save format and pathing assume 8-byte
pointers), `-sSTACK_SIZE=8388608`, `-sUSE_SDL=2`, `-sUSE_ZLIB=1`,
`-sALLOW_MEMORY_GROWTH=1`, `-sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2`,
`-sFULL_ES3=1` (**required for glMapBufferRange/glUnmapBuffer even on WebGL2**), and
`-Wl,--allow-multiple-definition` (remaining shared helpers in the static link;
this flag does not preserve native module ownership by itself).

## Game and client map ownership

The Makefile browser build and `port/web/build-engine.py` both include
`port/web/src/web_game_world_namespace.h` for the game module. It gives the
game's `world`, map/archive readers and coordinate APIs private symbols. The
game's WPM reader must initialize `server/sv_routing.c` storage; the executable's
reader initializes only the client placement-preview cells. Renaming only
`CM_SetupPathMap` and `CM_GetPathingFlagsAt` leaves `CM_LoadMap` and
`CM_ReadPathMap` interposed by the executable. That produces a green placement
preview while every authoritative construction returns `PLACE_OUT_OF_BOUNDS`.
Hidden visibility cannot isolate these functions in one static WASM image.

`wc3_world.archive_load_initializes_authoritative_build_pathing` loads the real
fixture archive through `CM_LoadMap`, reads its terrain and evaluates Barracks
placement. It fails three assertions before symbol isolation and passes seven
afterward, including rejection outside the map. The test also runs in native
ROC/TFT. Synthetic routing initialization alone does not cover this failure.

For a headless MEMORY64 test build, link the WebSocket JS library even with
`EM_FS=node`, then use local fixtures without browser HTTP:

```sh
make EMSCRIPTEN=1 EM_FS=node GL_BACKEND=gles3 BUILD=release \
  BIN_DIR=build/em-node/bin LIB_DIR=build/em/lib SHARE_INSTALL=build/em-web/share \
  EM_FS_FLAGS='-sNODERAWFS=1 -lwebsocket.js' build/em-node/bin/openwarcraft3-tests.js
(cd build/em-web && node --require ../../port/web/tests/node-local-fixtures.cjs \
  ../em-node/bin/openwarcraft3-tests.js -data ../tests \
  +dedicated 1 +test 'wc3_world.*')
```

The Node adapter returns HTTP misses and supplies no assets or game state.
Archive tests and packet probes are separate from rendered browser acceptance.
Run from the installed `share/` parent: the browser `/share` lookup falls back
to a relative `share` under NODERAWFS. Running from the repository root omits
`OpenRealmAbilityStrings.txt`, including the authored `Aabs` button position,
and creates false presentation failures. With the installed share, the 221
building tests pass 2,446 assertions and 69 routing tests pass 257 assertions.

Example browser build:

```
docker run --rm --user 10000:10000 -e HOME=/tmp -e EM_CACHE=/emsdk/upstream/emscripten/cache \
  -v <root>:/src -v <root>/emsdk-cache:/emsdk/upstream/emscripten/cache \
  -w /src/open-realm-em emscripten/emsdk:latest bash -lc \
  'make EMSCRIPTEN=1 EM_FS=browser GL_BACKEND=gles3 -k -j8 BIN_DIR=build/em-web/bin \
   LIB_DIR=build/em/lib SHARE_INSTALL=build/em-web/share \
   EM_PRELOAD="--preload-file /src/webdata@/data --preload-file /src/open-realm-em/build/em-web/share@/share" \
   build/em-web/bin/openwarcraft3.js'
```

Flag changes do not retrigger make: `rm -f build/em*/bin/openwarcraft3*` to force relink.

## Native before-unload callback pointers

SDK 4.0.23 passes the `pipp` callback return directly to `UTF8ToString`.
With MEMORY64 that return is a BigInt, so a nonempty SDL quit warning throws
while the browser navigates away. `port/web/src/web_beforeunload.js` supplies
the same HTML5 event registration with `from64` on the returned pointer.
Both browser build entry points link this library. It preserves the warning,
navigation cancellation, null return and callback removal; it does not disable
the unload handler or suppress an unsaved-progress warning.

The regression executes a real C callback through the linked HTML5 event
listener. Run from the repository root with the SDK activated:

```sh
emcc -sMEMORY64=1 -sENVIRONMENT=node \
  --pre-js port/web/tests/beforeunload-environment.js \
  --js-library port/web/src/web_beforeunload.js \
  port/web/tests/beforeunload-memory64.c -o build/beforeunload.cjs
node build/beforeunload.cjs
```

Also run without `-sMEMORY64=1` and with `-O2`. Omitting `--js-library`
reproduces SDK 4.0.23's pointer-type failure. This fixture covers the native/JS
contract; a browser save, reload and result-to-site flow checks persistence
and navigation separately. No network or saved-game layout changes.

## Browser main loop and Asyncify

Browser builds use a requestAnimationFrame-driven loop:
`emscripten_set_main_loop_arg(OpenRealm_Frame, ...)` under `#ifdef __EMSCRIPTEN__` in `common/main.c`.

The hosted room client (`EM_FS=browser`, `BZ_WEB_ASYNC_ASSETS`) uses Asyncify so that `WC3_FetchRemoteFile` can
download a missing asset while the engine stack waits, and so that the loading presentation can yield with
`emscripten_sleep`. Asyncify instruments every function that might reach one of those imports. Engine modules call
each other through function tables, so the file paths cannot be narrowed with `ASYNCIFY_IGNORE_INDIRECT`.

libc stdio, formatting and `qsort` make indirect calls only into libc streams or the caller's comparator, never to
engine code, yet they counted as possible suspensions. Every function that logged or sorted was instrumented as a
result, including the per-geoset draw `MDLX_RenderGeoset`. The Makefile's `EM_ASYNCIFY_REMOVE` list excludes those
libc internals. With Emscripten 7.0.1 this cut the instrumented functions from 14,759 to 11,399 and the wasm from
6.21 MB to 5.75 MB. Add a function to the list only when it can never have engine code above it on the stack during a
fetch or sleep: a missing frame cannot be resumed. To list instrumented functions and the reason for each, link with
`EM_PRELOAD='--preload-file build/em-web/share@/share -sASYNCIFY_ADVISE=1'` into a separate `BIN_DIR`.

## Smoke harness

- Serve `build/em-web/bin/{openwarcraft3.js,.wasm,.data}` + an HTML shell (sets
  `Module.arguments`, `Module.ENV`, and captures engine stderr into `window.__errors`)
  over plain HTTP; chromium flag set: `--headless=new --enable-unsafe-swiftshader
  --autoplay-policy=no-user-gesture-required --remote-debugging-port=<port>`, fresh
  `--user-data-dir` per asset change.
- Data: a `webdata/` dir (top-level `.mpq`s + a loose `Maps/` dir) preloaded at `/data`;
  `share/` preloaded at `/share`. Launch args: `-data /data +map Maps/Transport.w3m`.
- Drive via CDP: `Runtime.evaluate` probes (`window.__errors`, screenshots via
  `Page.captureScreenshot`). The Playwright browser tool refuses 127.0.0.1 — use a
  dedicated chromium + CDP instead.

## Open issues

1. **Menu wedge (browser):** the second menu->client `mi.FS_ReadFile` call in `M_Init`
   (`UI\FrameDef\GlobalStrings.fdf`) wedges in the browser, with or without asyncify —
   the first call (`UI_LoadTheme`) always completes; same-module HUD FDF reads are fine;
   node builds complete the whole batch. The menu FDF batch is therefore deferred under
   `__EMSCRIPTEN__` in `M_Init` (map launches clear the menu and do not need it).
2. `test_openwarcraft3_ui` OptionsMenu test crashes with "null function or function
   signature mismatch" in node wasm (plain and asyncify builds) — likely the same family
   as (1).
3. `-sMEMORY64` prints a deprecation notice (prefers `-m64` / `--target=wasm64`).

## Roadmap

- Websocket transport: browser client (WS in `common/net.c` under `__EMSCRIPTEN__`) ->
  small WS<->UDP bridge -> native dedicated server (`+dedicated 1 +map`).
- Asset pipeline against a licensed Reforged install (CASC reads, 1.33+ model format).

## WebSocket transport (browser client <-> dedicated server) — implemented

The browser client keeps speaking the engine's UDP protocol; a small Node
bridge (`tools/ws-bridge/bridge.mjs`) translates WebSocket frames to UDP
datagrams 1:1 (one frame == one datagram, ephemeral UDP socket per browser),
and `common/net.c` grows an `__EMSCRIPTEN__` client backend that tunnels
datagrams over a WebSocket (`ws_url` cvar; sends before the socket opens are
queued and flushed on open, since the plain-IP connect handshake is
single-shot).

    browser wasm client  --WS-->  bridge.mjs  --UDP-->  dedicated server
                         <--WS--             <--UDP--

Verified end-to-end (fixture data): lobby join with live slot assignment
(lobby_setup stream revisions as the operator configures slots) and match
start (client follows the signon, switches to key_game, renders the world;
missing textures are expected - fixtures are not retail MPQs).
Also verified with TWO browser clients simultaneously: distinct lobby slots
(1 and 2, cross-visible occupancy in both clients) and both follow the signon
into the match. The bridge gives each browser its own ephemeral UDP socket,
so the server sees distinct client addresses.

### Runbook

    # 1. bridge (host)
    cd tools/ws-bridge && npm ci && node bridge.mjs --listen 8790 --target 127.0.0.1:27960

    # 2. dedicated server (CI image, host network)
    docker run -d --name wc3-dedicated --network=host \
      -v "$PWD":/src -w /src/open-realm-em \
      ghcr.io/corepunch/open-realm-ci:ubuntu-24.04 \
      bash /src/open-realm-em/tools/ws-bridge/run-dedicated.sh

    # 3. browser client (serve/ on :8791 + headless chromium smoke harness)
    client args: -data /data +connect 127.0.0.1:27960 +ws_url ws://127.0.0.1:8790

### Engine quirks found while wiring this up

- The server's UDP port opens only inside `SV_StartLobby`; a dedicated
  `+map` boot alone never opens a socket.
- `lobby_slot` commands issued in the same console batch as `lobby_start`
  do not stick - issue them after a pause (see `run-dedicated.sh` bursts).
- Plain-IP `CL_Connect` sends its handshake once (retries exist only for
  NA_EOS targets), so the bridge socket may still be connecting; net.c
  queues pre-open sends.
- `startmatch <map>` (server/sv_init.c) = the operator trigger to leave the
  lobby and start the match on a dedicated server (mirrors what the host
  does in the EOS acceptance test).

### Browser session teardown

The LAN r29 two-client acceptance found both browsers returning to the room list
while the bridge still reported both WebSocket peers. No victory dialog followed
the guest's normal Quit Mission. Those observations are failures, not completed
result-screen acceptance.

`CL_DisconnectInternal` now sends its final disconnect datagram and closes
`NS_CLIENT` through the ordinary transport API. The browser implementation in
`common/net_websocket.h` uses Emscripten's close (which drains pending outbound
bytes) followed by delete, then discards the old peer and packet queues.
`NET_ConfigSource(NS_CLIENT, false)` and shutdown share this path. A server close
also releases the handle; late open/message/close callbacks from an old handle
cannot mutate a new connection. Native UDP retains its existing close API.

`tests/test_browser_websocket.c`, included in `test-core`, compiles this same
production header against the WebSocket callback API fixture. Before the fix,
the server-close case failed four assertions (active handle, stale received
packet, missing delete, blocked reopen). Both local and remote close, delayed
callbacks, pre-open sends and idempotent cleanup pass 40 assertions after the
fix. These are transport-contract tests; browser peer removal and natural
melee results require separate runtime receipts.

### Persistent window-control delivery

The original-map native diagnostic produced the Chinese Victory dialog and
received its text on the host transport, while actual background-tab browser
acceptance did not display it. Inspection found `PF_Unicast` placed `svc_window`
open/close controls in the disposable netchan message stream. They do not heal
from subsequent world snapshots. The browser's bounded receive queue can drop
packets while rendering is suspended; this is an observed delivery risk, not a
completed causal proof for every browser failure.

Window controls now enter the existing acknowledged reliable stream. An opaque
per-client FIFO retains bursts while that stream is full, and the normal
`SV_Frame` network send path flushes them after capacity becomes available.
Configstrings are queued before any deferred window that may reference them.
Drop, map/client replacement and shutdown free the retained allocations. Other
unicast payloads keep their existing delivery contracts. No entity/player
network state or packet opcode was widened.

The FIFO permits 64 messages of at most the existing 16 KiB reliable-message
limit each. An oversized control or exhausted FIFO disconnects that peer with
a server diagnostic; continuing with silently missing modal state is not a
valid recovery. This limit must be considered when authoring large custom UI.
The loss regression failed both reliable-retention and resend assertions before
the fix. Eight focused tests pass 77 assertions for loss, reconnect cleanup, delayed capacity,
scheduler flushing/order, resource definitions, loopback, limits and cleanup.
Rendered result/score acceptance remains separate and pending.

The multiplayer score controller also places its terminal `svc_disconnect` in
this stream: Continue first blacks out the local window, so losing the final
disconnect must not leave that surface stuck forever. The packet-loss regression
fails two assertions without reliable admission and passes all seven afterward.
Original-map transport acceptance received Victory, the score window and the
final disconnect despite deliberately dropping the first Victory and disconnect
packets. See [multiplayer score ownership](games/warcraft-3/score-screen.md#multiplayer-score-viewers).

### Independent resource admission

Actual r31 browser input displayed Victory after the opponent's ordinary Quit
Mission; its Quit button returned to the room list and released the final peer.
Mining progressed from 500 to 1,150 gold. No multiplayer score screen was shown.
The guest menu also appeared late, after the other browser resumed processing.

The server regression reproduces a separate cross-recipient dependency: the
global `sv.syncstrings` stayed false while any reliable stream was full, so
every other recipient requeued that same resource and deferred its windows.
Private `client_t.syncstrings` now records admission of the current value into
each recipient's ordered stream. A resource mutation invalidates those bits;
reconnect and map replacement reset them. Global completion still tracks all
current recipients, but a window waits only for its own missing definitions.
Signon writes cannot consume a live update owed to another peer. Signon pages
mark only their own recipient's values queued. The wire and save formats are
unchanged. `server_net.window_resources*`, `signon_does_not_consume*` and
`changing_a_resource*` cover independence, duplicate suppression, scheduler
retry, late signon and subsequent mutation; the two original defects fail
five and six assertions respectively before the fix.
The complete server-net selection passes 77 tests / 3,356 assertions; full suite
15 passes 38 suites / 9,143 tests / 17,750,385 assertions with no failures,
errors or skips. All three native targets pass debug/release builds, and the
MEMORY64 browser build passes. This validates the ordered delivery contract;
rendered timing under real background throttling remains a separate check.
