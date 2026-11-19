# Ability tooltip text

`api/api_ability_text.h` registers all six class-level `BlzGetAbility*Tooltip`
and `BlzSetAbility*Tooltip` pairs. `game/common.txt` supplies their signatures:
an ability rawcode and zero-based rank, with a string for setters. These are
independent of the ability-instance `BlzSetAbilityStringLevelField` API, which
still has separate implementation gaps.

`g_ability_text.c` owns the value and lifetime. The exact supplied
3.0.1.24371 `Units/AbilityMetaData.slk` maps the fields as follows:

| Native suffix | Profile field | W3A ID | Authored ranks |
| --- | --- | --- | --- |
| Tooltip | Tip | atp1 | repeated |
| ActivatedTooltip | Untip | aut1 | repeated |
| ExtendedTooltip | Ubertip | aub1 | repeated |
| ActivatedExtendedTooltip | Unubertip | auu1 | repeated |
| ResearchTooltip | Researchtip | aret | scalar |
| ResearchExtendedTooltip | Researchubertip | arut | scalar |

Map modifications precede profile text, and custom abilities inherit their
authored parent. W3A repeated ranks are one-based; scalar research fields use
rank zero. Profile lists retain quoted commas. WTS values resolve through
the current map. Reads and writes reject zero/unknown rawcodes and invalid
ranks/recipients. A valid explicit empty edit remains empty.

Runtime values are owned strings in `level.ability_text`, keyed by rawcode,
field, rank and local recipient. A synchronized write replaces earlier local
edits for that key; a later local write shadows the default only for that
recipient. Getters with the existing `currentplayer` context resolve the local
value. A context-free server query resolves the synchronized default. This
does not solve arbitrary divergent Lua `GetLocalPlayer` execution.

The command-card builder resolves the concrete ability and the viewing
recipient, then formats the already-selected string. It does not parse an
edited string as another comma-separated list. Setters invalidate command
cards for the affected recipients. Research `%d` formatting remains a UI
operation; the native string read preserves the stored text.

Native save 163 / browser save 24 introduced these records through the normal
mapped level schema. The reader validates rawcodes, field/rank/recipient
bounds and duplicate keys; malformed references with a valid checksum are
rejected. Map replacement and shutdown free strings and records. No shared
network struct or JASS callback identity changes; snapshot version remains 16.
Current envelope versions are listed in [save/load](save-load.md).

`wc3_api.ability_tooltip_*` covers all six pairs, map-field IDs, custom-parent
inheritance, quoted commas, empty edits, UI rank/recipient selection,
12,000-byte native strings, two save/read cycles and failed writes preserving
the prior valid file. `wc3_save.rejects_invalid_ability_tooltip_references_with_valid_checksum`
covers damaged keys and over-limit counts through the actual reader.

The original Silverpine Sprint script uses the normal tooltip pair around
its preload-file bridge (`loadfile`, original lines 644–648). Preloader and
file generation remain unimplemented; providing text storage alone does not
make that bridge or the entire scenario playable. Exhaustion of a short
authored profile list currently reuses its last supplied entry; that edge and
complete retail UI parity still need exact-version binary/runtime evidence.
The original executable has been retained with its SHA-256; locating native
name strings alone is not evidence for their behavior.
