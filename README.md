# OpenRealm LAN

This archive preserves the Warcraft III browser and LAN work from an OpenRealm deployment. It includes the engine source, WebAssembly build scripts, room UI, native room launcher, WebSocket to UDP bridge, and tools for preparing your own game data.

- `main` preserves the R93 LAN source checkpoint.
- `candidate/r94` preserves the later Divine Shield cancellation candidate. In the two-client Edge check, a map-created ability lacked its inherited icon and the command did not appear.
- [Archived build records](ARCHIVED-BUILDS.json) identify the original revisions and validation runs. The project supports experimental LAN matches. Some gameplay and stability checks are still open.

The shared engine and Warcraft III module derive from [OpenRealm](https://github.com/corepunch/open-realm) at [ffa72096](https://github.com/corepunch/open-realm/commit/ffa72096a3da72e3a1138afb57a548e1a05ec09e). The [MIT license](LICENSE) and the individual [third-party notices](engine/docs/vendored-dependencies.md) are preserved. Warcraft III game data is supplied separately from your own installation.

## Build and test

On macOS, install Xcode Command Line Tools and put SDL 2.32.10 in a local prefix. On Linux, use the SDL2, OpenGL and zlib development packages provided by your distribution. For example, on macOS:

```sh
export WC3_SDL_PREFIX=/path/to/sdl2
make -C engine HOMEBREW_PREFIX="$WC3_SDL_PREFIX" BUILD=release -j4 openwarcraft3
```

For the browser client, activate [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) 4.0.23, then run:

```sh
bash lan/build-browser-engine.sh
npm --prefix lan/server ci
npm --prefix lan/server test
python3 lan/test-stage-browser-release.py
```

See [LAN setup](lan/README.md) for staging a matching native and browser build, preparing map indexes, and starting the room service. [Reference tests](lan/reference-tests.md) describes how to run the tests that need local game data.
