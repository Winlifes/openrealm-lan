# Reforged 3.0 data compatibility

Target data: the user's CASC installation `3.0.1.24371`, zhCN, classic SD.
Integrated upstream reference: `ffa72096a3da72e3a1138afb57a548e1a05ec09e`
(November 13), retaining the wc3-wasm port and Winlife SD/browser fixes.
Keep browser and native builds on the same source revision.

## Evidence and format contracts

Real input snapshots and execution receipts live outside this source repository in
`deploy/wc3-lan/proof/fullplay-20261114`. Do not put retail maps or scripts in test fixtures.

Reference codec: [w3proj-studio](https://github.com/koneu/w3proj-studio), commit
`d8b36dd86d61dd3ccc3ef1a36222547046d6a847`, specifically `formats/{doodads,units,w3i}.json`
and `src/{w3e,objdata}.rs`. Cross-check field boundaries against the actual map bytes;
the reference explicitly leaves some extension semantics unknown.

* Placement v8 gains a skin id after scale in Reforged. W3I game-version metadata
  disambiguates the older v8 layout. Read map info before placements.
* Placement v12/v13 adds a skin id, a pre-flags word, trailing words, and counted
  nine-word extension records. Preserve those records rather than interpreting them as placements.
  Doodad v13 has three trailing words; units have two.
* Unit subversion 9 lacks the item-table pointer and hero attributes even with a v12 header.
  The version alone cannot decide those fields.
* Terrain v12 uses eight bytes per corner. The texture word has six index bits,
  four flags, then six additional bits; variation and cliff bytes follow it.
  Terrain v11 retains its seven-byte representation.
* W3I v39 adds a loading crest, a terrain-fog block, ten water-rendering integers,
  and (in later editor variants) a HUD skin per player. A shifted player/team count
  previously caused unbounded allocation while loading Human01.
* Object data v3 adds counted modification sets before each object's modifications.
  The classic campaign inputs use flag zero. Nonzero set semantics remain unsupported
  and are reported explicitly rather than silently flattening them.
* The 3.0 `common.j` initializes `fogstyle`, `equipmentType`, `itemTag`, and
  `loadoutslot` constants. Their conversion natives return typed enum handles;
  JASS equality must compare their numeric payloads.

The engine's four-byte loop macros reserve an internal `end` variable. Do not use
`end` as a file terminator destination inside `FOR_LOOP`: it overwrites the loop limit.

## Build and test

On the LAN game station, current integration source is
`$WC3_LAN_ROOT/development/upstream-src`.
Emscripten is pinned to `4.0.23` (SDK release `aaa43392544d695232b70eda706d751f18980c2a`).
The SDK manager requires Python >=3.10; the station uses Homebrew Python 3.13.

```sh
make -j2 build/bin/openwarcraft3-tests build/bin/openwarcraft3
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_map_format.*'
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_api.*enums*'
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test wc3_api.reforged_fog_style_constants_keep_typed_value_equality
make TEST_JOBS=2 test
```

`deploy/wc3-lan/build-browser-engine.sh` builds WebAssembly/WebGL2. Link-only Emscripten
flags belong in `EM_LINK_FLAGS`, not archive compilation flags. Preload engine-owned
share files; create `/data` before main and fetch game assets from the separate SD mirror.

The baseline full suite could not link `test_sound`: its source list omitted the FLAC
decoder added by the browser port. The test now links the same decoder as production.

## Acceptance boundaries

Parser regressions cover legacy/no-skin, Reforged skin, v12/v13 subversion 9/11,
v12 terrain flags, and v3 property alignment. Native bounded map checks separately
load Bandit Ridge, Human01, and Prologue01 using the actual data. Bound diagnostics
by time and resident memory; a successful process exit alone is not a passing map check.

Map initialization is not complete gameplay. Enable the play button only from the
separate browser acceptance receipt, never compilation or map-audit exit status.

The November 14 browser run covered two human clients joining a native server,
independent race/color changes, gathering gold, building a Barracks and Farm,
training a Peasant and three Footmen, and synchronized enemy deaths/building damage.
A later run covered an ordinary player leave, the other player's natural victory,
and return to the room list. It used actual mouse/keyboard input without cheats or
injected results. Twelve-player occupancy is a code limit, not a tested load result.
Campaign completion, arbitrary RPG/Lua maps, HD assets and retail visual parity
remain unverified/unsupported; describe the delivered scope as classic SD JASS melee.

## Presentation, audio and persistence

* Accept SD MDX 800–1800 material/layer extensions, extra UV/LOD records, TANG/SKIN,
  modern lights and camera records. Keep primary UV/LOD0 rendering; split triangles
  into 128-matrix GPU palettes while preserving animation. The installed-model audit
  uses the production parser and covers 3,479 models, independently from unit fixtures.
* Merge UnitSkin/ItemSkin/DestructableSkin/DoodadSkins presentation into the typed
  SLK lookup chain, retaining SD priority and absent legacy fields. Reforged stores
  tree HP/target/pathing in `DestructableData.slk` but its model in
  `Units/DestructableSkin.txt`: omitting the skin prevents the tree from spawning,
  so both human and AI lumber harvesting stall. Doodad variation colors come from
  `vertR/G/B` CSV arrays rather than the legacy numbered SLK columns. Project-authored
  split-table fixtures exercise initialization, real tree spawn and an AI lumber order
  in ROC/TFT, plus SD priority, old embedded models and all ten color variations.
  Invalid item rawcodes must not create generic units or enter item removal.
* Keep WASM game FDF symbols separate from menu FDF storage. Bind info-panel controls
  relative to their semantic parent; regenerate headers with fdfbindgen.
* HUD text resolves the locale's theme font. Text wrapping treats CJK glyph boundaries
  as opportunities, while preserving Latin word wrapping and inline color escapes.
* `GetLocalizedString` resolves valid references through the current map's WTS
  before its lazy GlobalStrings.fdf lookup. This lets the original `StringIdentity`
  resolve a label before color/name concatenation in JASS and Lua; see
  [trigger strings](trigger-strings.md#script-localization-before-composition).
  Unresolved map literals retain their existing behavior.
* Sound/music use the common incremental OGG/MP3/FLAC PCM decoder. The generated
  audio index maps only unambiguous physical paths; it does not invent substitutes.
* Browser state mounts IDBFS at `/userdata`, sets `XDG_DATA_HOME`, and waits for
  `syncfs(true)` before engine startup. Session exit waits for `syncfs(false)` before
  navigation. Emscripten uses `EXIT_RUNTIME=0`, so `Sys_Quit` explicitly notifies
  `Module.onSessionEnded`; relying on `onExit` alone does not work.
  Multiplayer keeps an unmounted temporary `/userdata` instead: its Save/Load UI is
  disabled, and IDBFS reconciliation deletes remote files missing from an older
  tab's local tree. Sharing this mount let a multiplayer exit erase a newer solo save.
  Nicknames remain in the site's localStorage and are independent of saved games.
* The single-player Load Saved Game entry uses the original LoadSavedGameScreen.fdf and MapListBox widget.
  `menuImport_t.ListSaves` reuses the engine's newest-first manual-save catalog; selected basenames flow
  through the normal validated `load` command. Empty catalogs disable Load; paths and command delimiters
  are rejected. ROC/TFT synthetic fixtures cover selection, invalid names/indexes and empty lists.
* `Com_Quit` owns client teardown. Calling `CL_Shutdown` before it caused a second
  renderer shutdown after the WebGL context was destroyed.
* Resource/scenery admission introduced native 148 / browser 9. Current save
  versions are native 150 / browser 11, including the independent interface skin;
  earlier layouts are rejected rather than migrated. The matched
  client/server protocol uses the user-authorized 16-bit effect model index;
  `playerState_t` has not been widened.

## Multiplayer contracts and misleading limits

The native game has **12 playable slots** and **16 player identities**. Identities
12–15 are neutral players. `MAX_CLIENTS=24` is transport capacity, not the melee
player limit. `GetBJMaxPlayers()` returns 12 and `GetBJMaxPlayerSlots()` returns 16;
otherwise Blizzard.j skips neutral identities or visits nonexistent game players.
The room catalog additionally limits occupancy to each map's authored human/computer slots.

* Generated map `config()` can overwrite lobby choices. Apply the saved lobby
  configuration after `config()` and before `main()`, including race/controller/team/color/name.
* Every browser room owns a separate native process and UDP socket; same-origin WSS
  bridges datagrams. Closing a WSS peer dispatches stdin-only `client_drop_endpoint`
  through normal client disconnect handling. It is not a remote admin command.
* Both F10 Quit Mission and result-dialog Quit Game use the generic client disconnect
  action in multiplayer. A server-local menu transition cannot navigate the remote browser.
  The corresponding server-only commands ignore multiplayer requests.
* A connected player's disconnect queues ordinary `EVENT_PLAYER_LEAVE` once. Player-only
  events must match their owner directly; a player edict's default `s.player=0` must not
  dispatch another player's event handler. Blizzard.j still decides defeat/victory.
* Count live TownHall classifications from UnitBalance (TFT) or UnitData (ROC),
  including custom units. The old rawcode list misspelled Orc `ogre`/`ostr` and counted
  dead ruins, causing false exposure warnings or preventing exposure after destruction.
* Edge scrolling requires a real pointer motion/button event. In a newly joined canvas,
  `(0,0)` is zero initialization, not mouse intent. Pointer leave/focus reset invalidates it.
* `+com_fast_forward 0` **enables** the boolean shortcut. Use `+set com_fast_forward 0`
  for real-time networking. The wrong form saturated the server with keepalives.
  Dedicated idle polling yields 1 ms without changing simulation deadlines.

## Installed-map audit and release isolation

The first bounded audit ran 258 melee and 97 campaign map inputs (45 seconds and
2 GiB per input, two workers): 211 JASS melee and 27 campaign maps initialized.
Fourteen crashing campaign inputs were retried after the ItemSkin fix: 13 then
initialized; the remaining input reported a missing native explicitly. These are
initialization results, not campaign playthroughs. The room/browser catalog exposes
only the 211 passing JASS melee maps; the resource pack preserves all original maps.

Never serve live native processes from mutable build directories. Copy bin/lib/share
to an immutable `engine-releases/<version>` directory, then atomically replace the
`engine-current` symlink. Resolve the binary with realpath when the room service starts.
Freeze browser JS/WASM/data/bootstrap/index files into the matching immutable browser
release. All asset URLs resolve from `/play/gamedata/`, including the base64 sidecar index.
Old clients retain their immutable bundle while a new release is built.

Authoritative classic SDL2 is 2.32.10, installed at `third_party/sdl2-classic` on the
station. Homebrew's SDL2compat/SDL3 caused synthetic text-input test crashes. The
classic dylib must be copied into each native build's lib directory; DYLD overrides
are not a reliable substitute on macOS.

## Final integration regressions

* The result dialog's Quit button extends below its nominal root. Generic client-window
  hit testing now includes visible clickable descendants outside that rectangle; an
  inert descendant still does not capture input. The network/window test drives actual
  serialized frames and mouse down/up, rather than checking only OnClick strings.
* After a manual load, W3I still declares the map's original human slots. Runtime
  `G_IsSinglePlayer` uses restored controllers after scripts start, retaining solo
  Save/Load and pause. Before scripts start it uses map/lobby metadata as before.
* In the earlier r20–r23 investigation, save version 90 rejected incompatible
  layouts and validates static movement relocations by animation hash; a development
  r20 save was explicitly rejected after the r22 relink. Do not migrate these offsets
  or describe the fallback fresh map as a successful restore. Test fresh saves on the
  final build and confirm resources/orders after a new browser-engine startup.

## Authored JASS quit choices

The actual 3.0.1 Blizzard.j victory dialog uses `DialogAddQuitButton` and an
otherwise empty registered trigger. It does not use the engine's fallback result
dialog. The previous native recorded the quit flag and closed the window, but
never ended the session. `G_JassDialogClick` now snapshots quit/score/session mode
before callbacks, preserves ordinary choice events, and invokes the existing
EndGame boundary for solo or reliably unicasts the existing `svc_disconnect`
to only the choosing multiplayer client. Duplicate clicks stay rejected.
Browser terminal disconnects notify `Module.onSessionEnded` so the shell flushes
its mounted state and returns to the site's room list. No wire layout changed.

ROC/TFT regression covers a normal choice, solo EndGame, recipient-specific
multiplayer disconnect and duplicate rejection. Full suite 21 passes 6,703 tests,
332,568 assertions across 34 suites. The r24 browser exit flow still needs
interactive verification; compilation and unit coverage do not count as that check.
The r23 same-build save/load was verified with actual UI input, a 2,383-edict
restore, continuing gold gathering and an enabled solo Save/Load menu.

## November upstream integration

The shared W3E batch reader chooses seven-byte (through v11) or eight-byte (v12+)
strides before its allocation and short-file checks. The version-aware decoder is
shared by collision and rendering. MDX bone partitioning copies all UV channels
with source/destination vertex strides rather than only UV0.

`common/difficulty.h` is the single campaign-path classifier for menu difficulty
and game return navigation. It recognizes legacy `Maps/Campaign` /
`Maps/FrozenThrone/Campaign` and supplied `Campaign/Classic/{ROC,TFT}` paths.
Fresh campaign difficulty and deferred score-screen destinations share it. A
remote lobby overrides a previously remembered single-player create-screen mode.

The LAN build uses wasm64/MEMFS/IDBFS/SDL audio. Upstream's experimental
WasmFS/OPFS and AudioWorklet build explicitly defines `BZ_WEB_OPFS` and
`BZ_WEB_AUDIO_WORKLET`; these backends must not be inferred solely from
`__EMSCRIPTEN__`. Pointer relocation/hash packing uses actual pointer width.
## HUD binding variants

The supplied 3.0 SD `SimpleInfoPanel.fdf` replaces `Layer "ARTWORK"` with
`SimpleBuildQueueBackdropFrame`. The queue background remains named `SimpleBuildQueueBackdrop`.
Keeping `ARTWORK` required reports a missing status-template binding and prevents the attack-2 icon
clone from initializing. Do not edit the original FDF or make all children optional.

Regenerate `game/generated/simple_info_panel.h` from both the archived classic FDF and the supplied
3.0 FDF with `fdfbindgen -flatten ARTWORK -flatten SimpleBuildQueueBackdropFrame`. This affects C
bindings only; the parser retains the actual containers, anchors and art. The common queue background
stays required. Select the building root before the unit root, so unsuffixed build-time/action fields
refer to the building context; both archives also contain a unit-context copy.

`EscMenuOptionsPanel.fdf` no longer has the old GammaNotch/GammaNotchLabel decorations. Generate its
binding from both original variants so only variant-specific controls become optional. Core controls
remain required. The original files and generation commands are recorded in the LAN deployment's
`vendor/wc3-ui-reference-20261115/README.md`; production does not load those reference files.

The `wc3_canvas.info_panel_binding_follows_backdrop_moved_to_sibling_container` regression removes
the old group entirely before binding the replacement tree, then removes the actual queue background
to verify a missing required child still fails. The generator test covers merging flattened groups
and retaining common required controls. Native ROC/TFT tests do not replace browser visual acceptance.

## Lazy presentation assets

Actual r28 browser input showed empty attack/armor icons despite visible values. The original
`war3skins.txt` names `infocard-*.blp`, while this SD snapshot stores DDS. The renderer already
accepted that representation, but `InfoPanelTextureExists` rejected the exact BLP path before
rendering. Both now use `common/texture_path.h`: exact first, then the supported store forms.
Missing Neutral art still falls back only to the same authored attack/armor family, and missing
both still reports the error. The DDS-only regression retains the Neutral choice and cache.

The peasant world model was preloaded but its real `peasant_portrait.mdx` was present only in
`assets.index.txt`. `FS_FileExists` previously ignored that mounted remote source, so the client
never requested the portrait and the world model had no portrait camera. Browser existence
probes now consult the exact mirror index after local/archive probes; the normal subsequent
read fetches the actual model. Local-only OPFS builds without this index retain local behavior.
Node tests exercise the production metadata query. Actual r29 Edge input displayed the peasant
portrait and attack/armor icons, saved at 640 gold, then reopened the browser entry and loaded the
new slot; gold advanced to 690 and unit movement remained available.

## Original score FDF verification

Both the archived classic and supplied 3.0 `ScoreScreen.fdf` contain the selected-caption instance.
Its declaration is `Frame"TEXT" "ScoreScreenTabTextSelected"` without whitespace after `Frame`.
A line search restricted to `Frame ` incorrectly misses it; this is not a missing-node defect.
The original files are retained as `TestUI/Compat/ScoreScreen-{classic,3.0}.fdf` fixtures.
`wc3_canvas.score_panel_loads_original_classic_and_modern_fdf` loads each through the real HUD
reader, verifies the Continue command and dynamic rows, and serializes all four tab pages.
Full ROC/TFT tests passed with this positive coverage. This establishes panel admission and
serialization, not a rendered natural-result transition or retail visual parity.

## Item team-color native names

The supplied 3.0.1 `scripts/common.j` declares both `SetItemColor` and
`BlzSetItemColor` with `(item, playercolor) -> nothing`. Both registrations now
use the existing typed item-color implementation, which changes replaceable
team-color presentation without changing ownership or item rules. The real
JASS regression invokes both spellings, including explicit color zero, color
24 and null-item inputs. Before registering `SetItemColor`, that test reported
the unimplemented native and the missing authored color. The original audit
map containing this call also used `SetPlayerRaceSkin`; the separate HUD-skin
implementation is described below. Individual native fixes do not prove a
complete map playable.

## Independent player interface skin

The supplied 3.0.1 `scripts/common.j` declares
`SetPlayerRaceSkin(player, racepreference) -> nothing`. For example, the original
`campaign/classic/tft/humanx01.w3x` generated `config` sets gameplay preferences
Human/Undead/Orc and separately sets each interface skin to USER_SELECTABLE.
This distinction is also established by the populated W3I v39 player fixture in
[wc3libs' map-format notes](https://github.com/inwc3/wc3libs/blob/master/docs/warcraft-iii-3.0-map-formats.md).

The native retains its exact preference in `playerScript_t.race_skin`; it never
changes player race, race preference, ownership or unit rules. `Theme_PlayerString`
resolves explicit Human=1, Orc=2, NightElf=4, Undead=8, Demon=16 and Forsaken=128
sections. Automatic values use the gameplay race. The supplied skin file includes
Forsaken but no Demon section, so the existing Default fallback applies there.
Map-authored `CustomSkin` overrides and localized/edition aliases keep precedence.
Console/resource-bar, command and dialogue presentation caches are invalidated;
cursor and other game-authored skin lookups resolve the new recipient skin.
New map-player spawn clears the override before generated configuration runs.

The real JASS regression covers explicit/automatic changes, a missing race skin
section, null player, a second unaffected player and a map override. Before the
implementation the initial six-preference case failed 22/39 assertions. Native
150 / browser 11 persist the independent preference; see [Save/Load](save-load.md).
Rendered dynamic skin switching and broader campaign gameplay remain separate
acceptance work.
