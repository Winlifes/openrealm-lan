#include "s_skills.h"

#define ID_ORB_FIRE MAKEFOURCC('A', 'I', 'f', 'b')
#define ID_ORB_LIGHTNING_OLD MAKEFOURCC('A', 'I', 'l', 'b')
#define ID_ORB_FROST MAKEFOURCC('A', 'I', 'o', 'b')
#define ID_ORB_POISON MAKEFOURCC('A', 'I', 'p', 'b')
#define ID_ORB_CORRUPTION MAKEFOURCC('A', 'I', 'c', 'b')
#define ID_ORB_FREEZE MAKEFOURCC('A', 'I', 'z', 'b')
#define ID_ORB_LIGHTNING_TFT MAKEFOURCC('A', 'I', 'l', 'l')
#define ID_ORB_SLOW MAKEFOURCC('A', 'I', 's', 'b')
#define ID_ITEM_LIFE_STEAL MAKEFOURCC('A', 'I', 'v', 'a')
#define ID_ITEM_VENOM MAKEFOURCC('A', 'p', 'o', '2')

static uint32_t const orb_codes[] = {
    ID_ORB_FIRE, ID_ORB_LIGHTNING_OLD, ID_ORB_LIGHTNING_TFT, ID_ORB_FROST, ID_ORB_POISON, ID_ORB_CORRUPTION, ID_ORB_FREEZE,
};

/* Passive item stat bonuses — apply on pickup, reverse on drop.
 * Follows WarSmash pattern: CAbilityItemAttackBonus.onAdd/onRemove,
 * CAbilityItemDefenseBonus.onAdd/onRemove, etc. */

static void apply_attack(edict_t *unit, float amount) {
    G_ApplyTemporaryAttackDamageBonus(unit, amount);
}

static void apply_defense(edict_t *unit, float amount) {
    G_ApplyTemporaryArmorBonus(unit, amount);
}

static void apply_life(edict_t *unit, float amount) {
    G_ApplyTemporaryMaxHealthBonus(unit, amount);
}

static void apply_mana(edict_t *unit, float amount) {
    G_ApplyTemporaryMaxManaBonus(unit, amount);
}

/* Arel: DataA hit points per second (Ring of Regeneration). */
static void apply_life_regen(edict_t *unit, float amount) {
    G_InitUnitState(unit);
    unit->unit_state->health_regen_bonus += amount;
}

/* Attribute aliases share the authored Agility/Intelligence/Strength field order used by tomes. */
static void apply_stat(edict_t *unit, uint32_t code, float sign) {
    float str = sign * S_SpellData(code, 1, 3);
    float agi = sign * S_SpellData(code, 1, 1);
    float intel = sign * S_SpellData(code, 1, 2);
    if (!G_UnitIsHero(unit)) {
        return;
    }
    unit->unit_state->hero.bonus_str += (int32_t)str;
    unit->unit_state->hero.bonus_agi += (int32_t)agi;
    unit->unit_state->hero.bonus_intel += (int32_t)intel;
    unit->unit_state->hero.str = (uint32_t)MAX(0, (int32_t)unit->unit_state->hero.str + (int32_t)str);
    unit->unit_state->hero.agi = (uint32_t)MAX(0, (int32_t)unit->unit_state->hero.agi + (int32_t)agi);
    unit->unit_state->hero.intel = (uint32_t)MAX(0, (int32_t)unit->unit_state->hero.intel + (int32_t)intel);
    G_RecomputeHeroStats(unit);
}

/* Item bonus abilities also work on a unit directly: UnitAddAbility sends
 * A_ENABLE and UnitRemoveAbility A_DISABLE. */
static bool S_ItemBonusMessage(abilityMsg_t msg) {
    return msg == A_ITEM_ADD || msg == A_ITEM_REMOVE || msg == A_ENABLE || msg == A_DISABLE;
}

/* Inventory messages retain the authored alias, including distinct values on stacked items. */
#define BZ_ITEM_BONUS_PROC(NAME, APPLY) \
    BZ_ABILITY_PROC(C##NAME) { \
        if (!S_ItemBonusMessage(msg)) return CAbilityPassive(ent, msg, call); \
        if (!ent || !call || !call->item) return false; \
        float sign = msg == A_ITEM_ADD || msg == A_ENABLE ? 1.0f : -1.0f; \
        APPLY(ent, sign * S_SpellData(call->item->code, 1, 1)); \
        return true; \
    }

BZ_ITEM_BONUS_PROC(AbilityAttackBonus, apply_attack)
BZ_ITEM_BONUS_PROC(AbilityDefenseBonus, apply_defense)
BZ_ITEM_BONUS_PROC(AbilityMaxLifeBonus, apply_life)
BZ_ITEM_BONUS_PROC(AbilityMaxManaBonus, apply_mana)
BZ_ITEM_BONUS_PROC(AbilityLifeRegenBonus, apply_life_regen)

/* Stock Boots share one strongest flat movement benefit. Attack-speed and
 * intrinsic mana-regeneration fractions add across carried instances. Keep
 * concrete aliases/ranks so custom data is not replaced by stock constants. */
static float item_rate_amount(edict_t *unit, abilityCall_t const *call) {
    uint32_t const rank = call->source_item ? 1 : MAX(1, G_UnitAbilityLevel(unit, call->item->code));
    return S_SpellData(call->item->code, rank, 1);
}

BZ_ABILITY_PROC(CAbilityItemMoveSpeedBonus) {
    float amount;
    if (msg != A_RATE_BONUS || !call || !call->item || !call->rates) return CAbilityPassive(ent, msg, call);
    amount = item_rate_amount(ent, call);
    if (!call->rates->has_move_speed || amount > call->rates->move_speed_flat) call->rates->move_speed_flat = amount;
    call->rates->has_move_speed = true;
    return true;
}

BZ_ABILITY_PROC(CAbilityItemAttackSpeedBonus) {
    if (msg != A_RATE_BONUS || !call || !call->item || !call->rates) return CAbilityPassive(ent, msg, call);
    call->rates->attack_speed_fraction += item_rate_amount(ent, call);
    return true;
}

BZ_ABILITY_PROC(CAbilityItemManaRegenBonus) {
    if (msg != A_RATE_BONUS || !call || !call->item || !call->rates) return CAbilityPassive(ent, msg, call);
    call->rates->mana_regen_fraction += item_rate_amount(ent, call);
    return true;
}

unitRateBonuses_t S_UnitRateBonuses(edict_t *unit) {
    unitRateBonuses_t rates = {0};
    abilityCall_t call = MAKE(abilityCall_t, .rates = &rates);
    if (!unit) return rates;
    S_UnitAbilityEventWithCall(unit, A_RATE_BONUS, &call);
    if (!G_InventoryCanUseItems(unit)) return rates;
    FOR_LOOP(slot, G_InventoryCapacity(unit)) {
        edict_t *item = G_InventoryItem(unit, slot);
        cstring_t abilities = item ? G_ItemAbilityList(item) : NULL;
        if (!abilities) continue;
        PARSE_LIST(abilities, name, parse_segment) {
            abilityitem_t owner;
            if (strlen(name) != 4) continue;
            owner = S_AbilityItem(FS_SLKKey(name));
            if (!owner.ability) continue;
            call.item = &owner;
            call.source_item = item;
            S_AbilityMessage(unit, A_RATE_BONUS, &call);
        }
    }
    return rates;
}

/* Read every attribute from the same alias on acquisition and removal. */
BZ_ABILITY_PROC(CAbilityAttributeBonus) {
    if (!S_ItemBonusMessage(msg)) return CAbilityPassive(ent, msg, call);
    if (!ent || !call || !call->item) return false;
    float sign = msg == A_ITEM_ADD || msg == A_ENABLE ? 1.0f : -1.0f;
    apply_stat(ent, call->item->code, sign);
    return true;
}

/* Item orb family (AIfb/AIlb/AIob/AIpb/AIcb/AIzb, retail parent AIDB).
 * Bonus damage rides CAbilityAttackBonus pickup handling via DataA; this owns
 * the on-hit BuffID state next to S_SlowPoisonOnHit. ROC omits BuffID, so the
 * three buffed orbs fall back to their TFT tokens. AIfb/AIlb/AIpb author no
 * buff in either version. The status system consumes Frost's movement and
 * attack-rate slow. Corruption instead owns its pre-damage armor modifier. */

/* Retail's highest occupied orb slot owns its effect. Passive attack bonuses
 * still accumulate through their ordinary acquisition/removal messages. */
edict_t *S_ActiveItemOrb(edict_t *unit) {
    if (!unit || !G_InventoryCanUseItems(unit)) return NULL;
    FOR_LOOP(slot, G_InventoryCapacity(unit)) {
        edict_t *item = G_InventoryItem(unit, slot);
        cstring_t abilities = item ? G_ItemAbilityList(item) : NULL;
        if (!abilities) continue;
        PARSE_LIST(abilities, name, parse_segment) {
            uint32_t const code = strlen(name) == 4 ? FS_SLKKey(name) : 0;
            uint32_t const base = code ? G_AbilityCode(code) : 0;
            if (base == ID_ITEM_LIFE_STEAL || base == ID_ITEM_VENOM || base == ID_ORB_SLOW) return item;
            FOR_LOOP(o, sizeof(orb_codes) / sizeof(orb_codes[0]))
                if (code == orb_codes[o] || base == orb_codes[o]) return item;
        }
    }
    return NULL;
}

static uint32_t item_ability(edict_t const *item, uint32_t ability) {
    cstring_t abilities = item ? G_ItemAbilityList(item) : NULL;
    if (!abilities) return 0;
    PARSE_LIST(abilities, name, parse_segment) {
        uint32_t const code = strlen(name) == 4 ? FS_SLKKey(name) : 0;
        if (code == ability || (code && G_AbilityCode(code) == ability)) return code;
    }
    return 0;
}

BZ_ABILITY_PROC(CAbilityVampireBonus) {
    cstring_t illusions, immune;
    edict_t *target;
    if (msg != A_VALIDATE || !call || !call->item || !call->target) return CAbilityPassive(ent, msg, call);
    target = call->target->entity;
    if (!S_SpellAllowsAttackTarget(call->item->code, ent, target)) return false;
    illusions = Stb_IniCacheFind(&game.config.misc, "Misc", "IllusionsCanRestoreLife");
    immune = Stb_IniCacheFind(&game.config.misc, "Misc", "MagicImmunesResistLeech");
    /* The original commit rejects an illusion victim. Its impact callback
     * additionally gates illusion-source healing with this zero-default key. */
    if ((target->aiflags & AI_ILLUSION) ||
        ((ent->aiflags & AI_ILLUSION) && (!illusions || !atoi(illusions)))) return false;
    return !immune || !atoi(immune) || !S_UnitSpellImmune(target);
}

void S_ItemLifeStealOnHit(edict_t *attacker, edict_t *target, float damage) {
    abilityAliasRef_t ref = S_ResolveAbilityAlias(attacker, ID_ITEM_LIFE_STEAL);
    edict_t *item = S_ActiveItemOrb(attacker);
    abilityitem_t ability;
    spellTarget_t st = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target);
    abilityCall_t call;
    if (item) {
        ref.alias = item_ability(item, ID_ITEM_LIFE_STEAL);
        ref.level = ref.alias ? 1 : 0;
    }
    if (!ref.alias || !ref.level || damage <= 0) return;
    ability = S_AbilityItem(ref.alias);
    call = MAKE(abilityCall_t, .item = &ability, .target = &st);
    if (!S_AbilityMessage(attacker, A_VALIDATE, &call)) return;
    G_AddHealth(attacker, MIN(target->health.value, damage) * S_SpellData(ref.alias, ref.level, 1));
    G_SpawnAbilityEffectTarget(ref.alias, WC3_EFFECT_SPECIAL, 0, attacker, "origin", true);
}

/* The orb owns this splash independently of innate Flak/Storm/Barrage. A
 * manually committed arrow can therefore suppress only the item effect. */
void S_OrbAttackSplash(edict_t *attacker, edict_t *target, int damage) {
    abilityAliasRef_t ref = S_ResolveAbilityAlias(attacker, ID_ORB_FIRE);
    float radius;
    if (!ref.level) {
        ref.alias = item_ability(S_ActiveItemOrb(attacker), ID_ORB_FIRE);
        ref.level = ref.alias ? 1 : 0;
    }
    if (!ref.level) return;
    radius = S_SpellNumber(ref.alias, ABILITY_NUMBER_AREA, ref.level);
    if (radius <= 0) return;
    FILTER_EDICTS(other, other != target && S_SpellIsAliveTarget(other) && S_SpellIsEnemy(attacker, other) &&
                  S_AttackHitAllowed(attacker, target, other) &&
                  Vector2_distance(&other->s.origin2, &target->s.origin2) <= radius)
        T_Damage(other, attacker, damage);
}

/* Held AIsr bracers reduce spell damage by their authored percentage. Use the
 * strongest carried reduction once; duplicate item copies do not compound. */
float S_ItemSpellDamageReduction(edict_t const *unit) {
    float reduction = 0.0f;
    if (!unit || !G_InventoryCanUseItems(unit)) return 0.0f;
    FOR_LOOP(slot, G_InventoryCapacity(unit)) {
        edict_t *item = G_InventoryItem((edict_t *)unit, slot);
        cstring_t abilities = item ? G_ItemAbilityList(item) : NULL;
        if (!abilities) continue;
        PARSE_LIST(abilities, name, parse_segment) {
            uint32_t const code = strlen(name) == 4 ? FS_SLKKey(name) : 0;
            float amount;
            if (code != MAKEFOURCC('A','I','s','r')) continue;
            amount = S_SpellData(code, 1, 2); /* Runed Bracers author reduction as DataB. */
            if (amount > 1.0f) amount *= 0.01f;
            reduction = MAX(reduction, MIN(1.0f, MAX(0.0f, amount)));
        }
    }
    return reduction;
}

static cstring_t orb_buff(uint32_t code) {
    static struct { uint32_t code; cstring_t buff; } const fallback[] = {
        { ID_ORB_FROST, "Bfro" },
        { ID_ORB_FREEZE, "Bfre" },
    };
    cstring_t buff = S_SpellBuffId(code, 1);
    if (buff && strlen(buff) >= 4) return buff;
    FOR_LOOP(i, sizeof(fallback) / sizeof(fallback[0]))
        if (fallback[i].code == G_AbilityCode(code)) return fallback[i].buff;
    return NULL;
}

static void orb_apply(edict_t *attacker, edict_t *target, uint32_t orb, uint32_t *seen, uint32_t *count) {
    cstring_t buff;
    uint32_t level;
    /* Corruption owns its pre-mitigation impact, not this post-damage path. */
    if (G_AbilityCode(orb) == ID_ORB_CORRUPTION) return;
    FOR_LOOP(i, *count)
        if (seen[i] == orb) return;
    buff = orb_buff(orb);
    if (!buff) return;
    level = MAX(1, G_UnitAbilityLevel(attacker, orb));
    seen[(*count)++] = orb;
    S_SpellApplyTimedStatus(target, buff, level, S_SpellHeroDuration(orb, level, target));
}

/* Retail CBuffCorruption applies a signed armor contribution, replaces it on
 * refresh, and restores it on removal. Existing status descriptors provide
 * that lifecycle and serialization without changing unit or network layouts. */
BZ_ABILITY_PROC(CAbilityCorruptionDamageBonus) {
    edict_t *target;
    uint32_t code, rank;
    cstring_t buff;
    heroabilitystatus_t *status;
    float amount;
    if (msg != A_EXECUTE) return CAbilityAttackBonus(ent, msg, call);
    if (!ent || !call || !call->item || !call->target) return false;
    target = call->target->entity;
    code = call->item->code;
    if (!S_SpellAllowsAttackTarget(code, ent, target) || S_UnitInvulnerable(target)) return false;
    rank = call->source_item ? 1 : MAX(1, G_UnitAbilityLevel(ent, code));
    buff = S_SpellBuffId(code, rank);
    /* The original class selector returns intrinsic BIcb even when legacy
     * AbilityData omits BuffID; an authored custom BuffID still takes priority. */
    if (!buff || strlen(buff) < 4) buff = "BIcb";
    status = S_SpellApplyTimedTargetStatus(target, code, rank, buff, S_SpellHeroDuration(code, rank, target));
    if (!status) return false;
    status->source = ent;
    status->source_spawn_time = ent->spawn_time;
    /* Retail Corruption reports negative=true, magic=false, physical=false.
     * Priest/Destroyer dispels retain it; unfiltered script removal does not. */
    status->buff_flags |= WC3_STATUS_BUFF_NEGATIVE | WC3_STATUS_BUFF_MAGIC_DISPEL_IMMUNE;
    /* Original ability preparation supplies zero for an illusion attacker;
     * the buff's apply/refresh/removal separately skip illusion receivers. */
    amount = ((ent->aiflags | target->aiflags) & AI_ILLUSION) ? 0 : -S_SpellData(code, rank, 2);
    if (!S_SpellStatusModifier(status, WC3_STATUS_MOD_ARMOR_FLAT, amount)) {
        fprintf(stderr, "Corruption: no authored armor modifier capacity for %u\n", target->s.number);
        return false;
    }
    G_InvalidateUnitInfoPanel(target);
    return true;
}

/* Only the selected held orb supplies an item modifier; native aliases keep
 * their own rank. Spell immunity is not a physical-attack target rejection. */
void S_CorruptionOnHit(edict_t *attacker, edict_t *target) {
    abilityAliasRef_t ref = S_ResolveAbilityAlias(attacker, ID_ORB_CORRUPTION);
    edict_t *item = NULL;
    abilityitem_t ability;
    spellTarget_t st = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target);
    if (!ref.level) {
        item = S_ActiveItemOrb(attacker);
        ref.alias = item_ability(item, ID_ORB_CORRUPTION);
        ref.level = ref.alias ? 1 : 0;
    }
    if (!ref.alias || !ref.level) return;
    ability = S_AbilityItem(ref.alias);
    (void)S_AbilityMessage(attacker, A_EXECUTE, &MAKE(abilityCall_t, .item = &ability, .source_item = item, .target = &st));
}

/* AIsb and its AIll alias share one original SpellEffectBonus owner. UnitID
 * selects the nested spell; DataB/C/D select ordinary/Hero/summoned chance.
 * No target effect may be substituted when that authored handler is missing. */
BZ_ABILITY_PROC(CAbilitySpellEffectBonus) {
    edict_t *target;
    uint32_t code, rank, effect, chance_slot;
    float chance;
    abilityitem_t nested;
    abilityCall_t invoke;
    spellTargetType_t target_type;
    if (!ent || !call || !call->item) return false;
    code = call->item->code;
    rank = call->source_item ? 1 : MAX(1, G_UnitAbilityLevel(ent, code));
    effect = S_SpellUnitId(code, rank);
    nested = S_AbilityItem(effect);
    if (msg != A_EXECUTE) {
        if ((msg == A_ITEM_ADD || msg == A_ENABLE) &&
            (!nested.ability || !(nested.ability->flags & AB_SPELL))) {
            char owner_name[5] = {0}, effect_name[5] = {0};
            memcpy(owner_name, &code, 4); memcpy(effect_name, &effect, 4);
            fprintf(stderr, "SpellEffectBonus: %s has no implemented nested spell %s\n", owner_name, effect_name);
        }
        return CAbilityAttackBonus(ent, msg, call);
    }
    if (!call->target || call->target->type != SPELL_TARGET_UNIT) return false;
    target = call->target->entity;
    if (!S_SpellIsAliveTarget(target) || !S_SpellIsEnemy(ent, target) ||
        !S_SpellAllowsAttackTarget(code, ent, target) || S_UnitInvulnerable(target)) return false;
    if (!nested.ability || !(nested.ability->flags & AB_SPELL)) return false;
    /* Retail nested virtual +0x34c must admit flag 4 (unit target). A point
     * spell cannot consume the hit's entity union or acquire a channel. */
    target_type = S_SpellTargetTypeForCode(nested.ability, effect);
    if (target_type != SPELL_TARGET_UNIT && target_type != SPELL_TARGET_UNIT_OR_POINT) return false;
    chance_slot = G_UnitIsHero(target) ? 3 : target->summon_ability ? 4 : 2;
    chance = S_SpellData(code, rank, chance_slot);
    if (chance <= 0 || (chance < 100 && (float)(rand() % 100) >= chance)) return false;
    if (!S_SpellAllowsTarget(effect, ent, target)) return false;
    invoke = MAKE(abilityCall_t, .item = &nested, .target = call->target);
    /* This detached effect is not a player-issued spell order: its own mana,
     * cooldown, command-card ownership and research do not gate the orb hit. */
    if (!nested.ability->proc(ent, A_VALIDATE, &invoke)) return false;
    return nested.ability->proc(ent, A_EXECUTE, &invoke);
}

static void spell_effect_bonus_on_hit(edict_t *attacker, edict_t *target) {
    abilityAliasRef_t ref = S_ResolveAbilityAlias(attacker, ID_ORB_SLOW);
    edict_t *item = NULL;
    abilityitem_t ability;
    spellTarget_t st = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target);
    if (!ref.level) {
        item = S_ActiveItemOrb(attacker);
        ref.alias = item_ability(item, ID_ORB_SLOW);
        ref.level = ref.alias ? 1 : 0;
    }
    if (!ref.alias || !ref.level) return;
    ability = S_AbilityItem(ref.alias);
    (void)S_AbilityMessage(attacker, A_EXECUTE,
        &MAKE(abilityCall_t, .item = &ability, .source_item = item, .target = &st));
}

/* Called from S_ResolveAttackHit after a hit lands on an enemy. Checks native
 * orb ownership and held orb items; the same orb from both sources applies once. */
void S_OrbOnHit(edict_t *attacker, edict_t *target) {
    /* At most one native alias and one selected held rawcode per family. */
    uint32_t seen[2 * sizeof(orb_codes) / sizeof(orb_codes[0])];
    uint32_t count = 0;
    if (!attacker || !target || !S_SpellIsEnemy(attacker, target)) return;
    spell_effect_bonus_on_hit(attacker, target);
    FOR_LOOP(o, sizeof(orb_codes) / sizeof(orb_codes[0])) {
        /* Owned map aliases carry their own BuffID and duration. */
        abilityAliasRef_t const ref = S_ResolveAbilityAlias(attacker, orb_codes[o]);
        uint32_t const code = ref.alias ? ref.alias : orb_codes[o];
        if (G_UnitAbilityLevel(attacker, code)) orb_apply(attacker, target, code, seen, &count);
    }
    edict_t *item = S_ActiveItemOrb(attacker);
    cstring_t abilities = item ? G_ItemAbilityList(item) : NULL;
    if (!abilities) return;
    PARSE_LIST(abilities, name, parse_segment) {
        uint32_t code = strlen(name) == 4 ? FS_SLKKey(name) : 0;
        FOR_LOOP(o, sizeof(orb_codes) / sizeof(orb_codes[0]))
            if (code && code == orb_codes[o]) orb_apply(attacker, target, code, seen, &count);
    }
}
