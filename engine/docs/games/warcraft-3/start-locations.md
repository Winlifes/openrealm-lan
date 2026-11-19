# Map start-location placement

`G_SpawnEntities` initializes W3I start locations and ally/enemy priority masks,
runs the unchanged map `config`, then calls `G_ApplyMapPlayerConfiguration`.
That phase reapplies lobby controller/team/race/color choices and resolves
locations before `main` or the original melee BJs create starting units.

The supplied `scripts/common.j` defines random (0), fixed (1), map settings
(2), and teams together (3). It explicitly says that
`ForcePlayerStartLocation` reserves its location against later random placement.
W3I `fixed_start_position` also fixes that player's configured location.
Unconfigured maps default to map settings; campaign coordinates are retained.
`DefineStartLocation` and its location-handle variant can define a start without
creating a player slot. Closed W3I slots remain random-placement candidates;
neutral/rescuable controllers are not combat-player assignment rows.

`SetStartLocPrioCount`/`SetStartLocPrio` configure ally preferences, and
`SetEnemyStartLocPrioCount`/`SetEnemyStartLocPrio` configure enemy preferences.
The table is indexed by start location, not player color or team. High (1) and
low (0) are preference tiers. Not (2) expresses no preference; it does not
forbid a location. Count/slot/location/enum validation prevents out-of-bounds
writes; new count slots are initially unspecified.

The game-owned resolver uses the existing server RNG for shuffled assignments.
Random placement reserves fixed locations and assigns the remaining players
uniquely. Teams-together placement evaluates 32 shuffled assignments with at
most 16 improving-swap passes each. It compares the number of directed high
preferences, then low preferences, then allied squared distance. Swaps can use
unoccupied starts as well as occupied ones. Fixed rows never move. This is a
bounded preference search implementing the exposed configuration contract;
it is not a claim that Blizzard's private tie-breaking/search algorithm was
recovered or that all possible preference graphs attain a global optimum.
Insufficient starts or contradictory fixed combat starts retain the script's
assignment rather than inventing coordinates.

The result updates existing `player_t.start_location` and the starting camera.
`GetPlayerStartLocation` and the unmodified starting-unit BJs therefore consume
the same resolved location. No transport or shared player/entity fields change.
The masks and preference tables are configuration-only: loading a map reruns
`config`, and existing player/camera save fields restore the resolved result.
Placement adds no serialized fields or migration; the current envelope and
snapshot versions are listed in [save/load](save-load.md).

`wc3_jass_map.start_placement*` runs in both ROC and TFT. Coverage includes the
supplied Lua Turtle Rock ally pairing, enemy preferences using an empty start,
force reservation, W3I masks, fixed/map settings, distance fallback, additional
script-defined starts, repeated seeds, invalid/insufficient input, twelve
unique combat assignments with neutral slots, actual `CreateUnit` coordinates,
and real `WriteGame`/`ReadGame` restoration. These tests do not establish
rendered map or army-battle acceptance.
