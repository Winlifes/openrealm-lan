# Map Trigger Strings in Authored Names

## Contract

`TRIGSTR_<id>` is a map-local reference, not a displayable unit name. The
authoritative text is the matching `STRING <id>` entry in the active map's
`war3map.wts`. `G_MapString()` accepts only an exact `TRIGSTR_` token and
`G_LevelString()` applies that lookup through `level.mapinfo`.

## Data Flow

Map archive data flows through these owners:

```text
war3map.wts
  -> CM_ReadMapInfo / MAPINFO.strings
  -> G_MapString / G_LevelString
  -> WC3 unit, player, HUD, JASS, and entity snapshot consumers
```

Unit object overrides are a separate step. `war3map.w3u` can replace a
profile's `UnitProfile.Name` with a `TRIGSTR_` token while the profile row still
correctly retains that authored token. Consumers must call `G_UnitName()` (or
`G_LevelString()` for non-unit fields) at the presentation boundary.

The entity snapshot path is especially important: `G_CustomizeEntity()` packs
the resolved name into `CS_GENERAL`. The client receives that packed string,
but does not receive the server's `MAPINFO.strings`, so publishing the raw
token leaves `TRIGSTR_*` visible in the client UI.

### Script localization before composition

The supplied 3.0.1.24371 `Scripts/Blizzard.j` defines `StringIdentity` by returning
`GetLocalizedString(theString)`. This is an actual localization boundary: Extreme
Candy War 2005 calls `StringIdentity("TRIGSTR_2779")` before adding its shop-label
color prefix. Resolving only the final text-tag value is too late because that
composed string is no longer a plain table reference.

The game-owned `GetLocalizedString` first calls `G_LevelString`, then retains the
existing lazy `GlobalStrings.fdf` load and interface-string lookup. The same
native serves JASS and Lua, including a dynamically constructed table token.
It uses the current map's table on every call; the VM owns copies of returned
string values, so replacing the table must not alter earlier script values.
An authored empty WTS entry remains empty. No lexer rewrite, client lookup,
global replacement table or new persistence field is involved.

The historical 1.27.1.7085 native registration at `0x6f207666` identifies
`GetLocalizedString` as `0x6f200c20`. Its wrapper calls `0x6f2196b0`, which parses
the trigger-string prefix with `0x6f6ee6f0` and reads the map table with
`0x6f6ee3f0`, before the wrapper's subsequent text lookup. This is historical
call-chain evidence, not a claim of complete current-version fallback, buffer
limit or malformed-token parity; `G_MapString` still has its exact-token policy.

## Human06 Evidence

In the configured Warcraft III campaign archive, `Maps/Campaign/Human06.w3m`
contains these authored references:

| Token | Map source | `war3map.wts` text |
|---|---|---|
| `TRIGSTR_028` | `war3map.w3u` unit name | `Plagued Male Villager` |
| `TRIGSTR_029` | `war3map.w3u` unit name | `Plagued Female Villager` |
| `TRIGSTR_241` | `war3map.w3i` player name | `Undead Elite Guard` |
| `TRIGSTR_242` | `war3map.w3i` player/force name | `Alliance Forces` |

These values are map-local. Do not add them to a global string table or replace
them with hardcoded names.

## Known Pitfalls

- A profile lookup returning `TRIGSTR_028` is correct raw data; resolving it
  while building display state is the required behavior.
- The renderer cannot resolve these tokens after the server has packed a raw
  value into `CS_GENERAL`; resolution must happen in the WC3 game module.
- Do not use installed retail archives in tests. The in-engine regression uses
  an in-memory profile row, map unit override, and WTS entry.

## Verification

The `wc3_slk.map_unit_name_resolves_wts_override` test verifies both the raw
profile value and the resolved `G_UnitName()` value, then drives
`globals.CustomizeEntity` and checks the packed configstring entry:

```sh
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_slk.map_unit_name_resolves_wts_override'
make test-wc3-engine WC3_PATTERN='wc3_slk.*'
```

Run the WC3 engine suite in both ROC and TFT modes through the normal
`make test-wc3-engine` target.

`wc3_api.localized_string_*` also checks FDF/literal behavior, WTS lookup before
color composition through the original BJ call shape and retained text-tag
state, table replacement without changing VM values, authored empty strings,
and the actual Lua map-script bridge. The old implementation fails all three
WTS cases despite passing the direct table-provider controls. These isolated
checks do not replace an original-map framebuffer acceptance run.

See also [WC3 trigger-string syntax](file-docs/readme.md#trigstr_-strings),
[loading and asset resolution](loading-and-assets.md), and
[server-authored UI payloads](../../architecture/ui-payloads.md).
