# Hero revival

OpenRealm models Altar-style Hero revival as production of an existing Hero
entity, not as training a replacement unit.

## Capability

Revival capability is data-driven from the unit profile `Revive` (`urev`)
field. Any building whose normalized profile has a non-zero `Revive` value can
offer eligible dead Heroes owned by the same player. No Altar rawcode is
special-cased.

## Hero death state

Heroes keep their authoritative edict after death. When the death animation
finishes, `DissipateTime` is used for the Hero corpse/dissipation timer. At the
end of that timer the Hero is hidden and marked `revival.awaiting` instead of
being freed. While the `Dissipate` presentation runs, its temporary render alpha
falls linearly from fully opaque to transparent across `DissipateTime`. This
multiplies the Hero's authored/JASS vertex alpha only in the client datagram; it
does not mutate persistent `vertex_color` state. `EVENT_PLAYER_HERO_REVIVABLE`
and `EVENT_UNIT_HERO_REVIVABLE` are published at the final transition.

A dead Hero remains counted by `G_GetPlayerTechCountValue()`. This preserves
Hero/techtree limits while the Hero is dead. The same edict is not interactable as a
living unit during this period: `unit_die()` clears its selection mask and sets
`EF_NOT_SELECTABLE`, and `G_ReviveHero()` clears that flag when the Hero returns to its
living stand state. The shared death-animation, selection, and corpse-ordering contract is
documented in [WC3 Economy And Unit Presentation](economy-and-unit-presentation.md#unit-death-and-corpse-interaction).

## Command card and identity

A `Revive=true` building enumerates eligible dead Hero *instances*. Each button
uses the Hero unit's art and carries a `revive:<entity-number>` server command,
so multiple Heroes of the same type remain distinct. Queueing one Hero sets its
`reviving` flag immediately, which removes it from every other eligible
building's command card.

## Cost and time

The implementation reads the authoritative Misc gameplay constants:

```text
ReviveBaseFactor
ReviveLevelFactor
ReviveBaseLumberFactor
ReviveLumberLevelFactor
ReviveMaxFactor
ReviveTimeFactor
ReviveMaxTimeFactor
```

Gold uses the Hero's `goldCost`; lumber independently uses `lumberCost`. Cost
factors grow by Hero level and are capped by `ReviveMaxFactor`. Revival time is
the Hero's normal `buildTime * level * ReviveTimeFactor`, capped by
`ReviveMaxTimeFactor`.

Gold and lumber are charged when the Hero is inserted into the producer's
existing linked production queue. The Hero itself is the queue identity; a
separate `revival.queue_next` pointer avoids reusing its normal `build` field.
`revival.producer` identifies the queue that owns the Hero, while
`revival.player` records the player actually charged so cancellation cannot
refund a later owner after either entity changes ownership.

## Completion and cancellation

Only the queue head progresses. On completion the normal deterministic producer
exit search (`SP_FindUnitExitPosition`) finds a legal point beside the Altar,
then `G_ReviveHero()` restores the same Hero object. The scripted revive helper
also clears hidden/revival state and applies both mana terms:

```text
max mana * HeroReviveManaFactor
+ initial mana * HeroReviveManaStart
```

`G_ReviveHero()` returns false unless the edict is an in-use, dead Hero outside
Soul Trap; it returns true after restoring the Hero. JASS `ReviveHero` and
`ReviveHeroLoc` return that result, so a living Hero or ordinary unit is not
reported as revived. Orc08's `Trig_Grom_Dead` can therefore revive Grom from
its actual Player 1 death event when the Soul Gem was not used.

The finish event is published after the Hero is alive and positioned. An active
revival exposes the existing command-card Cancel action; cancellation refunds
the exact gold/lumber charged, clears `reviving`, and leaves the Hero awaiting
revival. Destroying a producer cancels/refunds all Hero revivals in its mixed
production chain and then cancels ordinary trainees. Removing a queued Hero,
scripted revival, and ownership transfer detach it through the same cancellation
path before the edict or owner state changes.

## AI production

The supplied `common.ai` `StartUnit` computes a `TownCount` deficit and calls
`SetProduce`; it has no separate revival native. `G_BotProduce` therefore first
looks for an owned, dead instance of the requested Hero type awaiting revival.
It selects a completed owned producer with revival capability at the requested
town, balancing queue space as for training, and calls `G_QueueHeroRevive`.
That shared queue owns eligibility, payment, timing, cancellation and completion.
No new Hero, free resource grant or AI policy-script change is involved.

`BotUnitCount` and `G_BotTownUnitCount` include a queued revival when `done=false`,
as they include a trainee. An unqueued corpse and a revival with `done=true`
are excluded. A pending revival belongs to its producer's town, not the town
nearest the corpse; cancellation removes it from pending counts.

Original 3.0.1 starter-map headless diagnostics confirmed the former failure:
after the Farseer died, production repeatedly found one hidden train command,
zero living matching Heroes, and one dead/awaiting/not-reviving Hero. The
existing Hero still consumed its techtree limit, so duplicate training could
never recover it. Trace events `hero_revive_started` / `hero_revive_completed`
now retain producer/entity identity, level, and the charged cost/time; production
shortfalls retain matching live/dead/awaiting/reviving counts.

Regression coverage verifies same-instance revival with normal costs and queue
ticks, retained level, insufficient gold/lumber, duplicate request rejection,
and town/global pending-versus-done counts. Headless simulation is separate
from rendered browser acceptance and does not establish full retail AI parity.

## Remaining gaps

- Exact proper-Hero-name formatting and a Hero-level number overlay are not yet
  represented by the current command-button wire format.
- Revival tooltip resource icons/cost fields are not represented separately by
  `gameCommandButton_t`; authoritative charging is implemented server-side.
- Altar Rally is applied after revive-finish events through the shared producer `G_ApplyRallyOrder`; see [rally-points.md](rally-points.md).
- Tavern/instant Hero awakening remains a separate future mechanic.
