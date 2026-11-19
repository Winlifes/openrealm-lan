# Vendored Dependencies

OpenWarcraft compiles its own Lua and XML parser from checked-in sources, matching Quake 3's `code/jpeg-6/` pattern. No `pkg-config`, system `liblua5.4-dev`, or `libxml2-dev` is required.

## Lua 5.4 (`vendor/lua/`)

- Source: `vendor/lua/src/` — a verbatim copy of Lua 5.4.8 (`vendor/lua/LICENSE`, `vendor/lua/README`).
- Built as a static archive `build/lib/liblua.a` from the unity rule in the top-level `Makefile` (search `LUA_LIB`).
- Only the VM library is compiled; the interpreter/compiler drivers `lua.c` and `luac.c` are excluded.
- Variables defined in the top-level `Makefile`:
  - `LUA_DIR` / `LUA_SRCS` / `LUA_OBJ` / `LUA_LIB` / `LUA_CFLAGS` (`-Ivendor/lua/src`).
- Only the WoW UI links it: `games/world-of-warcraft/game.mk` sets `LUA_LIBS := -llua` and lists `$(LUA_LIB)` as a prerequisite of `libmenu-wow`, `test-wow-menu`, and `test-wow-hud-xml`.
- To upgrade: replace the files under `vendor/lua/src/` (and `README`/`LICENSE`); the Makefile wildcard picks up the new set automatically.

## Lua 5.3.4 and Eris (`vendor/lua53/`, `vendor/eris53/`)

- Lua comes from the official 5.3.4 archive; the supplied retail executable embeds that release identity.
  Original file hashes, archive SHA-256 and local changes are retained in `vendor/lua53/provenance.json`.
- Eris is pinned to Panakotta00/eris commit `9d328c612a825ca10f1c495595acd96f27f5e288`.
  The five standard-library permanent-function accessors are appended to the 5.3.4 source rather than replacing it
  with the Eris branch's different Lua release. Both MIT licenses are retained.
- `liblua53.a` contains only the VM and serializer. A forced namespace header isolates all 316 external symbols
  from the existing Lua 5.4 runtime. `tools/lua53_namespace.py --check` verifies the built archive's exports.
  The new test links and executes both interpreters in the same process and in a MEMORY64 module.
- Heap streams use strict private `W53` revision 1, rejecting upstream `ERIS` streams and different ABI layouts.
  Local fixes preserve 64-bit C continuation contexts and continuation payload for valid zero-status C frames.
  There is no old-format migration. This SDK stream is not yet a Warcraft world-save envelope.
- `make test-lua53` covers aliases/cycles, shared closure upvalues, typed userdata constructors, suspended Lua/C
  continuations, comparison fallback, protected error handling and malformed/version/layout rejection. It closes
  the source VM before restoring into a fresh VM. Native identities are named explicitly rather than dumped pointers.
- The [Warcraft map integration](games/warcraft-3/lua-maps.md) owns map loading, typed native/BJ binding,
  handles, cross-language scheduling and world saves. External random/file state, execution hooks and direct Lua
  local-recipient presentation retain the limits recorded there. Dependency tests alone do not establish map gameplay.
- Both Lua archive rules honor `AR`; this is required for WebAssembly's archive symbol index.

## XML parser (`common/tinyxml.h`)

- A self-contained, header-only DOM parser with a libxml2-compatible subset of the API. It replaced `libxml2` for the WoW FrameXML parser (`stb_wowxml.h`, `ui_xml.c`), the SC2 layout parser (`stb_sc2layout.h`), and the SC2 map/catalog parser (`sc2_map.c`).
- API surface provided: `xmlDocPtr`/`xmlNodePtr`/`xmlAttrPtr`, `xmlReadMemory`, `xmlParseMemory`, `xmlDocGetRootElement`, `xmlGetProp`, `xmlNodeGetContent`, `xmlNodeListGetString`, `xmlStrcasecmp`, `XML_ELEMENT_NODE`, `XML_PARSE_*`, `BAD_CAST`.
- Nodes keep libxml2's linked-list layout (`children`/`next` for siblings, `properties`/`next` for attributes, an attribute's value stored as a text node under `attr->children`), so the migrated call sites kept their original traversal loops.
- All strings/nodes come from a per-document **chunked arena** (linked list of blocks, never `realloc`'d in place) so tree pointers stay valid across growth; `xmlFreeDoc` frees the arena and `xmlFree` is a no-op.
- Parsing is lenient (equivalent to `XML_PARSE_RECOVER`): comments, processing instructions, and DOCTYPE are skipped; CDATA becomes text; the five standard entities and numeric character references are decoded.
- Because it is header-only with `static` functions, the include guard prevents redefinition inside unity builds; every consumer compiles with `-Wno-unused-function`.

## PKWARE DCL decoder (`vendor/blast/`)

- Verbatim `contrib/blast/blast.c` and `blast.h` from zlib tag `v1.3.1`:
  https://github.com/madler/zlib/tree/v1.3.1/contrib/blast
- Mark Adler's blast 1.3; its permissive license is retained in `blast.h`.
- Built as a separate translation unit into `libshared`, avoiding unity-build symbol collisions.
  The standalone MPQ compatibility binary and WoW tests using `COMMON_GAME_SRCS` compile it directly.
- `common/mpq.c` uses bounded output callbacks to decode MPQ method byte `0x08`.
  This supports retail archive listfiles and patch campaign data needed by the parity catalog.
- `make test-mpq-compression` checks the upstream reference stream, truncated input, and output bounds.
  This adds method-byte decoding, not a claim that every legacy `MPQ_FILE_IMPLODE` layout is supported.

## GL Shader (`vendor/gl_shader/`)

- GL Shader 1.0.0, extracted from Open Realm and shared with Corepunch Orion UI.
- Standalone C99 descriptors, profile generation, compilation and cached typed uniforms; MIT license retained.
- Unity-included once by `renderer/r_shader.c`; update both repositories' complete vendor directory together.
- See [shared shaders](architecture/shared-shaders.md) and the [module contract](https://github.com/corepunch/open-realm/blob/main/vendor/gl_shader/README.md).
- `make test-gl-shader` exercises the shared contract; renderer model/shadow tests exercise integration.
