# Divine Shield state ownership

## Original evidence and current difference

The supplied 3.0.1 data maps `AHds`, `ACds` and `AIdv` to
`CAbilityDivineShield`. The supplied `AHds` brief authors `BHds`, three
durations of 15/25/35 seconds, mana 25 and cooldowns 35/50/65. Its strings
include the cancel action. The current data brief and the historical machine
code below are separate evidence; no current protected native runtime parity
is claimed.

In historical 1.27.1.7085, `CAbilityDivineShield` execution at `0x6f5d8730`
finds an existing `CBuffDivineShield` or creates/attaches it. Buff application
at `0x6f5e7c30` calls `0x6f48b850` and `0x6f48b890`; removal at
`0x6f5d90b0` calls `0x6f48ecd0` and `0x6f48ed40`. These methods increment
and decrement unit counters at offsets `0xe8`, `0x15c` and `0x160`.
The first counter sets a unit flag on its zero-to-one transition and clears
it only on its one-to-zero transition. Removing one buff does not restore
a boolean captured when it started.

The previous `s_ability_stubs.c` implementation stored the explicit unit flag
in a thinker's `resources`, set that flag and restored the captured value
on expiry. It could discard a later script-owned source, restore a source
already removed by a script, or leave permanent immunity after a refresh.
The existing sparse status system already composes active contributions.

## Implementation contract

- Divine Shield's timed status contributes invulnerability independently of
  the explicit `SetUnitInvulnerable`/innate unit flag and other timed sources.
- Authored aliases and custom BuffID values get their state from the ability
  producer; they do not depend on a built-in `BHds` rawcode check.
- Refreshing a shared authored buff retains its latest duration. Expiry of
  an earlier auxiliary timer cannot mutate the current state or base flag.
- Status-capacity preflight rejects before mana and cooldown commitment.
- The existing expiry callback remains rostered and validates its captured
  unit incarnation. It retires its helper without restoring global flags.
- The status, source and helper survive normal strict-version game saves.
  The changed saved meaning requires native 171 / browser 32; JASS stays 17.
  Entity/player wire layouts do not change.

The existing RoC missing-column fallback and duration floor are retained for
this correction. Exact zero-duration behavior, cancel-button/order flow and
complete Phase Shift visibility/pause/presentation remain separate original
mechanism work; this document does not certify the entire ability family.

## Verification

ROC/TFT tests issue the production no-target cast, advance the server frame,
and observe real `T_Damage` outcomes. They cover script sources added/removed
while active, refresh without leaked immunity, authored custom buffs and
durations, full status tables, and whole-world save/load before expiry.
The existing buff-duration/save and recycled-unit tests remain controls.
