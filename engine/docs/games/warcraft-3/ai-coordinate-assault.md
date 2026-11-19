# AI coordinate assaults

The supplied `scripts/common.ai` declares `AttackMoveXY(integer x, integer y)`.
Its `AttackMoveXYA` wrapper issues that command, then calls
`SleepUntilAtGoal` and `SleepInCombat`. The native must issue real movement;
the original wrapper retains ownership of the waits.

`AttackMoveXY` resolves the calling private AI VM's player and issues
`order_attackmove` to living members of that player's assault captain. It
records the requested world point as the captain goal and shares an existing
waypoint among those members. Defenders and other players' armies are untouched.
Empty and retreating captains retain their existing behavior. `AttackMoveKill`
uses the same helper after checking its live target, preserving its existing
moving-target refresh behavior.

This closes the missing-native failure reached by the supplied UndeadX08 map.
It does not claim that a bounded initialization run completes that mission or
recovers Blizzard's private minimap signaling/return-home policy.

The native adds no fields, callbacks, wire records or save-layout changes.
Existing captain goals, membership and real unit goal-entity references carry
the point through `WriteGame`/`ReadGame`; this AI change adds no saved fields.
Current strict envelopes are documented in [Save/Load](save-load.md).
ROC/TFT regression coverage uses the real player-bound AI VM, two living members
and one dead member, retargeting through another player-bound coroutine, and
whole-world save/restore. Existing target-assault and retreat regressions cover
the shared helper's inverse paths.
