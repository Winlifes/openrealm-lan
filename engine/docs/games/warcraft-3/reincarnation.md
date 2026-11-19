# Reincarnation and Ankh

`CAbilityReincarnation` owns `AOre`, `ACrn` and `ANrn` death replacement;
`CAbilityItemReincarnation` owns `AIrc`, including authored aliases. The
generic unit death path asks `A_DEATH_REPLACE` before final death accounting.
Unit abilities get the first opportunity, then usable inventory abilities
in slot order. Backpack inventory with `inv3/DataC=0` cannot activate an Ankh.
One successful replacement consumes one item use through `G_CompleteItemUse`,
including the original unit/item use events and retained event-handle lifetime.

## Original evidence and version

The supplied 3.0.1.24371 SD zhCN `AbilityData` has `AIrc` DataA=5 seconds,
DataB=500 restored life, and DataC=-1. `AbilityMetaData.slk` identifies these
as `Ircd`, `irc2`, and `irc3`. Original `common.j` explicitly names `irc3`
`ABILITY_ILF_RESTORED_MANA__1_FOR_CURRENT`: -1 retains the dying unit's mana.
Other values restore that authored amount, bounded by the unit's current max.
The data is not replaced with a fixed delay or full-life/full-mana shortcut.

The [original Blizzard item guide](https://classic.battle.net/war3/basics/heroitemsconsumed.shtml)
describes 500 life with a historical seven-second delay. Blizzard's
[1.36.2 patch notes](https://us.forums.blizzard.com/en/warcraft3/t/warcraft-iii-reforged-patch-notes-version-1362/32218/1)
reduced that delay to five seconds; the current extracted row agrees.
The [original Tauren Chieftain guide](https://classic.battle.net/war3/orc/units/taurenchieftain.shtml)
specifies full mana and no kill XP when Reincarnation is used. The
[original building guide](https://classic.battle.net/war3/basics/buildings.shtml)
explicitly keeps both a reincarnating Chieftain and an Ankh carrier alive for
rally/follow purposes. Final death retains the normal invalidation path.

Original WarChasers grants `ankh`, listens to `EVENT_PLAYER_UNIT_USE_ITEM` to
announce its use, and excludes carried Ankhs from its hero-death loss check.
The published R77 Edge session reproduced both heroes staying dead because
held `AIrc` was unregistered and never scheduled. The existing innate-only
helper also ran after final death events, food removal and kill accounting.
It is replaced by the owning procedures rather than editing the map script.

The precise retail callback micro-order and priority with arbitrary scripted
death replacements have not been compared to an original executable. The
implemented death-replacement boundary is corroborated by the original map,
Blizzard's XP/rally contracts and Warsmash's separate replacement phase; this
does not certify every original edge case or audiovisual detail.

## Ownership, cancellation and persistence

The original unit retains `reincarnation_t` (component 35), containing the
pending thinker identity, restored vitals and prior corpse-policy flags.
The existing `reincarnation_think` callback owns the delay and tombstone.
`channel_t` carries owner and marker incarnation tokens, resolved with
`S_SpellChannelOwner/Target`. A removed/reused owner or marker cannot be used
as an unrelated target. Unit removal and external revival cancel the owned
thinker/marker; a later ordinary death cannot reuse that old return.

Pending units keep food and rally identity, produce no final-death event,
kill score, bounty or XP, and cannot enter ordinary corpse/altar progression.
`A_CORPSE_DECAY` lets the owning ability finish the death presentation while
waiting. Returning revives the same edict, clears death/animation locks,
restores configured vitals and retires the component. Normal final death is
unchanged when no eligible replacement accepts it.

Native save 165 / browser save 26 persist the new component and its `F_EDICT`
thinker fixup. JASS snapshot 16, callback identity and network layouts remain
unchanged. Older native/browser saves are rejected without migration. Saving
in the pending delay, restoring after pointer/vital changes, rejecting an
invalid thinker pointer and advancing the real entity scheduler are covered.

## Regression and acceptance

`retail_item_effect.ankh*` covers stock and non-stock data, alias lookup,
death/use events, food, rally and follow, repeated death, two items, innate
cooldown priority, carry-only inventory, external revival, unit removal and
pending whole-world save/load. Run it in both ROC and TFT with
`make WC3_PATTERN='retail_item_effect.ankh*' test-wc3-engine`.

The initial tests failed against R77 before production changes. Passing these
isolated tests does not establish a complete browser match, all original
mechanisms, retail callback ordering or full-capacity performance. The R77
failure and subsequent candidate/Edge results remain separate deployment
receipts under `deploy/wc3-lan/proof/fullplay-20261114` in the parent workspace.
