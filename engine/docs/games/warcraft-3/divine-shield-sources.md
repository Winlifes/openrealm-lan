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

## Conditional cancellation

Cancellation requires both nonzero authored DataA (`Hds1`, Can Deactivate) and
`Misc.CanDeactivateDivineShield`. The supplied 3.0.1 data has both disabled by
default; ordinary stock matches retain that policy. A map may enable them.

Historical `0x6f5c9000` reads the level field, compares it with zero and checks
that Misc key. Its `ucomiss`/`lahf`/`test ah,0x44`/`jnp` sequence rejects zero;
it is not a live-buff query. `0x6f5f9790` supplies the optional off-order
`852091` when cancellation is allowed. This corrects the earlier R93 inference
that that function alone detected an active shield. Active state is separate:
execute clears the ability's `0x8000` inactive bit and retains its buff reference;
deactivation and the buff-removal observer set that bit and release the reference.

The cancel button is a cost-free deactivation while the original cooldown
continues. Its Untip/Unubertip/Unart/Unhotkey fields replace the normal fields
only while cancellation is allowed and the shield is live. Scripted on/off
orders remain explicit state requests; an off request must not create a shield.
The shared casting-toggle capability routes activation through normal resource
checks and deactivation through the owner without re-committing a spell.
The command button must query the concrete ability rawcode for its state, cost
and learned rank; resolving it to the implementation's base code loses the
custom ability's active status. Native tests reproduced that failure before
the HUD correction. Explicit off requests also visit actual owned skills, so
an inactive base skill cannot hide its active alias and the creep class shares
the same order names. Learned hero ranks are valid ownership for the generic
ability dispatcher even when absent from the static ability list; the prior
static-only check rejected them, including Mana Flare's existing off regression.
Activation retains the existing deterministic selected skill. This adds no saved fields or callbacks; the existing
native 171 / browser 32 / JASS 17 representation remains unchanged.

The existing RoC missing-column fallback and duration floor are retained.
Exact zero-duration behavior, refreshes that shorten an existing duration,
class-level sharing between aliases with distinct BuffID values,
and complete Phase Shift visibility/pause/presentation remain
separate original mechanism work; this document does not certify the family.

## Verification

ROC/TFT tests issue the production no-target cast, advance the server frame,
and observe real `T_Damage` outcomes. They cover script sources added/removed
while active, refresh without leaked immunity, authored custom buffs and
durations, full status tables, and whole-world save/load before expiry.
The existing buff-duration/save and recycled-unit tests remain controls.

The cancellation regressions have 24 focused tests in each native mode,
including disabled map/data controls, cost-free cancellation with a running
cooldown, independent immunity, explicit base-skill on/off orders, concrete
alias HUD state and whole-world save/load. Additional controls cover non-boolean
nonzero DataA, a missing map constant, shared off orders, the creep class,
learned-only hero skills, ability removal and full-capacity cancellation.
Its SLK fixture owns its text until
teardown because the test parser borrows that storage. The earlier stack-text
fixture was invalid and is not authoritative red evidence; the owned-storage
HUD regression still failed before its fix. Full native tests pass at this
checkpoint. Release builds, browser MEMORY64 and visible Edge acceptance are
recorded separately in deployment receipts; passing these regressions does not
certify the complete ability family.
