# Warcraft III Trigger Event Dispatch And Response Context

OpenRealm routes Warcraft III gameplay events through `GAMEEVENT` in
`games/warcraft-3/game/g_events.c`. Map JASS registers `LPEVENT` handlers;
authoritative gameplay publishes an event when its corresponding state change
or accepted input actually occurs. Tutorial/campaign behavior must use this generic
bridge rather than map-specific checks.

## Player-unit registration filters

`TriggerRegisterPlayerUnitEvent` retains its fourth `boolexpr` argument on
each registration. A matching event evaluates that filter before the trigger's
conditions and actions. A null filter passes. False filters skip both phases;
two registrations on one trigger can retain different filters.

The filter binds `GetFilterUnit` to the event subject while preserving the
caller's existing unit/player response context. The new event's
`GetTriggerUnit` and `GetTriggerPlayer` responses are installed only for the
subsequent trigger conditions/actions. `And`, `Or` and `Not` preserve that
context through nested predicates and short circuit normally. Enumeration
filters retain their existing outside-trigger fallback independently.

Named code predicates and shared anonymous expression graphs survive queued
events and whole-world save/load. The tests use real selection commands and
the server-frame scheduler, including a foreign-owned selected unit, rejected
conditions/actions, composite short circuiting and a Lua callback restored
into a fresh VM. See [saved continuations](script-continuations.md) for graph
identity and strict format versions.

## Player save/load game events

`TriggerRegisterGameEvent` registers `EVENT_GAME_SAVE` (`259`) and
`EVENT_GAME_LOADED` (`256`) using the normal bounded event registry. Other game
IDs remain unsupported and return null event handles. The global registrations
have no unit/player subject, respect disabled triggers, and survive a game
snapshot alongside the other registered events.

Normal ESC-menu Save Game / Quick Save and the server's `save` command issue
`EVENT_GAME_SAVE` **before** writing the full snapshot, allowing map handlers
such as the Rexxar zone-backup scripts to copy their existing zone files.
The session loader issues `EVENT_GAME_LOADED` **after** a successful named
full-game restore; this lets campaign scripts restore a saved slot's zone
backups. A scripted zone restore also dispatches this event after applying its
snapshot, with `MAP_RELOADED` temporarily set so campaign return handlers run
without the named-save directory-copy handler. These callbacks run event
conditions and queued actions through the existing JASS scheduler while the
save-name context is valid. The event is not issued after a failed restore.
The save is aborted if pre-save event execution reports a JASS runtime error or
the event queue cannot accept the event.

`GetSaveBasicFilename` returns the logical save basename (stripping a `.sav`
or `.w3z` suffix) during these callbacks, not an OS path. Outside the callback
it is empty. Actions that yield and run later do not retain this transient
filename; asynchronous save-handler completion is **not** supported yet.
JASS-initiated scripted zone saves and loads use deferred VM-safe/session
operations. The internal source save does not synthesize `EVENT_GAME_SAVE`;
the successful scripted restore dispatches `EVENT_GAME_LOADED` with
`MAP_RELOADED` as described above. See
[save/load](save-load.md).

## Event response payload

`GAMEEVENT` carries the response state that must survive until the JASS action
coroutine runs:

| Field | Meaning |
| --- | --- |
| `edict` | primary/triggering unit or widget |
| `source` | secondary unit/widget: attacker, order target, manipulated item, etc. |
| `value` | event-specific scalar such as spell/research rawcode |
| `point` / `has_point` | spell point target when the event carries one |
| `responseTo` | exact targeted registration for region/range/game-state events |

`jass_calltriggerevent()` copies those fields into `JASSCONTEXT`; sleeping JASS
coroutines retain the same response context. The save/JASS snapshot serializers
persist the queue/context fields introduced by the event bridge.

## Retail event IDs

The retail `common.j` used by current campaign data assigns player sell events
to 272/273/274 and unit sell events to 289/290/291 (`SELL`, `CHANGE_OWNER`,
`SELL_ITEM`). The corresponding spell lifecycle events follow those values
through 279 and 296. Keep the `EVENTTYPE` values and the declarations in
`game/common.txt` aligned with those IDs: `ConvertPlayerUnitEvent` and
`ConvertUnitEvent` preserve the numeric value, so a stale producer enum can
silently publish an event that no retail JASS registration matches. The
OrcX01 armory regression in `t_items.c` exercises the player sell-item value.

## Explicit trigger execution

Event dispatch evaluates conditions, then queues action coroutines for the JASS
scheduler. A direct `TriggerExecute()` call instead starts each action and runs
it immediately until it finishes or reaches its first wait. A waiting action
remains scheduled to resume after the caller continues.
`ConditionalTriggerExecute()` therefore exposes action writes before the
action's first wait to its caller; if the action has no wait, all of its writes
are visible when the native returns.

Enabled state only gates registered events. `TriggerEvaluate()` evaluates the
trigger's conditions regardless of whether `DisableTrigger()` was called, and
`TriggerExecute()` runs its actions explicitly. The event/timer bridge checks
`trigger->disabled` before evaluating a registered callback. This matches
Blizzard.j's `QueuedTriggerAddBJ`: its `TriggerExecuteBJ` helper evaluates the
conditions and then explicitly executes the action, so maps can queue a trigger
that they created disabled. `ConditionalTriggerExecute()` also uses explicit
evaluation; a condition that needs to reject a disabled trigger can query
`IsTriggerEnabled()` itself.

Native declarations supplied as `code` callbacks run through the same scheduler;
they are not empty script functions. Waiting conditions and native callbacks
retain their execution state in [saved continuations](script-continuations.md).

UndeadX04 relies on this contract for the opening Base quest: the map disables
`Dragonhawks Die Q` during initialization, then queues it after gameplay starts.
Its dialogue action discovers the Base quest. Rejecting disabled triggers in
`TriggerEvaluate()` caused the Blizzard.j queue helper to discard that entry.
The regression `wc3_jass.disabled_triggers_allow_explicit_execution_but_ignore_events`
checks that explicit disabled-trigger execution succeeds while disabled timer
events remain suppressed and enabled timer events still fire.

Nested immediate actions must preserve the active parent coroutine. In
particular, if the parent calls `TriggerSleepAction()` after a nested
`TriggerExecute()`, it must still yield and resume at that point. `jass_resume()`
restores the enclosing coroutine, player/unit response context, and loop index
when the nested action returns.

This distinction fixed Orc03's zeppelin victory gate. Its `ForGroup` callback
executes a counter trigger for each zeppelin, then immediately checks the
counter. Queuing those `TriggerExecute()` actions made the check run too early,
so the victory trigger was skipped even when both zeppelins were home. The
regression is covered by
`wc3_jass_map.orc03_home_counter_actions_complete_before_victory_gate_check` and
`wc3_jass_map.nested_trigger_execute_preserves_parent_coroutine`.

## Selection

Interactive membership changes use `G_SelectEntity()` / `G_DeselectEntity()`.
`G_PublishSelectionDelta` emits `EVENT_PLAYER_UNIT_SELECTED` / `DESELECTED`
through `G_PublishEventForPlayer`, retaining the selecting player's Warcraft
number independently of the unit owner. The runtime cap-validation pass uses
the same publisher for removed members. Re-sending identical membership still
does not generate duplicate transitions.

`gameEvent_t.trigger_player` is a one-based explicit event player (zero retains
the normal subject-derived context). It generalizes the former `dialog_player`
field already shared by dialogs and chat. Event registration matching, trigger
conditions and queued/sleeping actions use this retained player; `GetTriggerUnit`
continues to identify the selected unit. Unit-specific selection notifications
retain their separate unit context; this fix does not establish the original
`GetTriggerPlayer` response for the unit-specific event domain.

The supplied original Candy War 2005 chooser units belong to `Player(10)`,
while `InitTrig_Pick_Heroes` registers human selections for players 0..9.
Owner-only dispatch misses those registrations. Seven regressions cover a
foreign chooser, two simultaneous selectors, condition context, queued-event
save/load, sleeping actions saved after a normal ownership change, remapped
player numbers and cap reduction through two actual `RunFrame` calls.
The historical 1.27.1.7085 natives independently read player context slot 3
(`0x6f203840`) and unit slot 0 (`0x6f203860`) through `0x6f2892d0`; do not
conflate that binary evidence with running the supplied protected 3.0.1 code.

Pointer input now retains an explicit trailing `click` in the existing `select`
command. `CMD_Select` first publishes its ordinary membership delta; if the
accepted clicked anchor was already selected, it separately publishes that
anchor's player/unit selected notifications. It does not publish a deselection,
repeat new members, replay an invalid anchor, or reinterpret a pending command
target as a selection. Untagged identical membership remains silent. Ctrl/double
click retains the same intent alongside same-type candidates; Shift keeps its
existing queue path and its unresolved toggle semantics.

The historical 1.27.1.7085 registration at `0x6f6bdc68`/`0x6f6bdd54` associates
opcode `0x1b` with `0x6f6bc6d0`. That handler resolves the unit and event player,
then calls CUnit vtable `0x1d0`/`0x1d4`, whose publishers are `0x6f67af30` and
`0x6f679e70`, separately from membership modification. This is inspected command
registration/handler evidence, not an inference from RTTI names. Candy War's
original script explicitly confirms a second matching Hero selection; its
temporary `otbr` carries `Agho,Aloc,Avul` and is not a selectable replacement.
ROC/TFT regressions drive two clients through first selection, an untagged
resend, deliberate repeat, same-type additions, rejection and queued-event
save/load. Both Heroes acquire the actual selector as owner; unit event counts
and the absence of false deselections are checked independently. These are
focused contracts, not certification of an entire original map's gameplay.

Native save 169 / browser save 30 require the new retained selection meaning;
old saves reject. The JASS context already saves the explicit player, so its
snapshot layout/version remains 16. No entity/player network field changes.
The later click-intent correction changes only producer/input behavior and
retains those save versions; no stored field, callback identity or queue meaning
changes. Paired client/server artifacts are still required for the new token.
The minimal fixture `Scripts/common.j` declares the original unit-selected
constants 57/58; repack `test-assets` after changing it. A declaration present
only in `game/common.txt` does not put it in the test MPQ.

Remaining: unit-specific player responses, nested explicit execution context
and registration filters still need separate evidence/repair. `TriggerRegisterPlayerUnitEvent`
currently ignores its filter argument; conditions are not a substitute for it.
Do not describe selector identity tests as complete original hero-picking
or selection-mechanism acceptance.

`SelectUnit` / `ClearSelection` are local-presentation natives. A synchronized
call applies to every **connected local client view**, not to unused map-player
slots. When JASS is executing under a local-player context, only that connected
client view is changed. Disconnected placeholder clients must never acquire
selection bits or generate selection events.

## Attacked events

An attack event is emitted when an actual unit attack swing starts, not when an
attack order is merely accepted:

```text
attacker begins melee/ranged swing
    -> EVENT_PLAYER_UNIT_ATTACKED
    -> EVENT_UNIT_ATTACKED
```

Response context is:

```text
GetTriggerUnit() = attacked unit
GetAttacker()    = attacker
```

The attacked unit is `GAMEEVENT.edict`; the attacker is `GAMEEVENT.source`.
Destructable attacks do not synthesize unit-attacked events.

## Spell effect events

The current unified spell pipeline supports the high-confidence **starts the
effect** transition:

```text
validation succeeds
    -> mana/cooldown commit where applicable
    -> EVENT_PLAYER_UNIT_SPELL_EFFECT
    -> EVENT_UNIT_SPELL_EFFECT
    -> spell gameplay callback
```

Response context is:

```text
GetSpellAbilityUnit() = caster
GetSpellAbilityId()   = ability rawcode
GetSpellTargetUnit()  = entity target when the current spell target is a unit
GetSpellTargetX/Y()   = point target coordinates when the current target is a point
GetSpellTargetLoc()   = location for a point target
```

`GetSpellTargetItem()` / `GetSpellTargetDestructable()` safely classify the
secondary target handle if future spell target modes carry those widget types.
OpenRealm does not yet expose a confident ability-instance handle for
`GetSpellAbility()`.

Do **not** fake the rest of the lifecycle. `SPELL_CHANNEL`, `SPELL_CAST`,
`SPELL_FINISH`, and `SPELL_ENDCAST` remain unsupported until the spell state
machine has distinct authoritative transitions for them; event diagnostics
continue to report registrations for those gaps.

## Orders, items, Hero skills, regions and range

The bridge also provides:

- accepted immediate, point, and target order events with `GetIssuedOrderId()`,
  point and target response natives;
- successful item pickup/use context via `GetManipulatingUnit()` and
  `GetManipulatedItem()`; final-charge perishable uses retain the consumed item
  handle through queued event dispatch and sleeping trigger-action context so
  the response remains valid while JASS is handling that use;
- successful Hero skill learning via `GetLearningUnit()`, `GetLearnedSkill()`
  and `GetLearnedSkillLevel()`;
- region enter/leave with `GetTriggeringRegion()` and entering/leaving unit;
- `TriggerRegisterUnitInRange` with the watched unit as trigger unit and the
  entering/other unit as filter source.

Authored boolexpr filters run with the event-specific filter unit before trigger
conditions/actions are scheduled.

## Diagnostics

`wc3_event_debug` is opt-in and must not affect gameplay:

```text
wc3_event_debug 1   registrations, known gaps, per-map summary
wc3_event_debug 2   plus event queue, filter rejection and dispatch detail
```

Summary counters have distinct meanings:

- `registered`: map registrations of that event type;
- `queued`: `GAMEEVENT`s published by gameplay;
- `matched`: a registration was reached and its event filter passed;
- `dispatched`: `jass_calltriggerevent` / direct timer trigger queued work after
  trigger conditions;
- `filtered`: authored registration filter rejected the event.

`EVENT_GAME_TIMER_EXPIRED` is special: `G_RunTimers()` invokes its registrations
directly instead of creating a `GAMEEVENT`, so `queued` can remain zero while
`matched`/`dispatched` increase. This is working behavior, not a missing timer
producer.

A summary reports `REGISTERED_NEVER_OBSERVED` only when an event was registered
but neither a queued gameplay event nor a direct matched registration was seen.
That status is evidence to investigate, not proof of a bug: the player may
simply not have performed the relevant action.

## Human01 diagnostic findings (2026-09-04)

A `wc3_event_debug 2` Human01 run established these reusable facts:

- region enter, unit-in-range, game-state, death, accepted order, Hero-skill and
  selection producers were active;
- script-driven selection incorrectly generated transitions for disconnected
  players; fixed by limiting local selection presentation to connected views;
- Human01 registers both player-unit and unit-specific attacked events while no
  attacked producer existed; fixed at attack-swing start;
- Human01 registers `EVENT_PLAYER_UNIT_SPELL_EFFECT`; the unified spell pipeline
  had no lifecycle producer, so `SPELL_EFFECT` is now emitted immediately before
  the gameplay effect callback;
- zero observed pickup/Hero-level/leave-region events in that run were **not**
  treated as implementation failures because those actions were not proven to
  have occurred;
- timer summaries were previously false positives because direct timer trigger
  execution bypassed the queued-event counter; `matched` now records them.

## Verification

Focused coverage lives in:

- `games/warcraft-3/game/tests/t_jass_map.c` — Orc03's synchronous `ForGroup` /
  `ConditionalTriggerExecute` counter gate, direct trigger waits, and nested
  parent-coroutine restoration;
- `games/warcraft-3/game/tests/t_api.c` — selection locality, attacked response,
  order/item/Hero/region/range response context, spell response context;
- `games/warcraft-3/game/tests/t_avatar.c` — a runtime-added spell can cast through
  its stock order and publishes the two spell-effect event families;
- `games/warcraft-3/game/tests/t_game.c` — direct timer expiry is counted as
  matched/dispatched diagnostics and save/load timer behavior remains intact.

After building, useful targeted runs are:

```bash
make test-wc3-engine WC3_PATTERN=wc3_jass_map.orc03_home_counter_actions_complete_before_victory_gate_check
make test-wc3-engine WC3_PATTERN='wc3_api.*'
make test-wc3-engine WC3_PATTERN='wc3_spell.*'
make test-wc3-engine WC3_PATTERN='wc3_save.round_trip_jass_timers'
```

## JASS interactive dialog choices

`DialogDisplay` creates a player-scoped modal `svc_window` without pausing the
simulation. A validated click publishes events targeted at the registered
dialog and button, with the authoritative player ID and one-based clicked
handle IDs. This response context is copied to a coroutine, so
`GetClickedButton` / `GetClickedDialog` continue to work after a trigger wait.
See [JASS choice dialogs](jass-dialogs.md) for lifecycle and limitations.

## Trigger destruction

`DestroyTrigger` retires every live `event_t` registration owned by the trigger
and clears queued `responseTo` references before those event slots can be reused.
It disables the map-lifetime trigger object instead of freeing it, so an action
can destroy its own trigger safely. `DisableTrigger` leaves registrations intact
so `EnableTrigger` can resume them.

### Scripted zone save lifecycle

The JASS `SaveGame` native used by zone-transition wrappers is a deferred
internal save, not a player-selected named game save. It does not synthesize
`EVENT_GAME_SAVE` or `GetSaveBasicFilename` context. Normal player save
entry points continue dispatching `EVENT_GAME_SAVE` before their own snapshot.
A following `ChangeLevel` is queued only when that deferred internal save
succeeds at the end-of-frame VM-safe boundary. The cross-map `LoadGame` uses
the session loader and dispatches `EVENT_GAME_LOADED` after restoring the
destination snapshot, with `MAP_RELOADED` temporarily set.
