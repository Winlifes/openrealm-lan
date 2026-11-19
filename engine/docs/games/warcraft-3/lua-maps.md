# Original Lua map execution

## Source and entry points

The supplied 3.0.1.24371 executable embeds Lua 5.3.4. `vendor/lua53` retains the
official release and its original hashes; `vendor/eris53` pins the Lua 5.3 Eris
branch. This is independent of the existing Lua 5.4 WoW UI dependency. See
[vendored dependencies](../../vendored-dependencies.md) for provenance and the
316-symbol namespace isolation.

`CM_ReadMapScript` honors W3I `scriptType`: 0 selects JASS, 1 selects Lua. It
tries the root script and then `scripts/` for that same language. An unknown
type or missing selected script is an error, not a fallback to an empty map or
another language. `G_SpawnEntities` loads the actual common.j/Blizzard.j API and
BJs, then the unchanged Lua source. Lua bypasses JASS comment stripping.
`config` runs before lobby overrides and [start-location placement](start-locations.md);
`G_StartScripts` queues `main` normally.

`jass/jlua.inc` is private to the script VM. A map's global table falls back to
typed JASS globals, global arrays and declared functions. BJ calls execute the
existing interpreter and original data scripts. There is no replacement melee
initializer or translation of the original Lua map into a simplified script.

`G_LoadMapScript` owns the game-side language entry. Lua also receives the
typed `UnitAlive(unit) -> boolean` declaration, backed by the existing native
in `api_ai.h`; it uses the declaration spelling from `common.ai` without
executing that AI library or adding its globals to a map. The supplied modern
Lua scenarios call `UnitAlive` directly although common.j does not declare it.
An already-declared function is retained. JASS maps retain their ordinary
explicit-declaration behavior; the private Lua bridge contains no game-native
name special case. The liveness regression drives real CreateUnit/KillUnit
and null handles through the production Lua map loader. Its minimal JASS
fixture explicitly declares KillUnit, which the supplied full common.j already
contains. Adding the declaration changes a Lua VM's source/declaration identity;
strict snapshot identity checks still reject incompatible earlier Lua saves.

## Calls, values and waits

Lua primitive arguments are checked against native declarations before any
JASS stack mutation. Userdata retains the complete `jassVar_t`, including type,
owned-handle identity and shared invalidation/refcount state. Enum equality
uses the same value-handle domains as JASS. Array views retain declaration names
rather than a borrowed Lua string pointer. `FourCC` keeps the original numeric
value; rawcode argument slots identified by common.j/Blizzard.j parameter names
and documented rawcode-returning functions convert at the engine byte-order
boundary. Ordinary integer arguments are never converted by their value.

Every Lua code closure has a stable `__lua_callback_ID` identity. Named entries
are aliases, so reassigning a global does not redirect a previously captured
timer/action closure. Named calls bind later Lua function assignments as well.
Captured native/BJ proxies resolve the original non-Lua declaration. This is
required by original scripts which save `oldMarkGameStarted` before defining a
Lua `MarkGameStarted` wrapper; resolving the proxy to the current alias recurses.

A Lua entry runs in its own Lua thread, owned by a normal JASS scheduler frame.
Direct native waits yield the thread once. A Lua call to a scripted BJ instead
pushes a JASS child frame on the same coroutine and yields through a named C
continuation. The child may sleep; its return value is delivered to the waiting
Lua expression before the Lua caller continues. No suspended C stack is kept.
Synchronous filter-style callbacks cannot suspend their native C caller.

Each Lua-to-host call installs the nearest protected JASS error boundary.
Native longjmp errors return to that boundary before Lua handles them with
pcall/xpcall. An unhandled Lua error then aborts its outer script chain. A
handled error preserves an earlier VM failure instead of clearing its record.

## Strict whole-world persistence

Native/browser saves require 164/25; the script stream requires snapshot 16.
The latest bump retains the additional declared enum value domains. Earlier
heaps could already have lost these values and reject without migration.
Older versions reject without migration. There are no wire, shared
entity/player-state or edict-field changes.

Callback declarations have an explicit section before native world fields,
because native timers, triggers and event filters refer to them by name. The
complete script snapshot follows native object restoration, allowing Lua
userdata to use the same host IDs and owned-handle relocation map as JASS.
The script source identity includes the actual Lua source bytes.

The heap root contains the map global environment, shared closure upvalues,
callback identities, jobs, suspended Lua stacks and module registry tables.
Globals/library tables are serialized; they are not marked permanent. Only
known C identities, engine-owned metatables and process standard streams have
named permanent identities. Typed handle/array constructors rebuild userdata;
no native address is dumped as a world object. Eris uses strict private W53
revision 1 and retains 64-bit C continuation contexts and zero-status C frames.

Each scheduler frame records its Lua job/return-job IDs. Function references
retain their kinds (JASS, native, Lua) and stable Lua closure names as
well: a sleeping original BJ must not reload as a same-named Lua override.
The reader validates heap roots, callback bindings and cross-language frame edges before committing.
Failed/truncated reads discard staged Lua roots and declarations without
deleting the live world's pending jobs. Whole-world checksums and exact outer
save versions remain independently required.

After a complete successful read, callback declarations absent from the saved
declaration table are retired. Temporary fresh-map/AI initialization may have
allocated extra closures; keeping their IDs after replacing the callback heap
can make the next saved checkpoint unreadable. This retirement occurs only
at commit, alongside the new heap, and leaves ordinary JASS/native declarations
in place. It adds no saved field or new callback identity. The production
`lua_save_replaces_larger_fresh_map_registries` regression starts a fresh VM,
creates extra groups/triggers/timers with anonymous callbacks, restores the
smaller snapshot, dispatches exactly one saved timer, then saves and reads again.

## Typed handle strings and nullable arguments

Map libraries such as Silverpine Sprint's `HandleType` parse the concrete
Warcraft type before the colon in `tostring(handle)`. The handle metatable now
supplies `__tostring` from the retained JASS value's declared type, rather than
exposing the bridge's shared registry name. Its address suffix is opaque debug
text, not a portable identifier or retail address-equivalence claim.

Lua table keys use raw identity even when userdata has `__eq`. The original
Silverpine Sprint script builds `UNIT_CLASSIFICATION_NAMES[unittype]` and later
looks it up using the same common.j constants. Previously each read produced a
new userdata, so `"non" .. UNIT_CLASSIFICATION_NAMES[unittype]` failed with a nil
lookup despite value equality. The existing JASS value-handle domains now use
a private weak-value Lua cache keyed by concrete type and uint32 enum value.
Repeated constants and converter results reuse one userdata while referenced;
different enum types remain distinct and zero is a valid enum value. Unreferenced
wrappers remain collectible. This does not canonicalize native world-object or
VM-owned non-enum handles. Value domains now include all 47 additional uint32
enum types returned by the actual CONVERT_FUNC and explicit Convert functions
in api_misc.h, including attack/damage/weapon types, frame points/events and
ability fields. Previously they compared by pointer and saved as null through
the host fallback. Production tests call all 47 converters with non-stock
values, restore their JASS globals, then build Lua keys for every domain and
run another full save/load before resolving those keys again.

The cache is derived, not serialized. Heap restoration builds a separate cache
as typed enum userdata decode, and swaps it only with a fully committed heap.
A truncated continuation after heap decoding discards the staged cache and keeps
the live table-key identities. The strict format bump rejects earlier heaps
whose maps could already contain broken enum-key lookups; no migration is added.
Interpreter regressions cover global/native keys, type separation, zero, weak
collection, fresh-VM continuation and failed-read rollback. A real game timer
uses `UNIT_TYPE_STRUCTURE`/`ConvertUnitType(2)` before and after WriteGame/ReadGame
and another complete save/load cycle.

Native/BJ calls materialize omitted trailing nullable arguments as Lua nil
after validating all arguments, before entering the JASS stack or yielding.
Handle nulls preserve their declared type, strings use the host's existing
empty-string representation, code nulls use a typed code value, and nil booleans
become false. Required numbers, wrong handle/non-boolean types and excess
arguments still reject. This covers the supplied map's omitted group filters
and optional SoundWizard booleans; it does not assert every retail coercion.
The [Lua 5.3 manual](https://www.lua.org/manual/5.3/manual.html#lua_settop)
documents nil padding and boolean conversion, and its table section specifies
raw equality for keys.

The new formatter C callback is registered in Eris's permanent host identities.
Captured formatter closures round-trip through a fresh VM and through real
WriteGame/ReadGame plus timer dispatch. JASS snapshot 14 and the native/browser
envelopes 160/21 reject prior formats; there is no migration. Numeric handle
identity, invalidation and cache lifetime need separate coverage.

## Verification and limits

Modern scenarios can catch initializer failures internally with Lua `pcall`.
A zero native exit status and absence of an uncaught error do not establish
their completion. A temporary diagnostic around `pcall` on the unmodified
Silverpine Sprint script exposed these additional failures: its `HandleType`
library parses the prefix before `:` in `tostring(rect)`, while this bridge
at that build exposed the generic `Warcraft.Handle` metatable name; group-enumeration
calls omit the nullable filter argument; and SoundWizard passes nil booleans
to CreateSound. These fail the current strict bridge and abort initialization
callbacks. Concrete handle string types and nullable argument behavior now have
focused bridge and production regression coverage. Table-key identity across repeated native
handle returns and fresh snapshots also needs an explicit regression; equality
alone does not establish Lua table-key identity.

Forgotten Hollow additionally uses the new nine-slot equipment loadout
(head, chest, gloves, boots, two rings, primary, offhand, trinket) alongside its
bag; do not alias EquipItem to the six-slot classic inventory. Both scenarios
need real dynamic frame and local mouse/client-metric APIs. The existing
`uicanvas` command carries a presentation class, not physical client dimensions;
hardcoding a viewport size would not implement those queries. Diagnostic
instrumentation must be restored byte-for-byte and rebuilt before freezing an
ordinary engine; it is never initialization or gameplay acceptance evidence.

`make test-galaxy` includes real language-dispatch, native argument/error,
JASS-global/array, handle, callback, wait and fresh-VM snapshot regressions.
It closes the originating interpreter before restore. The production
`wc3_save.lua_timer_callback_restores_into_fresh_vm_before_native_fields` test
uses real TimerStart, WriteGame, a fresh VM, ReadGame and timer/scheduler pumps
in both ROC and TFT. Removing the early declaration section fails that load.
Native and MEMORY64 tests are separate from rendered/IndexedDB acceptance.

The first bounded original-catalog check found 49 Lua maps: 34 initialized for
400 frames; 13 melee maps stopped at the then-missing SetEnemyStartLocPrioCount
interface, and two Forsaken Kingdom scenarios exposed additional missing
native APIs. The ally/enemy APIs now feed actual pre-main start placement;
that first audit remains historical rather than a current gameplay count.
The scenarios contain direct GetLocalPlayer branches; one also
uses math.random/randomseed. Existing JASS local-recipient branches still work,
but direct Lua local presentation and external random/file state do not gain
equivalence merely from heap persistence. Lua map-defined numeric-variable
events and every third-party embedding API are not established by these tests.
These gaps must remain explicit; initialization is not complete map gameplay.

See [saved continuations](script-continuations.md), [save/load](save-load.md)
and [trigger dispatch](trigger-events.md) for the adjacent owners.
