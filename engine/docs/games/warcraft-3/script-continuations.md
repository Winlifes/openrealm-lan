# Script callbacks and saved continuations

The game owns timer and trigger registration; the JASS VM owns execution and
suspended call state. `G_RunTimers` queues `jassContext_t` with a timer generation.
`jass_resume` validates a pending generation before entering the action. Direct
`TriggerExecute` starts the same action immediately and leaves a waiting action
scheduled. See [trigger dispatch](trigger-events.md).

## Native code values

A `code` value retains its `jassFunc_t` declaration. Both authored functions and
native declarations may be supplied where a zero-argument callback is required.
Native declarations have no AST body; a null program counter therefore cannot
mean that such a callback has already executed.

`jass_resumecoroutine` calls a native entry through `jass_call`, then discards its
unused return values. `native_done` records that invocation. If the native
requests a wait, the function frame stays alive until its wake deadline, but
resuming it does not invoke the native a second time. Missing native bindings
and callbacks requiring arguments raise runtime errors instead of silently
finishing. The regular coroutine error boundary handles those errors.

`wc3_save.native_timer_callback_runs_after_saved_pending_dispatch` registers
`ResetTerrainFog` through the actual `TimerStart` native, expires the timer,
saves the pending action through `WriteGame`, loads it and pumps the VM. The
original native must restore the independently saved default fog. Standalone
VM tests also cover a single invocation, missing bindings, invalid arguments
and a waiting native saved before its deadline.

## Snapshot contract

Native game saves currently require version 171; browser saves require version
32 and the JASS stream requires version 17. The latest envelope bump requires
[Divine Shield's status-owned immunity](divine-shield-sources.md); its helper no
longer restores a captured unit flag. The continuation fields described
below were introduced in version 13, alongside the revised
[logical expression AST](script-expressions.md). Older envelopes and streams are rejected; no
legacy frame layout, migration or optional extension is used. Network packets,
entity/player wire state and edict fields are unchanged.

`And`, `Or` and `Not` allocate VM-owned predicate nodes with private identities.
Their declaration graph follows Lua code declarations and precedes native
callback fields, Lua userdata and JASS values that reference those nodes.
Operands reference earlier graph IDs or ordinary typed code declarations;
unknown operators, invalid markers, excessive counts, cycles, forward edges,
and a second `Not` operand reject the stream. A truncated graph is discarded
before it can replace live declarations.

The game callback prelude and VM body may declare the same graph. Identical
nodes are reused so event registrations, trigger conditions and script aliases
resolve to the same expression. Only after both VMs commit are obsolete
initialization nodes retired. A later read failure restores the previous
declarations and identity counter. Fresh-VM JASS/Lua tests cover shared
handles, waiting Lua locals, callback closures, repeated saves, extra temporary
initialization nodes, strict rejection and rollback.

Each live coroutine stores its remaining wait, captured `bj_forLoopAIndex` and
whether that private index is valid. The VM temporarily restores the captured
index while resuming a callback and puts the ambient caller's index back after
the callback yields. Saving only the public global loses this isolation when
another callback changes it after load.

Each frame stores its type, function identity, AST body/PC identities, loop
count, locals and four execution flags:

| Flag | Owner and purpose |
| --- | --- |
| `return_boolean` | Child function returns its value to the suspended IF frame. |
| `negate_condition` | Parent IF applies the authored `not` operator. |
| `condition_result` | Parent IF retains the child's completed boolean result. |
| `native_done` | Native entry has already run; only its wait remains. |

The reader accepts `JASS_FRAME_IF_CONDITION`, validates the flag mask and
restores those flags. Previously it rejected that existing frame type and
omitted all four flags and the private loop index. A save could therefore be
written successfully but fail to load at a sleeping condition, or resume a
loop with another callback's index.

The retail-free standalone fixtures save actual suspended frames, reconstruct
a fresh VM from the same script and resume through the scheduler. Separate
positive and negated-false cases detect lost return and negation flags.
`wc3_save.sleeping_boolean_condition_continues_after_real_save_load` additionally
tests the complete game serializer. `previous_continuation_snapshot_is_rejected_without_mutation`
checks strict rejection without replacing current globals.

## Returning to the caller's conditional scope

A scripted boolean condition executes in its own function frame. Returning
releases that frame's locals and leaves an `IF_CONDITION` continuation. Restore
the caller's local scope immediately after popping that continuation, before
evaluating the next `elseif`. Waiting until the next scheduler iteration is
too late: a false result evaluates its next condition in the same iteration.

Original Worm War exercises this through `ScorpionHitsActions`: after
`IsWormAbleToEatScorpion(nPlayerIndex)` returns false, the next branch indexes
`udg_WormRespawn[nPlayerIndex]`. Without restoring the caller, that index
resolves to null and raises an integer argument error. The original map stays
unchanged. Regression coverage includes the actual trigger callback shape,
false/true/negated nested conditions, a nonzero wait, and a complete game
save/load while the condition is suspended.

This corrects execution of the existing continuation contract. Frame layouts,
callback identities, snapshot fields and version requirements do not change.

## Lua boundary

The private [Lua map bridge](lua-maps.md) now owns typed values, protected
nested native calls, cross-language waits and Lua heap/thread persistence.
Snapshot version 12 introduced stable Lua job IDs and an exact Lua source identity. Whole-game
callback declarations precede native timer/trigger fields; the Lua heap follows
native object restoration. Direct Lua recipient-local presentation and other
embedding gaps remain listed in that document. A completed JASS frame or a
serialized world alone cannot stand in for corresponding Lua state.

Run `make test-galaxy` and `make WC3_PATTERN='wc3_save.*' test-wc3-engine` for the
targeted contracts; the latter runs both ROC and TFT fixtures. Native tests do
not establish browser IndexedDB persistence or rendered Lua gameplay.
