# Warcraft III Campaign Map Audit

`tools/wc3_map_audit.py` runs every retail RoC and TFT campaign map for a
bounded number of headless server frames, and can smoke-run a loose custom map
under the data tree with `--loose-map`. It retains raw logs and produces a
JSON report plus a GitHub-ready Markdown matrix containing the human-readable
map name, filename, result, and errors observed for every map.

This is a smoke audit, not a completion test. Reaching the frame limit does not
show that objectives progress, the player can survive, cinematics render, or a
mission can be won.

## Prerequisites

Build the engine and MPQ diagnostic tool, and ensure the retail installation is
available at `data/Warcraft III`:

```sh
make build/bin/openwarcraft3 build/bin/mpqtool
```

The Make target builds these dependencies automatically.

## Run All Campaign Maps

The canonical audit uses 600 frames (60 simulated seconds at the fixed 10 Hz
server loop), four isolated workers, a 120-second per-map wall timeout, and a
serial confirmation run for every first-pass crash:

```sh
make audit-wc3-maps
```

Outputs are written to:

- `build/wc3-map-audit/report.json`: complete machine-readable results;
- `build/wc3-map-audit/report.md`: issue-ready summary and per-map tables; and
- `build/wc3-map-audit/logs/`: complete first-pass and serial-rerun logs.

To replace the body of the tracking issue with a fresh report:

```sh
gh issue edit 418 --body-file build/wc3-map-audit/report.md
```

Hero walk / save / load on the same campaign enumerator is a separate local
diagnostic: `make audit-wc3-hero-saveload`. See
[Save/Load](save-load.md#hero-walk-save-load).

Custom-map playability for DotA is tracked separately in
[#431](https://github.com/corepunch/open-realm/issues/431).

## Focused and Longer Runs

Pass runner arguments through `WC3_AUDIT_ARGS`:

```sh
make audit-wc3-maps WC3_AUDIT_ARGS='--map Human05.w3m --frames 18000 --jobs 1'
make audit-wc3-maps WC3_AUDIT_ARGS='--map "Human*.w3m" --frames 1200 --jobs 2'
```

At 10 Hz, `18000` frames requests 30 simulated minutes. For Human05 that can
exercise timer expiry only if gameplay is able to advance without player
actions. It still cannot prove that the defense is winnable. A deterministic
scenario must separately establish a valid player state and assert timer,
attack-wave, objective, survival, and victory milestones.

Useful direct options include:

```text
--frames N          server-frame budget per map
--timeout N         wall-clock seconds per map
--jobs N            concurrent isolated map processes
--map GLOB          filename or archive-path filter
--loose-map PATH    audit a disk-resident .w3m/.w3x under --data (repeatable)
--limit N           audit only the first N filtered maps
--rerun-crashes     rerun first-pass crashes serially
--fail-on-crash     return nonzero after reports have been written
--output-dir PATH   report/log destination
```

Each worker receives a temporary `XDG_DATA_HOME` and a unique `game_port`.
This prevents persistent campaign state and socket collisions from leaking
between maps. Four workers is intentionally conservative because large maps can
consume significant memory.

## Run the Tool Tests

Parser, diagnostic-compaction, and report tests require no retail data:

```sh
make test-wc3-map-audit
# equivalent:
python3 tests/test_wc3_map_audit.py
```

For an integration check against retail data without sweeping all maps:

```sh
make audit-wc3-maps WC3_AUDIT_ARGS='--map Human05.w3m --frames 10 --jobs 1'
```

## Interpretation

The report preserves exact native names, AI script paths, unit rawcodes, event
counts, and full raw logs. High-volume unit/resource messages are compacted
into counted families so one map remains one readable table row.

`completed` means only that the process reached `com_frame_limit` with exit
code zero. `SIGSEGV`, `timeout`, and `auditor-error` are process-level failures.
Warnings and runtime errors remain listed even when the process reaches its
frame limit. Visual correctness requires a rendered pass or human inspection;
mission completion requires scripted milestones or human play.

## Custom Maps

Pass `--loose-map` for a disk-resident scenario under the data tree (typically
`data/Warcraft III/Maps/*.w3x`). The auditor still isolates `XDG_DATA_HOME`,
assigns a unique `game_port`, enables `com_fast_forward` / `vid_hidden` /
`+dedicated 1`, and adds `-tft` for `.w3x` (RoC for `.w3m`). Crash and timeout
outcomes are recorded the same way as campaign maps.

```sh
python3 tools/wc3_map_audit.py \
  --data 'data/Warcraft III' \
  --binary build/bin/openwarcraft3 \
  --mpqtool build/bin/mpqtool \
  --jobs 1 --frames 10 --timeout 60 \
  --loose-map 'data/Warcraft III/Maps/DotA v6.83dAI PMV 1.42 EN.w3x'
```

Equivalent Make form:

```sh
make audit-wc3-maps WC3_AUDIT_ARGS="--jobs 1 --frames 10 --timeout 60 --loose-map 'data/Warcraft III/Maps/DotA v6.83dAI PMV 1.42 EN.w3x'"
```

Without `--loose-map`, the enumerator only walks retail campaign members inside
`War3.mpq` / `War3x.mpq`. See [DotA Custom-Map Playability](dota-map-playability.md).

### Community room compatibility on R50 and R51

The R50 room catalog passed 14 of 14 bounded initialization checks with
`lan/audit-installed-maps.py`. A room test is also needed for map actions that
only run after players join, choose a mode, or reach a later wave.

| Map | Runtime observation |
| --- | --- |
| 人族无敌 II 2.2a | In two rooms with the same difficulty and two human seats, random professions with repeats assigned both professions. Random professions without repeats assigned neither. `CreateUnitPool` and `PlaceRandomUnit` returned null to the map's assignment trigger. |
| 军团 TD 3.41 | Both arena groups accepted the map's attack-move order in a controlled test using its `H0D6` arena units. The map's type query returned an empty group for a live `h03D`. `GroupEnumUnitsOfType` also serves the periodic builder resource label and wave speed update in this map. |
| 澄海 3C 5.45 | A low-health computer hero carrying the map's Light return scroll `srrc` triggered `UnitUseItem`. The native rejected all three uses. |
| 军团 TD Mega 3.47lHf3 | The downloaded map reaches a JASS parse error around escaped quotes. A diagnostic parser adjustment reached missing `BlzCreateFrameByType`, `BlzTriggerRegisterPlayerKeyEvent`, and `BlzGetMouseFocusUnit` calls. |
| 澄海 3C 6.93 | Two clients reached the map and the host chose all heroes, single-hero quick play, and random mode. The view stayed black and the clients received no playable selection units. The server logged `MultiboardGetItem` range or registry errors. |

R51 uses the R50 engine with updated community map files and preloads.
Darwin's Island TFT 4.35 and Pudge Wars Advanced 2.50 both passed browser room
creation, two-player start, player-issued movement, and cleanup on the live
entrances. Pudge Wars Advanced waits for the host's mode selection. The test
used the map's `-km1` chat mode before issuing movement.

### Community room compatibility on R52

R52 implements `unitpool` so that Humans II 2.2a can assign unique professions
through `PlaceRandomUnit` and `UnitPoolRemoveUnitType`. Two players completed
the map's difficulty and mode dialogs and received different professions. The
2.6.70X candidate stalls in its bonus-system initializer. Its archive metadata
identifies 2.6.70X, although the download page lists 2.6.75.

The room catalog restores Chenghai 3C 5.45 and the Chinese Legion TD 3.41.
Chenghai 6.93 supplies packed unit IDs in the byte order that the current object
lookup fails to resolve, leaving the selection units without models. Both
players in 5.45 selected the random-hero mode, received heroes, and verified
movement before leaving. The computer's return scroll still depends on
`UnitUseItem` support.

Legion 3.43d reached settlement, with a runtime error from
`BlzGetUnitBaseDamage` in an authored combat routine. The 3.44 and 3.47 maps
require further Reforged script APIs to start. Two players in 3.41 chose a
builder, built defenders on both sides, reached the opening wave and saw the
same natural settlement. The restored type queries match unit rawcodes and
display names and count units that pass the filter. Browser room checks loaded
the three selected maps through the lobby and match readiness barrier.

### Darwin's Island TFT 4.35 endgame on R53

The map grants level 10 at 162 food and shares that creature's vision with its
opponents. Its 200-food victory trigger gives all players full-map vision and
offers the first player eight endings. The map uses permanent death after the
victory trigger. Its terrain and playable bounds stay fixed. `AI Move` gives
neutral hostile units an attack-move order to a random point every 21 seconds.
Armed units can engage nearby enemies during that walk.

In an isolated two-player room, combat crossed both food thresholds. The old
build displayed the ending menu but lost the earlier players' fog modifiers
as `CreateFogModifierRectBJ` replaced the shared last-created handle. The
active fog registry now retains its own reference until Stop, Destroy, or map
shutdown. The test `wc3_api.active_fog_modifier_outlives_the_script_reference`
covers two recipients and stopping one modifier. Both players saw a distant
opponent after selecting Feral Rampage in the repaired build. In a replay that
measured the first 21-second order cycle, 41 of 48 sampled wild units moved,
and the map's Harvested Human attacked a nearby passive test unit.

A Forest move from `(400, -2608)` toward `(688, -2290)` retargets to the
reachable point `(880, -2288)` for the starting creature. The old fallback
steered directly toward it and repeatedly reversed when it met the tree
line. The repaired move routed around the line and settled at the new
point. The follow-up move along the corridor to `(880, -2704)` also passed.

## Campaign difficulty source inventory (Patch 5)

Before interpreting runtime tests, enumerate difficulty-related branches and AI
script references in the *installed* RoC/TFT campaign maps:

```sh
python3 tools/wc3_map_audit.py --difficulty-scan \
  --data 'data/Warcraft III' --mpqtool build/bin/mpqtool
```

Use `--map 'Human*.w3m'` or `--loose-map` to narrow the scan. This mode requires
`mpqtool` and readable map MPQs but **does not** require a compiled game binary,
launch the game, or write profile state. Results are saved alongside the other
map audits as `difficulty-report.json` and `difficulty-report.md`.

The inventory reports source-line references to `GetGameDifficulty`,
`SetGameDifficulty`, `GetDefaultDifficulty`, `SetDefaultDifficulty`,
`SetPlayerHandicap`, `SetPlayerHandicapXP`, AI start calls, and
`CampaignAI`/`CampaignAttackerEx`/`CampaignDefenderEx`/`SetReplacements` helpers.
It also follows literal `.ai` names referenced by startup calls and checks
for map-local overrides before resolving AI scripts in the stock MPQs. Dynamic
script filenames and wrapper-only difficulty checks cannot be resolved by a
static scan; scripts marked **not resolved** need manual investigation.

**This is not a difficulty parity score.** Presence of a branch does not prove
that it ran or that a resulting unit, research, resource, timer, handicap, or
wave behaved correctly. Use the line references to construct focused
Easy/Normal/Hard `wc3_jass_map` or `wc3_bot` tests, then compare with retail
behaviour before changing game logic. In particular, do not invent a global
stat multiplier or hardcode authored waves by map name.

For runtime comparison after the static scan, run bounded map audits on each
level with separate output directories (shown for Human02):

```sh
for difficulty in 0 1 2; do
  python3 tools/wc3_map_audit.py --map Human02.w3m \
    --campaign-difficulty "$difficulty" --frames 1200 --jobs 1 \
    --output-dir "build/wc3-map-audit/human02-difficulty-$difficulty"
done
```

The preference is set before `+map` in each isolated game process. Even three
successful frame-limited runs only establish startup health at the requested
setting; they do not establish that authored waves or objectives are correct.
