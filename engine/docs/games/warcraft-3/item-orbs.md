# Item Orbs

The six `AIDB`-parent attack orbs (`AIfb` fire, `AIlb` lightning, `AIob`
frost, `AIpb` poison, `AIcb` corruption, `AIzb` freeze) share one data
shape: `DataA` bonus damage, single level, self `code=`. `AIcb` also
authors its armor reduction in `DataB`; all TFT rows carry `DataE=2`, whose
complete policy remains unverified. Do not register them on the guessed spell procs from the old TODOs
(`CAbilityOnFireHuman`, `CAbilityFrostNova`, `CAbilityImmolation`) —
those are wrong-class mappings of the same kind the creep-alias cleanup
unregistered.

## Mapping

| Orb | BuffID (TFT; ROC omits) | Behavior |
| --- | --- | --- |
| `AIfb` | none | damage + full-damage splash to nearby enemies |
| `AIlb` | none | damage only |
| `AIob` | `Bfro` | damage + frost state, `Dur`/`HeroDur` |
| `AIpb` | none | damage only |
| `AIcb` | `BIcb` | damage + authored armor reduction before physical impact, `Dur`/`HeroDur` |
| `AIzb` | `Bfre` | damage + freeze state (`Dur` 0 = indefinite) |

Five rows use `CAbilityAttackBonus`; `AIcb` uses `CAbilityCorruptionDamageBonus`
and delegates pickup/removal to that same procedure. Like `AIat`, pickup applies
`DataA` to `temporaryDamageBonus` and drop reverses it.
`S_OrbOnHit` in `s_item_stats.c` applies the other BuffID states from
`S_ResolveAttackHit` next to `S_SlowPoisonOnHit`, checking native orb
ownership and held orb items (the same orb from both applies once).
Orb of Fire (`AIfb`) uses its authored `Area` radius to splash the primary
attack's damage to nearby enemies through `S_OrbAttackSplash`. The splash is applied on both melee and
missile hits, excludes allies and out-of-radius units, and follows the normal
attack damage path. The supplied 3.0.1 row has `DataA=5`, `Area=150`, and `DataE=2`;
the historical fixture's radius is 140 and is not the current installed value.
The `orb_of_fire_splashes_full_attack_damage_to_nearby_enemies` regression uses
a non-stock radius and checks both melee and missile hits.
The status modifier system consumes the frost buff (`Bfro`) as the classic
50% movement and 25% attack-rate slow. The Orb of Frost regression drives
`S_OrbOnHit`, checks both slow modifiers, and advances the entity scheduler
through the authored duration to verify they expire. Complete Freeze behavior
remains a parity gap.

## Corruption

The supplied 3.0.1 `AIcb` row authors +5 damage, a four-point `DataB` armor
reduction, `BIcb`, `ground,air,ward` and five-second normal/Hero durations.
Its aliases `AACq/AACw/AACe` author independent amounts. The single-archive
supplied-data audit is distinct from historical ROC/TFT archive inputs.

The readable official 1.27b executable identifies `CAbilityCorruptionDamageBonus`
at vtable `0x6fb2c408` and `CBuffCorruption` at `0x6fb2c744`. Preparation
`0x6f54ffd0`/`0x6f550027` applies or refreshes the buff before target virtual
`+0x120`. Buff apply `0x6f55d7a0`, refresh `0x6f564960` and removal
`0x6f54f530` subtract, replace and restore the retained float through unit
adjustment `0x6f670370`. Illusion attackers supply zero and illusion receivers
skip the armor change. This is versioned source evidence, not a claim that
the full supplied 3.0.1 executable has been recovered or executed.
The original class selector `0x6f5591a0` returns the intrinsic `BIcb` fourcc
(`0x42496362`); legacy rows omitting BuffID therefore retain that marker.
An authored custom BuffID uses the supplied-data override instead.

`S_ResolveWeaponHit` retains raw damage, launch attack type and arrow identity
until impact. Miss checks precede Corruption; target armor mitigation follows
it. Melee, ordinary missiles and primary splash impacts use this route.
Already-mitigated direct callers retain `S_ResolveAttackHit[WithArrow]`.
The owning ability validates the actual alias's target mask and rank and
materializes one existing armor descriptor on its actual BuffID. Refresh
replaces that contribution; expiry/removal and current-format save/load use
the normal status lifecycle. Manual arrows and the selected held orb still
govern item application. Spell immunity does not reject this physical attack
modifier. New transient attack parameters do not change a network layout.

The original buff reports negative=true but both magic and physical=false.
`UnitRemoveBuffsEx` (`0x6f218dc0`) passes independent requested-type predicates
to `0x6f48eb10`; neither requested means no type restriction. Basic
`UnitRemoveBuffs` (`0x6f218d90`) requests neither. Corruption therefore resists
ordinary magical dispels yet can be removed by unfiltered negative-buff script
removal. `WC3_STATUS_BUFF_MAGIC_DISPEL_IMMUNE` records this independently from
the existing magical/physical flags. Native/browser saves are strictly
**166/27** because this retained policy changes; JASS remains 16. Older formats
reject rather than migrate.

The `corruption_*` and two `ocor_*` regressions cover stock and custom amounts,
native/ranked/item aliases, custom BuffID, first-hit melee and real missile
physics, launch-type retention, misses, target masks, manual suppression, held
orb selection, immunity/illusions, refresh/removal, script removal and live
status save/load. Whole-game, original feedback-art, complete `DataE`, extended
removal-category and all in-flight ownership combinations remain separate
verification requirements.

## Orb of Slow and Orb of Lightning

`AIsb` now has one `CAbilitySpellEffectBonus` owner. `AIll` resolves through
its authored `code=AIsb`; it is not separately registered as another proc.
Pickup/drop retain the concrete alias's additive `DataA` damage. A landed
non-manual attack selects the native alias/rank or the highest held item,
reads that row's `UnitID`, and dispatches the registered nested spell.
Chance comes from `DataB` for an ordinary unit, `DataC` for a Hero, or
`DataD` for a summoned unit. Hero classification takes priority over summon.
A zero-chance Lightning alias cannot acquire the separate base row's Slow.
Unknown/non-spell nested IDs produce an ownership-time diagnostic and no
substituted effect. Manual arrows, higher held orbs, dropped items and
carry-only inventories suppress the item proc through normal attack dispatch.

Historical 1.27.1.7085 evidence, SHA-256
`d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`:
`CAbilitySpellEffectBonus` vtable `0x6fb2cc90`, chance selector
`0x6f55ba50`/`0x6f55ba6b`, DataB/C/D reader `0x6f558ba0`, and nested
`UnitID` getter `0x6f4184b0` in the replacement/factory path `0x6f56ba30`.
The original `IsUnitType(10)` mask lookup at `0x6f1f89c0` maps to
unit `+0x248 & 4`; the supplied 3.0.1 `common.j` independently labels
unit type 10 as Summoned. Raw methods stop at the first return, so branch
continuations are retained separately rather than inferred from adjacent code.

Legacy nested `AIos` (`code=Aslo`) and authored aliases retain their own
BuffID, data, normal/Hero durations, and negative magical removal policy.
Custom Slow tokens now own movement/attack-rate descriptors; stock `Bslo`
keeps its existing movement consumer so it is not multiplied twice.
`CBuffSlow`'s original magic/physical/negative/positive virtuals at
`0x6f41d400`, `0x6f41d5d0`, `0x6f5195b0`, `0x6f41d650` return
true/false/true/false. Strict saves are native **167**, browser **28**,
JASS **16**; no network-state width changed and old formats reject.

There are 23 new production-entry regressions per edition, including
contrasting nested aliases, Hero/summon chance, native rank two, full base
and Lightning rows together, target masks, item priority/drop/carry-only,
manual arrows, custom movement, expiry, magic dispel and actual save/load.
A distinct ROC-shaped fixture uses `Data11`/`Data12`/`Data13`/`Data14`,
`Data22` and omitted BuffID, rather than merely rerunning a TFT schema.
Three initial regressions failed on the R80 implementation in both editions;
invalid early fixture-column attempts are retained separately. Existing Slow
and Purge fixtures now explicitly author their forced Hero/summon chances
and create a world-active Hero, rather than relying on missing fields/flags.

The current supplied data has `AIsb.DataB=25`, `DataC=10`, `DataD=25`,
`UnitID=AIno`; `AIll` has 30/10/30 and `UnitID=AIpg`. `AIpg` resolves
Purge; `AIno` has the newer **`code=Aosl`**, `BuffID=Bson`, 0.55/0.25
movement/attack reduction and 10/5-second duration. **Aosl remains
unimplemented**: it must not silently resolve to legacy Aslo. Its supplied
RTTI identifies separate `CAbilityOrbSlow`/`CBuffOrbSlow` classes, but the
relevant executable ranges are loader-protected. The supplied loader was
inspected read-only, not executed; its presence is not recovered semantics.
The original launch/impact decision phase, exact RNG/coefficient contract,
in-flight inventory changes, detached nested-rank independence, cross-family
BuffID collisions and complete current-version spell-immunity/feedback
parity still require evidence and implementation. This dispatch repair is
not complete Orb of Slow/Lightning or original multiplayer certification.

The `oven` item (`AIpb,Apo2`) also supplies an air-target missile attack when
the carrier has no enabled authored attack that can hit air. Its transient
profile uses the unit's Attack 1 damage, range, and timing with `Apo2`'s
authored `Abilities\\Spells\\Items\\OrbVenom\\OrbVenomMissile.mdl`; it does
not overwrite the unit's authored Attack 2.
Dropping the item removes the capability immediately because it is derived
from current inventory.

## Attack Damage Bonus Aliases

The passive item attack damage family is also handled by `CAbilityAttackBonus`.
TFT aliases `AIt6`, `AIt9`, `AItc`, `AItf`, `AItg`, `AIth`, `AIti`,
`AItj`, `AItk`, `AItl`, `AItn`, and `AItx` share `AIat`'s `DataA`
pickup/removal behavior. Their values come from each alias's own AbilityData
row, so an item carrying `AItg` uses its authored +1 while `AItx` uses +20.
ROC includes the `+6`, `+9`, `+12`, and `+15` aliases; the remaining aliases
are TFT-only. The registry maps all twelve directly to the same procedure.

## Mask of Death and held-effect selection

At source baseline `b73421f9`, held `AIva` does not provide its life steal.
`S_VampiricLifeSteal` only queries the Dreadlord aura and is melee-gated;
`S_OrbOnHit` does not include `AIva`. The original supplied 3.0.1 item `modt`
carries `AIva`, whose `DataA1=0.5` and targets are `air,ground,enemy`.
The [original Blizzard artifact guide](https://classic.battle.net/war3/basics/heroitemsartifact.shtml)
describes half the attack damage returning as life. It also treats the Mask
as an orb: the highest inventory effect wins; an explicitly activated attack
modifier overrides it while an autocast modifier is overridden by the orb.

`CAbilityVampireBonus` owns physical target validation. `S_ItemLifeStealOnHit`
reads the actual held/direct ability alias and fraction after an ordinary
primary attack has passed mitigation. `T_DamageInternal` invokes it before
subtracting victim life or entering death/XP, with damage capped to the
victim's remaining life. Spells, secondary splash and blocked hits do not
enter that item-leech path. The entry source incarnation is checked again
after synchronous damage handlers, rejecting a dead or recycled source. Spell immunity uses
`game.config.misc`'s `MagicImmunesResistLeech`; illusion-source healing uses
`IllusionsCanRestoreLife`. Both retain the original explicit zero default;
an illusion victim is rejected by the original commit rule.

`S_ActiveItemOrb` chooses the first held item with an orb effect. Item damage
bonuses keep their acquisition/removal behavior. Held Frost/Corruption/Freeze,
Fire splash and Venom consult that selection. A successfully affordable
manual arrow suppresses held effects; an orb suppresses autocast arrows
without paying their mana. Failed manual Searing Arrows can fall back to the
ordinary orb attack. Native poison still uses its own path. Directly granted
ability combinations and in-flight inventory changes remain separate parity
questions rather than being inferred from the held-item rule.

Current `Units/AbilitySkin.txt` leaves `AIva.Targetart` empty and authors
`Specialart=Abilities\\Spells\\Undead\\VampiricAura\\VampiricAuraTarget.mdl`.
The hit plays that alias-resolved Special art on the carrier, including when
the carrier is already at maximum life. It uses the existing temporary linked
effect and target-generation cleanup, adding no persistent fields or callback
identities. Save formats remain native 165 / browser 26 / JASS 16; the network
contract is unchanged.

`retail_item_effect.mask_of_death_*` contains 31 production-impact regressions
with stock 0.5 and contrasting 0.37 data, legacy columns, ranged attacks,
aliases, overkill, max life, item drop, carry-only inventory, Frost/Fire/Venom
priority, manual/autocast arrows and failure, illusion/immunity constants,
authored feedback, attachment cleanup and pre-death observation. A lethal
fixture initializes the Hero at level one so XP does not initialize unrelated
stats; it uses the production death handler. Initial fixture compilation and
setup failures are retained separately from valid red regressions. Validation
and release status are recorded in the parent workspace's
`deploy/wc3-lan/proof/fullplay-20261114/r79-mask-mechanism-evidence.json`;
test definitions alone are not a published fix or visual acceptance.

### Original executable evidence for the candidate repair

The supplied 3.0.1 executable exposes the VampireBonus RTTI, but its relevant
machine-code ranges cannot be reliably decoded from the file. Do not treat
that inspection as recovered behavior. For readable historical evidence the
official Blizzard `War3TFT_127b_English.exe` patch was downloaded from
`http://ftp.blizzard.com/pub/war3x/patches/pc/War3TFT_127b_English.exe`.
Its nested `Patch_War3x.mpq` contains `game.dll` with a 24-byte updater prefix;
the unchanged PE payload reports **1.27.1.7085**, SHA-256
`d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`.
These are preferred VAs in that specific DLL, not offsets for 3.0.1:

| Evidence | Observed contract |
| --- | --- |
| `CAbilityVampireBonus` vtable `0x6fb2355c`, slot `+0x170` → `0x6f5434c0` | Committed attack callback multiplies a positive damage payload by the ability's stored fraction and applies the result through the owner's life-adjustment path. |
| Dispatcher `0x6f66a350`, caller `0x6f69ca80:0x6f69caa3–0x6f69cadf` | The payload is capped to the victim's current life before dispatch. It does not award excess life for overkill. |
| Registered `GetWidgetLife` at `0x6f204180:0x6f20419b` | Independently identifies the caller's `+0x12c` virtual getter as current life. |
| Registered `IsUnitIllusion` at `0x6f206310:0x6f206321` | Identifies unit flag `+0x5c & 0x40000000`; the VampireBonus commit/impact paths test it. |
| `0x6f543563` and vtable `+0x1f8` → `0x6f55e600` | Read `Misc.IllusionsCanRestoreLife` and `Misc.MagicImmunesResistLeech`, each with an explicit zero default. Do not substitute a blanket spell-immunity rule for this physical effect. |

Current supplied data separately confirms `AIva.DataA1=0.5` and
`targs1=air,ground,enemy`; the private archive view in the R79 brief preserves
the unified supplied files and is not presented as two historical archive sets.
The new fixture also uses legacy `Data11`/`Data13`/`Data14`/`targs` columns,
an inherited alias and a contrasting 0.37 fraction. Lifecycle cases cover a
ranged carrier, overkill, dropped item, carry-only inventory, higher/lower
Frost Orb, manual/autocast Searing Arrows, illusions, invulnerability and spell
damage. Passing the two simple healing cases alone cannot satisfy this matrix.

Historical executable evidence and the current configuration remain separate.
Retail 3.0.1 execution, arbitrary directly granted modifier combinations,
in-flight inventory changes and the complete damage-event sequence still need
their own evidence. The restored relative order before death has a focused
callback regression; no version-wide parity conclusion follows from the old DLL.

## Commands

```sh
build/bin/ability_audit -data 'data/Warcraft III' -raw AIob
make test-wc3-engine WC3_PATTERN='wc3_item_lifecycle.*'
```
