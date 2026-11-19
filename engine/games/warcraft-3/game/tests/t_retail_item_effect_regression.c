/* Retail-first item effect regressions. These intentionally use real gameplay
 * entry points and assert observable results, including currently failing
 * behavior, not only inventory aliases or presence of BuffID.  The synthetic
 * SLKs are minimal transports for the audited rawcodes and data fields. */
#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *text);
void free_slk_rows(slkTestData_t *rows);
void attack_melee_cooldown(edict_t *self);
void attack_melee(edict_t *self);
void attack_ranged(edict_t *self);

typedef struct {
    slkTestData_t *ability_rows, *item_rows, *old_abilities, *old_items;
    UnitAbilities_t hero_abilities;
    edict_t *hero, *enemy;
} retailEffectFixture_t;

typedef enum { ANKH_NORMAL, ANKH_SAVE, ANKH_REMOVE, ANKH_EXTERNAL_REVIVE, ANKH_TWO_ITEMS,
               ANKH_ALIAS, ANKH_ABILITY_FIRST, ANKH_CARRY_ONLY } ankhMode_t;
typedef struct { int delay, life, mana; ankhMode_t mode; } ankhTest_t;
typedef enum { MASK_NORMAL, MASK_ROC, MASK_RANGED, MASK_ALIAS, MASK_OVERKILL, MASK_DROP,
               MASK_CARRY_ONLY, MASK_FIRST, MASK_FROST_FIRST, MASK_MANUAL, MASK_AUTOCAST,
               MASK_ILLUSION, MASK_VICTIM_ILLUSION, MASK_INVULNERABLE, MASK_SPELL,
               MASK_FIRE_FIRST, MASK_FIRE_LOWER, MASK_MANUAL_FIRE, MASK_MANUAL_NO_MANA, MASK_CAP,
               MASK_POISON_FIRST, MASK_POISON_LOWER, MASK_MANUAL_POISON, MASK_ILLUSION_ENABLED,
               MASK_IMMUNE, MASK_IMMUNE_RESISTS, MASK_EFFECT, MASK_EFFECT_REMOVE, MASK_EFFECT_FULL,
               MASK_DEATH_ORDER } maskMode_t;
typedef struct { float ratio; maskMode_t mode; } maskTest_t;
typedef enum { CORRUPT_STOCK, CORRUPT_ALIAS, CORRUPT_CUSTOM_BUFF, CORRUPT_NATIVE, CORRUPT_RANK,
               CORRUPT_REFRESH, CORRUPT_DISPEL, CORRUPT_DROP, CORRUPT_SAVE, CORRUPT_MELEE,
               CORRUPT_MANUAL, CORRUPT_IMMUNE, CORRUPT_SOURCE_ILLUSION, CORRUPT_VICTIM_ILLUSION,
               CORRUPT_AIR, CORRUPT_INVULNERABLE, CORRUPT_CARRY, CORRUPT_FROST_FIRST,
               CORRUPT_RANGED, CORRUPT_RANGED_TYPE, CORRUPT_MISS, CORRUPT_MAGIC_DISPEL,
               CORRUPT_BASIC_REMOVE, CORRUPT_EX_REMOVE, CORRUPT_WEAKER, CORRUPT_HERO } corruptMode_t;
typedef enum {
    NESTED_ZERO, NESTED_HERO_SUCCESS, NESTED_SUMMON_ZERO, NESTED_SUMMON_SUCCESS,
    NESTED_SUMMON_HERO, NESTED_NATIVE_RANK, NESTED_DROP, NESTED_EXPIRE,
    NESTED_DISPEL, NESTED_SAVE, NESTED_OUTER_AIR, NESTED_INNER_AIR,
    NESTED_MANUAL, NESTED_FROST_PRIORITY, NESTED_UNSUPPORTED, NESTED_CYCLE,
    NESTED_LEGACY, NESTED_LIGHTNING_ZERO, NESTED_LIGHTNING_ONCE, NESTED_CARRY
} nestedMode_t;
static char const nested_hit_items[] = "ID;PWXL;N;EBB;Y2;X3\n"
    "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\n"
    "C;Y2;X1;K\"oslo\"\nC;Y2;X2;K\"A900\"\nC;Y2;X3;K\"true\"\nE\n";

static char const nested_modern_slk[] =
    "ID;PWXL;N;EBB;Y10;X16\n"
    "C;Y1;X1;K\"alias\"\n"
    "C;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"DataA1\"\n"
    "C;Y1;X5;K\"DataB1\"\n"
    "C;Y1;X6;K\"DataC1\"\n"
    "C;Y1;X7;K\"DataD1\"\n"
    "C;Y1;X8;K\"DataE1\"\n"
    "C;Y1;X9;K\"UnitID1\"\n"
    "C;Y1;X10;K\"Dur1\"\n"
    "C;Y1;X11;K\"HeroDur1\"\n"
    "C;Y1;X12;K\"BuffID1\"\n"
    "C;Y1;X13;K\"targs1\"\n"
    "C;Y1;X14;K\"DataB2\"\n"
    "C;Y1;X15;K\"UnitID2\"\n"
    "C;Y1;X16;K\"targs2\"\n"
    "C;Y2;X1;K\"AInv\"\n"
    "C;Y2;X2;K\"AInv\"\n"
    "C;Y2;X3;K\"1\"\n"
    "C;Y2;X4;K\"6\"\n"
    "C;Y2;X6;K\"1\"\n"
    "C;Y2;X7;K\"1\"\n"
    "C;Y2;X8;K\"1\"\n"
    "C;Y3;X1;K\"AIv0\"\n"
    "C;Y3;X2;K\"AInv\"\n"
    "C;Y3;X3;K\"1\"\n"
    "C;Y3;X4;K\"6\"\n"
    "C;Y3;X6;K\"0\"\n"
    "C;Y3;X7;K\"1\"\n"
    "C;Y3;X8;K\"1\"\n"
    "C;Y4;X1;K\"AIsb\"\n"
    "C;Y4;X2;K\"AIsb\"\n"
    "C;Y4;X3;K\"1\"\n"
    "C;Y4;X4;K\"13\"\n"
    "C;Y4;X5;K\"100\"\n"
    "C;Y4;X6;K\"100\"\n"
    "C;Y4;X7;K\"100\"\n"
    "C;Y4;X8;K\"2\"\n"
    "C;Y4;X9;K\"AIos\"\n"
    "C;Y4;X13;K\"ground,air,ward\"\n"
    "C;Y5;X1;K\"A900\"\n"
    "C;Y5;X2;K\"AIsb\"\n"
    "C;Y5;X3;K\"2\"\n"
    "C;Y5;X4;K\"19\"\n"
    "C;Y5;X5;K\"%d\"\n"
    "C;Y5;X6;K\"%d\"\n"
    "C;Y5;X7;K\"%d\"\n"
    "C;Y5;X8;K\"2\"\n"
    "C;Y5;X9;K\"%s\"\n"
    "C;Y5;X13;K\"ground,air,ward\"\n"
    "C;Y5;X14;K\"100\"\n"
    "C;Y5;X15;K\"A901\"\n"
    "C;Y5;X16;K\"ground,air,ward\"\n"
    "C;Y6;X1;K\"AIos\"\n"
    "C;Y6;X2;K\"Aslo\"\n"
    "C;Y6;X3;K\"1\"\n"
    "C;Y6;X4;K\".4\"\n"
    "C;Y6;X5;K\".3\"\n"
    "C;Y6;X10;K\"7\"\n"
    "C;Y6;X11;K\"3\"\n"
    "C;Y6;X12;K\"Bslo\"\n"
    "C;Y6;X13;K\"ground,air,enemy\"\n"
    "C;Y7;X1;K\"A901\"\n"
    "C;Y7;X2;K\"Aslo\"\n"
    "C;Y7;X3;K\"1\"\n"
    "C;Y7;X4;K\".67\"\n"
    "C;Y7;X5;K\".43\"\n"
    "C;Y7;X10;K\"13\"\n"
    "C;Y7;X11;K\"6\"\n"
    "C;Y7;X12;K\"B901\"\n"
    "C;Y7;X13;K\"%s\"\n"
    "C;Y8;X1;K\"AIll\"\n"
    "C;Y8;X2;K\"AIsb\"\n"
    "C;Y8;X3;K\"1\"\n"
    "C;Y8;X4;K\"5\"\n"
    "C;Y8;X5;K\"%d\"\n"
    "C;Y8;X6;K\"100\"\n"
    "C;Y8;X7;K\"100\"\n"
    "C;Y8;X8;K\"2\"\n"
    "C;Y8;X9;K\"AIpg\"\n"
    "C;Y8;X13;K\"ground,air,ward\"\n"
    "C;Y9;X1;K\"AIpg\"\n"
    "C;Y9;X2;K\"Aprg\"\n"
    "C;Y9;X3;K\"1\"\n"
    "C;Y9;X4;K\"5\"\n"
    "C;Y9;X6;K\"150\"\n"
    "C;Y9;X7;K\"3\"\n"
    "C;Y9;X8;K\"1\"\n"
    "C;Y9;X10;K\"15\"\n"
    "C;Y9;X11;K\"5\"\n"
    "C;Y9;X12;K\"Bprg\"\n"
    "C;Y9;X13;K\"ground,air,ward\"\n"
    "C;Y10;X1;K\"AIob\"\n"
    "C;Y10;X2;K\"AIob\"\n"
    "C;Y10;X3;K\"1\"\n"
    "C;Y10;X4;K\"6\"\n"
    "C;Y10;X10;K\"3\"\n"
    "C;Y10;X11;K\"3\"\n"
    "C;Y10;X12;K\"Bfro\"\n"
    "E\n";
static char const nested_legacy_slk[] =
    "ID;PWXL;N;EBB;Y10;X16\n"
    "C;Y1;X1;K\"alias\"\n"
    "C;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"Data11\"\n"
    "C;Y1;X5;K\"Data12\"\n"
    "C;Y1;X6;K\"Data13\"\n"
    "C;Y1;X7;K\"Data14\"\n"
    "C;Y1;X8;K\"Data15\"\n"
    "C;Y1;X9;K\"UnitID1\"\n"
    "C;Y1;X10;K\"Dur1\"\n"
    "C;Y1;X11;K\"HeroDur1\"\n"
    "C;Y1;X12;K\"BuffID1\"\n"
    "C;Y1;X13;K\"targs\"\n"
    "C;Y1;X14;K\"Data22\"\n"
    "C;Y1;X15;K\"UnitID2\"\n"
    "C;Y1;X16;K\"targs2\"\n"
    "C;Y2;X1;K\"AInv\"\n"
    "C;Y2;X2;K\"AInv\"\n"
    "C;Y2;X3;K\"1\"\n"
    "C;Y2;X4;K\"6\"\n"
    "C;Y2;X6;K\"1\"\n"
    "C;Y2;X7;K\"1\"\n"
    "C;Y2;X8;K\"1\"\n"
    "C;Y3;X1;K\"AIv0\"\n"
    "C;Y3;X2;K\"AInv\"\n"
    "C;Y3;X3;K\"1\"\n"
    "C;Y3;X4;K\"6\"\n"
    "C;Y3;X6;K\"0\"\n"
    "C;Y3;X7;K\"1\"\n"
    "C;Y3;X8;K\"1\"\n"
    "C;Y4;X1;K\"AIsb\"\n"
    "C;Y4;X2;K\"AIsb\"\n"
    "C;Y4;X3;K\"1\"\n"
    "C;Y4;X4;K\"13\"\n"
    "C;Y4;X5;K\"100\"\n"
    "C;Y4;X6;K\"100\"\n"
    "C;Y4;X7;K\"100\"\n"
    "C;Y4;X8;K\"2\"\n"
    "C;Y4;X9;K\"AIos\"\n"
    "C;Y4;X13;K\"ground,air,ward\"\n"
    "C;Y5;X1;K\"A900\"\n"
    "C;Y5;X2;K\"AIsb\"\n"
    "C;Y5;X3;K\"2\"\n"
    "C;Y5;X4;K\"19\"\n"
    "C;Y5;X5;K\"%d\"\n"
    "C;Y5;X6;K\"%d\"\n"
    "C;Y5;X7;K\"%d\"\n"
    "C;Y5;X8;K\"2\"\n"
    "C;Y5;X9;K\"%s\"\n"
    "C;Y5;X13;K\"ground,air,ward\"\n"
    "C;Y5;X14;K\"100\"\n"
    "C;Y5;X15;K\"A901\"\n"
    "C;Y5;X16;K\"ground,air,ward\"\n"
    "C;Y6;X1;K\"AIos\"\n"
    "C;Y6;X2;K\"Aslo\"\n"
    "C;Y6;X3;K\"1\"\n"
    "C;Y6;X4;K\".4\"\n"
    "C;Y6;X5;K\".3\"\n"
    "C;Y6;X10;K\"7\"\n"
    "C;Y6;X11;K\"3\"\n"
    "C;Y6;X13;K\"ground,air,enemy\"\n"
    "C;Y7;X1;K\"A901\"\n"
    "C;Y7;X2;K\"Aslo\"\n"
    "C;Y7;X3;K\"1\"\n"
    "C;Y7;X4;K\".67\"\n"
    "C;Y7;X5;K\".43\"\n"
    "C;Y7;X10;K\"13\"\n"
    "C;Y7;X11;K\"6\"\n"
    "C;Y7;X13;K\"%s\"\n"
    "C;Y8;X1;K\"AIll\"\n"
    "C;Y8;X2;K\"AIsb\"\n"
    "C;Y8;X3;K\"1\"\n"
    "C;Y8;X4;K\"5\"\n"
    "C;Y8;X5;K\"%d\"\n"
    "C;Y8;X6;K\"100\"\n"
    "C;Y8;X7;K\"100\"\n"
    "C;Y8;X8;K\"2\"\n"
    "C;Y8;X9;K\"AIpg\"\n"
    "C;Y8;X13;K\"ground,air,ward\"\n"
    "C;Y9;X1;K\"AIpg\"\n"
    "C;Y9;X2;K\"Aprg\"\n"
    "C;Y9;X3;K\"1\"\n"
    "C;Y9;X4;K\"5\"\n"
    "C;Y9;X6;K\"150\"\n"
    "C;Y9;X7;K\"3\"\n"
    "C;Y9;X8;K\"1\"\n"
    "C;Y9;X10;K\"15\"\n"
    "C;Y9;X11;K\"5\"\n"
    "C;Y9;X13;K\"ground,air,ward\"\n"
    "C;Y10;X1;K\"AIob\"\n"
    "C;Y10;X2;K\"AIob\"\n"
    "C;Y10;X3;K\"1\"\n"
    "C;Y10;X4;K\"6\"\n"
    "C;Y10;X10;K\"3\"\n"
    "C;Y10;X11;K\"3\"\n"
    "E\n";
static float re_mask_life_at_death;

static void re_mask_die(edict_t *victim, edict_t *attacker) {
    re_mask_life_at_death = attacker->health.value;
    unit_die(victim, attacker);
}

static void re_init(retailEffectFixture_t *f, char const *ability_slk, char const *item_slk) {
    memset(f, 0, sizeof(*f));
    reset_entities(); setup_test_world(); level.time = 1000;
    f->ability_rows = parse_slk_string(ability_slk);
    f->item_rows = parse_slk_string(item_slk);
    T_NOT_NULL(f->ability_rows); T_NOT_NULL(f->item_rows);
    f->old_abilities = G_SetSLKRows("AbilityData", f->ability_rows);
    f->old_items = G_SetSLKRows("ItemData", f->item_rows);
    f->hero_abilities = (UnitAbilities_t){ .abilList = "AInv" };
    ((mapInfo_t *)level.mapinfo)->fileFormat = 24;
    f->hero = alloc_test_unit(FS_SLKKey("Hpal"), 0, 0);
    f->enemy = alloc_test_unit(FS_SLKKey("hfoo"), 64, 0);
    f->hero->data.UnitAbilities = &f->hero_abilities;
    f->hero->s.player = 0;
    f->enemy->s.player = 1;
    f->hero->svflags |= SVF_MONSTER;
    f->enemy->svflags |= SVF_MONSTER;
    /* Allocator-only test units do not go through SP_SpawnUnit, which stamps
     * their generation time. Status source validation needs that identity. */
    f->hero->spawn_time = f->enemy->spawn_time = level.time;
    f->hero->unit_state->attack1.type = ATK_NORMAL;
    f->hero->unit_state->attack1.targetsAllowed = TARG_GROUND;
    f->hero->targtype = f->enemy->targtype = TARG_GROUND;
    f->hero->health.value = f->hero->health.max_value = 500;
    f->enemy->health.value = f->enemy->health.max_value = 500;
    f->hero->mana.value = f->hero->mana.max_value = 500;
    f->enemy->mana.value = f->enemy->mana.max_value = 500;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
}
/* Drive the production per-entity frame scheduler so status owners, rather
 * than a direct update helper, process ticks and expiry. */
static void re_run_frame_at(edict_t *unit, uint32_t time) {
    level.time = time;
    G_RunEntity(unit);
}
static edict_t *re_equip(retailEffectFixture_t *f, cstring_t item_id, uint32_t slot) {
    edict_t *item = alloc_test_unit(FS_SLKKey(item_id), 16 + slot, 0);
    T_ASSERT(G_InventoryCapacity(f->hero) >= MAX_INVENTORY);
    T_ASSERT(G_InventoryCanUseItems(f->hero));
    item->targtype = TARG_ITEM;
    G_AddItem(item);
    G_GetItem(item)->in_world = true;
    G_GetItem(item)->inventory_slot = -1;
    T_ASSERT(G_AddItemToSlot(f->hero, item, slot));
    T_ASSERT(G_InventoryItem(f->hero, slot) == item);
    return item;
}
static void re_done(retailEffectFixture_t *f) {
    G_SetSLKRows("ItemData", f->old_items);
    G_SetSLKRows("AbilityData", f->old_abilities);
    free_slk_rows(f->item_rows);
    free_slk_rows(f->ability_rows);
}

/* Current 3.0.1 data uses a five-second delay. irc3's original common.j
 * declaration explicitly defines -1 as the current mana, not full mana. */
static void re_ankh_death(ankhTest_t test) {
    char abilities[1900];
    int const delay = test.delay, life = test.life, mana = test.mana;
    ItemData_t item_data = { .abilList = test.mode == ANKH_ALIAS ? "A900" : "AIrc", .perishable = true };
    static UnitProfile_t const producer_data = { .trains = "hpea" };
    char const *items = "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"perishable\"\n"
        "C;Y2;X1;K\"ankh\"\nC;Y2;X2;K\"AIrc\"\nC;Y2;X3;K\"true\"\nE\n";
    retailEffectFixture_t f;
    edict_t *item, *second = NULL, *producer, *follower, *resolved = NULL;
    uint32_t number, death_time;
    uint32_t deaths = 0, uses = 0;
    UnitBalance_t balance;

    snprintf(abilities, sizeof(abilities), "ID;PWXL;N;EBB;Y5;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
        "C;Y1;X7;K\"DataD1\"\nC;Y1;X8;K\"DataE1\"\nC;Y1;X9;K\"Cool1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"6\"\nC;Y2;X6;K\"%d\"\nC;Y2;X7;K\"1\"\nC;Y2;X8;K\"1\"\n"
        "C;Y3;X1;K\"AIrc\"\nC;Y3;X2;K\"AIrc\"\nC;Y3;X3;K\"1\"\n"
        "C;Y3;X4;K\"%d\"\nC;Y3;X5;K\"%d\"\nC;Y3;X6;K\"%d\"\n"
        "C;Y4;X1;K\"A900\"\nC;Y4;X2;K\"AIrc\"\nC;Y4;X3;K\"1\"\n"
        "C;Y4;X4;K\"%d\"\nC;Y4;X5;K\"%d\"\nC;Y4;X6;K\"%d\"\n"
        "C;Y5;X1;K\"AOre\"\nC;Y5;X2;K\"AOre\"\nC;Y5;X3;K\"1\"\n"
        "C;Y5;X4;K\"%d\"\nC;Y5;X9;K\"240\"\nE\n", test.mode != ANKH_CARRY_ONLY, delay, life, mana, delay, life, mana, delay);
    re_init(&f, abilities, items);
    T_ASSERT(G_UnitIsHero(f.hero));
    f.hero->health.value = f.hero->health.max_value = 800;
    f.hero->mana.value = 123;
    f.hero->stand = unit_stand; f.hero->die = unit_die;
    balance = *f.hero->data.UnitBalance; balance.foodUsed = 5;
    f.hero->data.UnitBalance = &balance;
    G_ActivateUnitFood(f.hero);
    producer = alloc_test_unit(FS_SLKKey("hbar"), 128, 0);
    producer->data.UnitProfile = &producer_data;
    producer->s.player = 0;
    T_ASSERT(G_SetRallyEntity(producer, f.hero));
    follower = alloc_test_unit(FS_SLKKey("hfoo"), 96, 0);
    follower->svflags |= SVF_MONSTER; follower->s.player = 0;
    number = f.hero->s.number;
    item = alloc_test_unit(FS_SLKKey("ankh"), 16, 0);
    item->targtype = TARG_ITEM; item->data.ItemData = &item_data;
    G_AddItem(item); G_GetItem(item)->charges = 1;
    G_GetItem(item)->in_world = true; G_GetItem(item)->inventory_slot = -1;
    T_ASSERT(G_AddItemToSlot(f.hero, item, 0));
    T_EQ(G_InventoryItem(f.hero, 0), item);
    if (test.mode == ANKH_TWO_ITEMS) {
        second = re_equip(&f, "ankh", 1);
        second->data.ItemData = &item_data;
        G_GetItem(second)->charges = 1;
    }
    if (test.mode == ANKH_ABILITY_FIRST) f.hero_abilities.abilList = "AInv,AOre";

    death_time = level.time;
    T_Damage(f.hero, f.enemy, 2000);
    T_ASSERT(M_IsDead(f.hero));
    for (uint32_t i = level.events.read; i < level.events.write; i++) {
        gameEvent_t const *event = level.events.queue + i % MAX_EVENT_QUEUE;
        if (event->edict != f.hero) continue;
        if (event->type == EVENT_UNIT_DEATH || event->type == EVENT_PLAYER_UNIT_DEATH) deaths++;
        if (event->type == EVENT_PLAYER_UNIT_USE_ITEM && event->source == item) uses++;
    }
    if (test.mode == ANKH_CARRY_ONLY) {
        T_EQ(deaths, 2); T_EQ(uses, 0);
        T_NULL(G_GetReincarnation(f.hero));
        T_EQ(G_GetFood(f.hero)->used, 0);
        level.time += delay * 1000 + 1; G_RunEvents(); G_RunEntities();
        T_ASSERT(M_IsDead(f.hero));
        re_done(&f);
        return;
    }
    T_EQ(deaths, 0); T_EQ(uses, test.mode == ANKH_ABILITY_FIRST ? 0 : 1);
    T_NOT_NULL(G_GetReincarnation(f.hero));
    T_EQ(G_GetFood(f.hero)->used, 5);
    T_EQ(G_ResolveRallyTarget(producer, NULL, &resolved), RALLY_TARGET_ENTITY);
    T_ASSERT(resolved == f.hero);
    T_ASSERT(G_ApplyRallyOrder(producer, follower));
    T_ASSERT(follower->orders->follow_target == f.hero);
    if (test.mode == ANKH_REMOVE) {
        edict_t *thinker = G_GetReincarnation(f.hero)->thinker;
        uint32_t index = thinker->s.number;
        G_FreeEdict(f.hero);
        T_ASSERT(!g_edicts[index].inuse);
        level.time += delay * 1000 + 1; G_RunEvents(); G_RunEntities();
        T_ASSERT(!f.hero->inuse);
        re_done(&f);
        return;
    }
    if (test.mode == ANKH_EXTERNAL_REVIVE) {
        T_ASSERT(G_ReviveHero(f.hero, 400, 400));
        T_NULL(G_GetReincarnation(f.hero));
        T_Damage(f.hero, f.enemy, 2000);
        level.time += delay * 1000 + 1; G_RunEvents(); G_RunEntities();
        T_ASSERT(M_IsDead(f.hero));
        T_FEQ(f.hero->s.origin2.x, 400, .001f);
        re_done(&f);
        return;
    }
    if (test.mode == ANKH_SAVE) {
        cstring_t path = Test_TempPath("ankh-pending.bin");
        edict_t *thinker = G_GetReincarnation(f.hero)->thinker;
        ((mapInfo_t *)level.mapinfo)->fileFormat = 0;
        level.time += 2000;
        G_GetReincarnation(f.hero)->thinker = (edict_t *)((uint8_t *)g_edicts + 1);
        T_ASSERT(!WriteGame(path));
        G_GetReincarnation(f.hero)->thinker = thinker;
        T_ASSERT(WriteGame(path));
        G_GetReincarnation(f.hero)->mana = 0;
        T_ASSERT(ReadGame(path));
        f.hero = g_edicts + number;
        /* Synthetic table views rebind through the fixture archive on load. */
        f.hero->data.UnitBalance = &balance;
        T_NOT_NULL(G_GetReincarnation(f.hero));
        if (G_GetReincarnation(f.hero)) {
            T_FEQ(G_GetReincarnation(f.hero)->mana, mana < 0 ? 123 : mana, .001f);
            T_EQ(G_GetReincarnation(f.hero)->thinker->think, reincarnation_think);
        }
        remove(path);
    }
    level.time = death_time + delay * 1000 - 1;
    G_RunEvents(); G_RunEntities();
    T_ASSERT(M_IsDead(f.hero));
    level.time++;
    G_RunEvents(); G_RunEntities();
    T_ASSERT(!M_IsDead(f.hero));
    T_EQ(f.hero->s.number, number);
    T_FEQ(f.hero->health.value, test.mode == ANKH_ABILITY_FIRST ? 800 : life, .001f);
    T_FEQ(f.hero->mana.value, test.mode == ANKH_ABILITY_FIRST ? 500 : (mana < 0 ? 123 : mana), .001f);
    if (test.mode == ANKH_ABILITY_FIRST) T_EQ(G_InventoryItem(f.hero, 0), item);
    else T_NULL(G_InventoryItem(f.hero, 0));
    T_ASSERT(!(f.hero->svflags & SVF_DEADMONSTER));
    T_ASSERT(!(f.hero->s.flags & EF_NOT_SELECTABLE));
    T_ASSERT(!G_UnitHasFlag(f.hero, UNIT_FLAG_ANIMATION_OVERRIDE));
    T_EQ(G_GetFood(f.hero)->used, 5);
    T_NULL(G_GetReincarnation(f.hero));

    /* One perishable item provides one return, not unlimited immortality. */
    T_Damage(f.hero, f.enemy, 2000);
    level.time += delay * 1000 + 1;
    G_RunEvents(); G_RunEntities();
    if (test.mode == ANKH_TWO_ITEMS || test.mode == ANKH_ABILITY_FIRST) {
        T_ASSERT(!M_IsDead(f.hero));
        T_FEQ(f.hero->health.value, life, .001f);
        T_NULL(G_InventoryItem(f.hero, test.mode == ANKH_TWO_ITEMS ? 1 : 0));
        T_Damage(f.hero, f.enemy, 2000);
        level.time += delay * 1000 + 1; G_RunEvents(); G_RunEntities();
    }
    T_ASSERT(M_IsDead(f.hero));
    re_done(&f);
}

TEST(retail_item_effect, ankh_revives_the_same_hero_with_authored_life_and_current_mana) { re_ankh_death((ankhTest_t){5, 500, -1}); }
TEST(retail_item_effect, ankh_uses_nonstock_delay_life_and_mana) { re_ankh_death((ankhTest_t){2, 137, 19}); }
TEST(retail_item_effect, ankh_pending_return_survives_real_save_load) { re_ankh_death((ankhTest_t){5, 500, -1, ANKH_SAVE}); }
TEST(retail_item_effect, ankh_unit_removal_cancels_its_pending_thinker) { re_ankh_death((ankhTest_t){5, 500, -1, ANKH_REMOVE}); }
TEST(retail_item_effect, ankh_external_revival_cannot_revive_a_later_death) { re_ankh_death((ankhTest_t){5, 500, -1, ANKH_EXTERNAL_REVIVE}); }
TEST(retail_item_effect, ankh_two_items_provide_two_distinct_returns) { re_ankh_death((ankhTest_t){5, 500, -1, ANKH_TWO_ITEMS}); }
TEST(retail_item_effect, ankh_custom_ability_alias_uses_authored_data) { re_ankh_death((ankhTest_t){2, 137, 19, ANKH_ALIAS}); }
TEST(retail_item_effect, ankh_follows_innate_reincarnation_and_its_cooldown) { re_ankh_death((ankhTest_t){5, 500, -1, ANKH_ABILITY_FIRST}); }
TEST(retail_item_effect, ankh_carry_only_inventory_cannot_use_the_item) { re_ankh_death((ankhTest_t){5, 500, -1, ANKH_CARRY_ONLY}); }

/* AIva's original item row and Blizzard's artifact guide define an attack
 * modifier, independently of the Dreadlord's melee-only Vampiric Aura. */
static void re_mask_attack(maskTest_t test) {
    char abilities[3400];
    stbIniCache_t old_misc = game.config.misc;
    bool const effect = test.mode == MASK_EFFECT || test.mode == MASK_EFFECT_REMOVE || test.mode == MASK_EFFECT_FULL;
    static UnitAbilities_t const immune_abilities = { .abilList = "Amim" };
    ItemData_t item_data = { .id = FS_SLKKey("modt"), .abilList = test.mode == MASK_ALIAS || effect ? "A900" : "AIva", .droppable = true };
    static ItemData_t const frost_data = { .id = MAKEFOURCC('o','f','r','o'), .abilList = "AIob", .droppable = true };
    static ItemData_t const fire_data = { .id = MAKEFOURCC('o','f','i','r'), .abilList = "AIfb", .droppable = true };
    static ItemData_t const poison_data = { .id = MAKEFOURCC('o','v','e','n'), .abilList = "AIpb,Apo2", .droppable = true };
    char const *items = "ID;PWXL;N;EBB;Y5;X2\nC;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"modt\"\nC;Y2;X2;K\"AIva\"\n"
        "C;Y3;X1;K\"ofro\"\nC;Y3;X2;K\"AIob\"\n"
        "C;Y4;X1;K\"ofir\"\nC;Y4;X2;K\"AIfb\"\n"
        "C;Y5;X1;K\"oven\"\nC;Y5;X2;K\"AIpb,Apo2\"\nE\n";
    retailEffectFixture_t f;
    edict_t *item, *orb = NULL, *nearby = NULL;
    bool const fire = test.mode == MASK_FIRE_FIRST || test.mode == MASK_FIRE_LOWER || test.mode == MASK_MANUAL_FIRE;
    bool const poison = test.mode == MASK_POISON_FIRST || test.mode == MASK_POISON_LOWER || test.mode == MASK_MANUAL_POISON;
    bool const manual = test.mode == MASK_MANUAL || test.mode == MASK_MANUAL_FIRE ||
        test.mode == MASK_MANUAL_NO_MANA || test.mode == MASK_MANUAL_POISON;
    uint32_t const mask_slot = test.mode == MASK_FROST_FIRST || test.mode == MASK_FIRE_FIRST ||
        test.mode == MASK_MANUAL_FIRE || test.mode == MASK_POISON_FIRST || test.mode == MASK_MANUAL_POISON ? 1 : 0;
    bool const steals = test.mode != MASK_CARRY_ONLY && test.mode != MASK_FROST_FIRST &&
        test.mode != MASK_MANUAL && test.mode != MASK_MANUAL_FIRE && test.mode != MASK_FIRE_FIRST &&
        test.mode != MASK_ILLUSION && test.mode != MASK_VICTIM_ILLUSION &&
        test.mode != MASK_INVULNERABLE && test.mode != MASK_SPELL && test.mode != MASK_POISON_FIRST &&
        test.mode != MASK_MANUAL_POISON && test.mode != MASK_IMMUNE_RESISTS;
    float health, expected, initial_life;
    snprintf(abilities, sizeof(abilities), "ID;PWXL;N;EBB;Y10;X13\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"%s\"\n"
        "C;Y1;X4;K\"%s\"\nC;Y1;X5;K\"%s\"\nC;Y1;X6;K\"DataE1\"\n"
        "C;Y1;X7;K\"levels\"\nC;Y1;X8;K\"%s\"\nC;Y1;X9;K\"Dur1\"\n"
        "C;Y1;X10;K\"HeroDur1\"\nC;Y1;X11;K\"BuffID1\"\nC;Y1;X12;K\"Cost1\"\nC;Y1;X13;K\"Area1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"6\"\n"
        "C;Y2;X4;K\"%d\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"1\"\nC;Y2;X7;K\"1\"\n"
        "C;Y3;X1;K\"AIva\"\nC;Y3;X2;K\"AIva\"\nC;Y3;X3;K\"%.6f\"\n"
        "C;Y3;X7;K\"1\"\nC;Y3;X8;K\"air,ground,enemy\"\n"
        "C;Y4;X1;K\"A900\"\nC;Y4;X2;K\"AIva\"\nC;Y4;X3;K\"%.6f\"\n"
        "C;Y4;X7;K\"1\"\nC;Y4;X8;K\"air,ground,enemy\"\n"
        "C;Y5;X1;K\"AIob\"\nC;Y5;X2;K\"AIob\"\nC;Y5;X3;K\"6\"\n"
        "C;Y5;X7;K\"1\"\nC;Y5;X9;K\"3\"\nC;Y5;X10;K\"1\"\nC;Y5;X11;K\"Bfro\"\n"
        "C;Y6;X1;K\"AHfa\"\nC;Y6;X2;K\"AHfa\"\nC;Y6;X3;K\"17\"\n"
        "C;Y6;X7;K\"1\"\nC;Y6;X8;K\"air,ground,enemy\"\nC;Y6;X12;K\"7\"\n"
        "C;Y7;X1;K\"AIfb\"\nC;Y7;X2;K\"AIfb\"\nC;Y7;X3;K\"5\"\n"
        "C;Y7;X7;K\"1\"\nC;Y7;X8;K\"ground,air,ward\"\nC;Y7;X13;K\"140\"\n"
        "C;Y8;X1;K\"AIpb\"\nC;Y8;X2;K\"AIpb\"\nC;Y8;X3;K\"5\"\nC;Y8;X7;K\"1\"\n"
        "C;Y8;X8;K\"ground,air,organic\"\n"
        "C;Y9;X1;K\"Apo2\"\nC;Y9;X2;K\"Apo2\"\nC;Y9;X3;K\"8\"\nC;Y9;X5;K\"1\"\n"
        "C;Y9;X7;K\"1\"\nC;Y9;X8;K\"ground,air,organic\"\n"
        "C;Y9;X9;K\"6\"\nC;Y9;X10;K\"6\"\nC;Y9;X11;K\"BIpb,BIpd\"\n"
        "C;Y10;X1;K\"Amim\"\nC;Y10;X2;K\"Amim\"\nC;Y10;X7;K\"1\"\nE\n",
        test.mode == MASK_ROC ? "Data11" : "DataA1", test.mode == MASK_ROC ? "Data13" : "DataC1",
        test.mode == MASK_ROC ? "Data14" : "DataD1", test.mode == MASK_ROC ? "targs" : "targs1",
        test.mode != MASK_CARRY_ONLY, test.ratio, test.ratio);
    re_init(&f, abilities, items);
    if (effect) T_ASSERT(G_TestPushAbilityConfig("[A900]\nSpecialart=TestUI\\Models\\delayed_stand.mdx\n"));
    game.config.misc = (stbIniCache_t){0};
    if (test.mode == MASK_ILLUSION_ENABLED)
        T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.misc, "[Misc]\nIllusionsCanRestoreLife=1\n"));
    if (test.mode == MASK_IMMUNE_RESISTS)
        T_ASSERT(Stb_IniCacheLoadBuffer(&game.config.misc, "[Misc]\nMagicImmunesResistLeech=1\n"));
    /* A live spawned Hero starts at level one. Level zero would turn the
     * lethal hit's XP into unrelated stat/health initialization. */
    f.hero->unit_state->hero.level = 1;
    f.hero->health.value = initial_life = test.mode == MASK_EFFECT_FULL ? 500 : test.mode == MASK_CAP ? 490 : 100;
    f.enemy->unit_state->armor_value = 0;
    if (test.mode == MASK_RANGED || manual || test.mode == MASK_AUTOCAST)
        f.hero->unit_state->attack1.weapon = WPN_MISSILE;
    if (test.mode == MASK_OVERKILL || test.mode == MASK_DEATH_ORDER) {
        f.enemy->health.value = 37;
        f.enemy->stand = unit_stand; f.enemy->die = unit_die;
        if (test.mode == MASK_DEATH_ORDER) {
            re_mask_life_at_death = -1;
            f.enemy->die = re_mask_die;
        }
    }
    if (test.mode == MASK_ILLUSION || test.mode == MASK_ILLUSION_ENABLED) f.hero->aiflags |= AI_ILLUSION;
    if (test.mode == MASK_VICTIM_ILLUSION) f.enemy->aiflags |= AI_ILLUSION;
    if (test.mode == MASK_IMMUNE || test.mode == MASK_IMMUNE_RESISTS) {
        f.enemy->data.UnitAbilities = &immune_abilities;
        T_ASSERT(S_UnitSpellImmune(f.enemy));
    }
    if (test.mode == MASK_INVULNERABLE) G_SetUnitFlag(f.enemy, UNIT_FLAG_INVULNERABLE, true);
    item = alloc_test_unit(FS_SLKKey("modt"), 16, 0);
    item->targtype = TARG_ITEM; item->data.ItemData = &item_data;
    G_AddItem(item); G_GetItem(item)->in_world = true; G_GetItem(item)->inventory_slot = -1;
    T_ASSERT(G_AddItemToSlot(f.hero, item, mask_slot));
    if (test.mode == MASK_FIRST || test.mode == MASK_FROST_FIRST || fire || poison) {
        orb = alloc_test_unit(FS_SLKKey(poison ? "oven" : fire ? "ofir" : "ofro"), 20, 0);
        orb->targtype = TARG_ITEM; orb->data.ItemData = poison ? &poison_data : fire ? &fire_data : &frost_data;
        G_AddItem(orb); G_GetItem(orb)->in_world = true; G_GetItem(orb)->inventory_slot = -1;
        T_ASSERT(G_AddItemToSlot(f.hero, orb, 1 - mask_slot));
        T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, fire || poison ? 5 : 6, .001f);
        if (fire) {
            nearby = alloc_test_unit(FS_SLKKey("hfoo"), 110, 0);
            nearby->svflags |= SVF_MONSTER; nearby->s.player = 1; nearby->targtype = TARG_GROUND;
            nearby->health.value = nearby->health.max_value = 500;
        }
    }
    if (manual || test.mode == MASK_AUTOCAST) {
        f.hero_abilities.abilList = "AInv,AHfa";
        if (test.mode == MASK_MANUAL_NO_MANA) f.hero->mana.value = 0;
        if (test.mode == MASK_AUTOCAST) T_ASSERT(G_SetUnitAutocast(f.hero, FS_SLKKey("AHfa"), true));
        else T_NOT_NULL(unit_applystatus(f.hero, &(status_application_t){ .buff = "AHfa", .level = 1,
            .source_ability = FS_SLKKey("AHfa"), .rank = 1, .buff_flags = WC3_STATUS_BUFF_ONE_SHOT_ATTACK }));
    }
    health = f.enemy->health.value;
    if (test.mode == MASK_SPELL) T_Damage(f.enemy, f.hero, 100);
    else S_ResolveAttackHit(f.hero, f.enemy, 100);
    expected = MIN(f.hero->health.max_value, initial_life + (steals ? (health - f.enemy->health.value) * test.ratio : 0));
    T_FEQ(f.hero->health.value, expected, .001f);
    if (test.mode == MASK_DEATH_ORDER) T_FEQ(re_mask_life_at_death, expected, .001f);
    T_EQ(G_InventoryItem(f.hero, mask_slot), item);
    if (test.mode != MASK_INVULNERABLE) T_ASSERT(f.enemy->health.value < health);
    else T_FEQ(f.enemy->health.value, health, .001f);
    if (orb && !fire && !poison) T_EQ(S_UnitHasStatus(f.enemy, FS_SLKKey("Bfro")), test.mode == MASK_FROST_FIRST);
    if (poison) T_EQ(S_UnitHasStatus(f.enemy, FS_SLKKey("BIpb")), test.mode == MASK_POISON_FIRST);
    if (nearby) T_FEQ(nearby->health.value, test.mode == MASK_FIRE_FIRST ? 400 : 500, .001f);
    if (manual || test.mode == MASK_AUTOCAST) {
        bool const paid = manual && test.mode != MASK_MANUAL_NO_MANA;
        T_FEQ(f.hero->mana.value, paid ? 493 : test.mode == MASK_MANUAL_NO_MANA ? 0 : 500, .001f);
        T_FEQ(health - f.enemy->health.value, paid ? 117 : 100, .001f);
    }
    if (test.mode == MASK_DROP || test.mode == MASK_FROST_FIRST) {
        float const before = f.hero->health.value;
        T_ASSERT(G_DropItem(f.hero, test.mode == MASK_DROP ? mask_slot : 0));
        health = f.enemy->health.value;
        S_ResolveAttackHit(f.hero, f.enemy, 100);
        T_FEQ(f.hero->health.value, before + (test.mode == MASK_DROP ? 0 : (health - f.enemy->health.value) * test.ratio), .001f);
    }
    if (effect) {
        edict_t *art = NULL;
        uint32_t count = 0;
        uint32_t const model = G_RegisterModel("TestUI\\Models\\delayed_stand.mdx");
        T_ASSERT(model);
        FILTER_EDICTS(candidate, candidate->goalentity == f.hero && candidate->s.model == model) {
            art = candidate; count++;
        }
        T_EQ(count, 1u); T_NOT_NULL(art);
        if (art) {
            T_EQ(art->movetype, MOVETYPE_LINK);
            T_EQ(art->damage, f.hero->spawn_time);
            if (test.mode == MASK_EFFECT_REMOVE) {
                G_FreeEdict(f.hero);
                re_run_frame_at(art, level.time + 16);
                T_ASSERT(!art->inuse);
            }
        }
        G_TestPopAbilityConfig();
    }
    Stb_IniCacheFree(&game.config.misc); game.config.misc = old_misc;
    re_done(&f);
}

TEST(retail_item_effect, mask_of_death_steals_authored_half_of_attack_damage) { re_mask_attack((maskTest_t){.5f, MASK_NORMAL}); }
TEST(retail_item_effect, mask_of_death_uses_nonstock_lifesteal_ratio) { re_mask_attack((maskTest_t){.37f, MASK_NORMAL}); }
TEST(retail_item_effect, mask_of_death_reads_roc_data_columns) { re_mask_attack((maskTest_t){.5f, MASK_ROC}); }
TEST(retail_item_effect, mask_of_death_works_on_ranged_attacks) { re_mask_attack((maskTest_t){.5f, MASK_RANGED}); }
TEST(retail_item_effect, mask_of_death_custom_alias_uses_its_data) { re_mask_attack((maskTest_t){.37f, MASK_ALIAS}); }
TEST(retail_item_effect, mask_of_death_overkill_uses_remaining_victim_life) { re_mask_attack((maskTest_t){.5f, MASK_OVERKILL}); }
TEST(retail_item_effect, mask_of_death_drop_removes_lifesteal) { re_mask_attack((maskTest_t){.5f, MASK_DROP}); }
TEST(retail_item_effect, mask_of_death_carry_only_inventory_cannot_steal) { re_mask_attack((maskTest_t){.5f, MASK_CARRY_ONLY}); }
TEST(retail_item_effect, mask_of_death_highest_item_suppresses_frost) { re_mask_attack((maskTest_t){.5f, MASK_FIRST}); }
TEST(retail_item_effect, mask_of_death_follows_higher_orb_then_its_drop) { re_mask_attack((maskTest_t){.5f, MASK_FROST_FIRST}); }
TEST(retail_item_effect, mask_of_death_manual_searing_arrow_overrides_orb) { re_mask_attack((maskTest_t){.5f, MASK_MANUAL}); }
TEST(retail_item_effect, mask_of_death_overrides_autocast_without_spending_mana) { re_mask_attack((maskTest_t){.5f, MASK_AUTOCAST}); }
TEST(retail_item_effect, mask_of_death_does_not_heal_illusion_attacker) { re_mask_attack((maskTest_t){.5f, MASK_ILLUSION}); }
TEST(retail_item_effect, mask_of_death_does_not_leech_illusion_victim) { re_mask_attack((maskTest_t){.5f, MASK_VICTIM_ILLUSION}); }
TEST(retail_item_effect, mask_of_death_invulnerable_hit_cannot_steal) { re_mask_attack((maskTest_t){.5f, MASK_INVULNERABLE}); }
TEST(retail_item_effect, mask_of_death_spell_damage_cannot_steal) { re_mask_attack((maskTest_t){.5f, MASK_SPELL}); }
TEST(retail_item_effect, mask_of_death_yields_to_higher_fire_orb) { re_mask_attack((maskTest_t){.5f, MASK_FIRE_FIRST}); }
TEST(retail_item_effect, mask_of_death_suppresses_lower_fire_splash) { re_mask_attack((maskTest_t){.5f, MASK_FIRE_LOWER}); }
TEST(retail_item_effect, mask_of_death_manual_arrow_also_suppresses_fire_splash) { re_mask_attack((maskTest_t){.5f, MASK_MANUAL_FIRE}); }
TEST(retail_item_effect, mask_of_death_failed_manual_arrow_falls_back_without_mana) { re_mask_attack((maskTest_t){.5f, MASK_MANUAL_NO_MANA}); }
TEST(retail_item_effect, mask_of_death_healing_stops_at_maximum_life) { re_mask_attack((maskTest_t){.5f, MASK_CAP}); }
TEST(retail_item_effect, mask_of_death_yields_to_higher_poison_orb) { re_mask_attack((maskTest_t){.5f, MASK_POISON_FIRST}); }
TEST(retail_item_effect, mask_of_death_suppresses_lower_poison_orb) { re_mask_attack((maskTest_t){.5f, MASK_POISON_LOWER}); }
TEST(retail_item_effect, mask_of_death_manual_arrow_suppresses_poison_orb) { re_mask_attack((maskTest_t){.5f, MASK_MANUAL_POISON}); }
TEST(retail_item_effect, mask_of_death_illusion_healing_obeys_gameplay_constant) { re_mask_attack((maskTest_t){.5f, MASK_ILLUSION_ENABLED}); }
TEST(retail_item_effect, mask_of_death_can_leech_spell_immune_target_by_default) { re_mask_attack((maskTest_t){.5f, MASK_IMMUNE}); }
TEST(retail_item_effect, mask_of_death_magic_immune_resistance_obeys_gameplay_constant) { re_mask_attack((maskTest_t){.5f, MASK_IMMUNE_RESISTS}); }
TEST(retail_item_effect, mask_of_death_plays_authored_special_art_on_carrier) { re_mask_attack((maskTest_t){.37f, MASK_EFFECT}); }
TEST(retail_item_effect, mask_of_death_feedback_removal_rejects_dead_attachment_owner) { re_mask_attack((maskTest_t){.37f, MASK_EFFECT_REMOVE}); }
TEST(retail_item_effect, mask_of_death_full_health_still_plays_attack_feedback) { re_mask_attack((maskTest_t){.37f, MASK_EFFECT_FULL}); }
TEST(retail_item_effect, mask_of_death_death_callback_observes_healed_carrier) { re_mask_attack((maskTest_t){.5f, MASK_DEATH_ORDER}); }


TEST(retail_item_effect, pres_restores_authored_health_and_mana) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\nC;Y1;X7;K\"DataE1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\n"
        "C;Y2;X3;K\"6\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"1\"\nC;Y2;X7;K\"1\"\n"
        "C;Y3;X1;K\"AIre\"\nC;Y3;X2;K\"AIre\"\n"
        "C;Y3;X3;K\"500\"\nC;Y3;X4;K\"200\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"perishable\"\n"
        "C;Y2;X1;K\"pres\"\nC;Y2;X2;K\"AIre\"\nC;Y2;X3;K\"true\"\nE\n";
    retailEffectFixture_t f;
    static ItemData_t item_data = { .abilList = "AIre", .perishable = true };
    edict_t *item, *player;
    re_init(&f, ability_slk, items);
    /* This contract is independent of the inventory permission schema. Use
     * the classic Hero default slots so the test isolates AIre's use effect. */
    ((mapInfo_t *)level.mapinfo)->fileFormat = 0;
    player = &g_edicts[0];
    player->client->ps.number = 0;
    f.hero->s.player = 0;
    G_SelectEntity(player->client, f.hero);
    T_EQ(G_GetMainSelectedUnit(player->client), f.hero);
    f.hero->health.value = 100;
    f.hero->mana.value = 350;
    item = re_equip(&f, "pres", 0);
    item->data.ItemData = &item_data;
    G_GetItem(item)->charges = 1;
    G_UseItem(f.hero, 0);
    T_FEQ(f.hero->health.value, 500, 0.001f);
    T_FEQ(f.hero->mana.value, 500, 0.001f);
    T_NULL(G_InventoryItem(f.hero, 0));
    re_done(&f);
}

TEST(retail_item_effect, healing_salve_targets_friendly_unit_and_interrupts_on_damage) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\n"
        "C;Y1;X7;K\"Range1\"\nC;Y1;X8;K\"BuffID1\"\nC;Y1;X9;K\"targs1\"\nC;Y1;X10;K\"DataC1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"6\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"0\"\nC;Y2;X10;K\"1\"\n"
        "C;Y3;X1;K\"AIrl\"\nC;Y3;X2;K\"AIrl\"\nC;Y3;X3;K\"1\"\n"
        "C;Y3;X4;K\"400\"\nC;Y3;X5;K\"45\"\nC;Y3;X6;K\"45\"\n"
        "C;Y3;X7;K\"50\"\nC;Y3;X8;K\"BIrg\"\nC;Y3;X9;K\"air,ground,friend,organic\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y1;X3;K\"uses\"\nC;Y1;X4;K\"perishable\"\n"
        "C;Y2;X1;K\"hslv\"\nC;Y2;X2;K\"AIrl\"\nC;Y2;X3;K\"3\"\nC;Y2;X4;K\"true\"\nE\n";
    retailEffectFixture_t f;
    edict_t *ally, *attacker, *player, *salve;
    gameClient_t *client;
    re_init(&f, ability_slk, items);
    f.hero->s.player = 0;
    f.enemy->s.player = 0;
    f.enemy->health.value = 100;
    ally = f.enemy;
    attacker = alloc_test_unit(FS_SLKKey("hfoo"), 128, 0);
    attacker->s.player = 1;
    attacker->svflags |= SVF_MONSTER;
    attacker->spawn_time = level.time;
    player = &g_edicts[0];
    static UnitBalance_t no_regen_balance;
    memset(&no_regen_balance, 0, sizeof(no_regen_balance));
    no_regen_balance.healthRegenType = "none";
    ally->data.UnitBalance = &no_regen_balance;
    player->client->ps.number = 0;
    client = player->client;
    G_SelectEntity(client, f.hero);
    salve = re_equip(&f, "hslv", 0);
    G_GetItem(salve)->charges = 3;
    G_UseItem(f.hero, 0);
    T_NOT_NULL(client->menu.on_entity_selected);
    if (client->menu.on_entity_selected) client->menu.on_entity_selected(player, ally);
    T_NOT_NULL(unit_findstatus(ally, FS_SLKKey("BIrg")));
    T_EQ(G_GetItem(salve)->charges, 2);
    float const after_cast = ally->health.value;
    re_run_frame_at(ally, level.time + 1000);
    T_FEQ(ally->health.value, after_cast + 400.0f / 45.0f, 0.01f);
    S_ResolveAttackHit(attacker, ally, 10);
    T_NULL(unit_findstatus(ally, FS_SLKKey("BIrg")));
    float const after_interrupt = ally->health.value;
    re_run_frame_at(ally, level.time + 1000);
    T_FEQ(ally->health.value, after_interrupt, 0.001f);
    re_done(&f);
}

/* The supplied 3.0.1 AIcb reduces armor by four, independently of its five
 * attack damage. Expiry must restore the original defense through the scheduler. */
TEST(retail_item_effect, ocor_applies_timed_corruption_status) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X11\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"levels\"\nC;Y1;X4;K\"DataA1\"\n"
        "C;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataE1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"BuffID1\"\n"
        "C;Y1;X10;K\"DataC1\"\nC;Y1;X11;K\"DataD1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\n"
        "C;Y2;X3;K\"1\"\nC;Y2;X4;K\"6\"\n"
        "C;Y2;X6;K\"1\"\nC;Y2;X10;K\"1\"\nC;Y2;X11;K\"1\"\n"
        "C;Y3;X1;K\"AIcb\"\nC;Y3;X2;K\"AIcb\"\n"
        "C;Y3;X3;K\"1\"\nC;Y3;X4;K\"5\"\n"
        "C;Y3;X5;K\"4\"\nC;Y3;X6;K\"2\"\n"
        "C;Y3;X7;K\"5\"\nC;Y3;X8;K\"5\"\n"
        "C;Y3;X9;K\"BIcb\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\n"
        "C;Y2;X1;K\"ocor\"\nC;Y2;X2;K\"AIcb\"\nC;Y2;X3;K\"true\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    float const before = G_UnitArmorValue(f.enemy);
    T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, 0, 0.001f);
    re_equip(&f, "ocor", 0);
    T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, 5, 0.001f);
    S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("BIcb")));
    T_FEQ(G_UnitArmorValue(f.enemy), before - 4, 0.001f);
    re_run_frame_at(f.enemy, level.time + 4999);
    T_FEQ(G_UnitArmorValue(f.enemy), before - 4, 0.001f);
    re_run_frame_at(f.enemy, level.time + 1);
    T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey("BIcb")));
    T_FEQ(G_UnitArmorValue(f.enemy), before, 0.001f);
    T_ASSERT(G_DropItem(f.hero, 0));
    T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, 0, 0.001f);
    re_done(&f);
}

/* The published item carries TWO authored abilities; a status-only check
 * is inadequate. Verify native hit, one (not two) DPS pulse, 6s expiry and
 * the independent five-point passive attack bonus. */
TEST(retail_item_effect, oven_combines_attack_bonus_and_single_poison_pulse) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y4;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"levels\"\nC;Y1;X4;K\"DataA1\"\n"
        "C;Y1;X5;K\"DataD1\"\nC;Y1;X6;K\"Dur1\"\n"
        "C;Y1;X7;K\"HeroDur1\"\nC;Y1;X8;K\"BuffID1\"\n"
        "C;Y1;X9;K\"DataE1\"\nC;Y1;X10;K\"DataC1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\n"
        "C;Y2;X3;K\"1\"\nC;Y2;X4;K\"6\"\n"
        "C;Y2;X5;K\"1\"\nC;Y2;X9;K\"1\"\nC;Y2;X10;K\"1\"\n"
        "C;Y3;X1;K\"AIpb\"\nC;Y3;X2;K\"AIpb\"\n"
        "C;Y3;X3;K\"1\"\nC;Y3;X4;K\"5\"\n"
        "C;Y3;X9;K\"2\"\n"
        "C;Y4;X1;K\"Apo2\"\nC;Y4;X2;K\"Apo2\"\n"
        "C;Y4;X3;K\"1\"\nC;Y4;X4;K\"9\"\n"
        "C;Y4;X5;K\"1\"\nC;Y4;X6;K\"6\"\n"
        "C;Y4;X7;K\"6\"\nC;Y4;X8;K\"BIpb,BIpd\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\n"
        "C;Y2;X1;K\"oven\"\nC;Y2;X2;K\"AIpb,Apo2\"\nC;Y2;X3;K\"true\"\nE\n";
    static UnitBalance_t target_balance;
    static UnitWeapons_t ground_only_weapons = { .attacksEnabled = 1 };
    retailEffectFixture_t f;
    memset(&target_balance, 0, sizeof(target_balance));
    re_init(&f, ability_slk, items);
    f.enemy->data.UnitBalance = &target_balance;
    f.hero->data.UnitWeapons = &ground_only_weapons;
    f.hero->unit_state->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
    memset(&f.hero->unit_state->attack2, 0, sizeof(f.hero->unit_state->attack2));
    f.enemy->targtype = TARG_AIR;
    T_ASSERT(!S_AttackCanTarget(f.hero, f.enemy));
    re_equip(&f, "oven", 0);
    T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, 5, 0.001f);
    T_ASSERT(S_AttackCanTarget(f.hero, f.enemy));
    f.enemy->targtype = TARG_GROUND;
    S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("BIpb")));
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("BIpd")));
    uint32_t const first_poison_tick = unit_findstatus(f.enemy, FS_SLKKey("BIpb"))->next_tick;
    S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_EQ(unit_findstatus(f.enemy, FS_SLKKey("BIpb"))->next_tick, first_poison_tick);
    float const post_hit_hp = f.enemy->health.value;
    re_run_frame_at(f.enemy, level.time + 1000);
    T_FEQ(f.enemy->health.value, post_hit_hp - 9, 0.001f);
    re_run_frame_at(f.enemy, level.time + 1000);
    T_FEQ(f.enemy->health.value, post_hit_hp - 18, 0.001f);
    re_run_frame_at(f.enemy, level.time + 4000);
    T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey("BIpb")));
    float const after = f.enemy->health.value;
    re_run_frame_at(f.enemy, level.time + 1000);
    T_FEQ(f.enemy->health.value, after, 0.001f);
    T_ASSERT(G_DropItem(f.hero, 0));
    T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, 0, 0.001f);
    re_done(&f);
}

/* Stock 15s AIxs/Aami contract from both audited archives.  A separate
 * deliberately nonstock 17s check already exists in t_anti_magic_shell.c. */
TEST(retail_item_effect, pams_blocks_hostile_magic_until_15_seconds) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X13\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"levels\"\nC;Y1;X4;K\"targs\"\n"
        "C;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\n"
        "C;Y1;X7;K\"DataB1\"\nC;Y1;X8;K\"DataC1\"\n"
        "C;Y1;X9;K\"BuffID1\"\nC;Y1;X10;K\"Cool1\"\n"
        "C;Y1;X11;K\"DataD1\"\nC;Y1;X12;K\"DataE1\"\nC;Y1;X13;K\"DataA1\"\n"
        "C;Y2;X1;K\"AIxs\"\nC;Y2;X2;K\"Aami\"\n"
        "C;Y2;X3;K\"1\"\nC;Y2;X4;K\"air,ground,friend,self\"\n"
        "C;Y2;X5;K\"15\"\nC;Y2;X6;K\"15\"\n"
        "C;Y2;X7;K\"10\"\nC;Y2;X8;K\"0\"\n"
        "C;Y2;X9;K\"Bams,Bam2\"\nC;Y2;X10;K\"30\"\n"
        "C;Y3;X1;K\"AInv\"\nC;Y3;X2;K\"AInv\"\nC;Y3;X3;K\"1\"\n"
        "C;Y3;X8;K\"1\"\nC;Y3;X11;K\"1\"\nC;Y3;X12;K\"1\"\nC;Y3;X13;K\"6\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"pams\"\nC;Y2;X2;K\"AIxs\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    /* Direct cast helper requires the alias on the synthetic unit ability list;
     * G_UseItem's UI target-selection path is covered separately. */
    f.hero_abilities.abilList = "AInv,AIxs";
    re_equip(&f, "pams", 0);
    T_ASSERT(S_CastUnitTargetSpell(f.hero, FS_SLKKey("AIxs"), f.hero));
    T_ASSERT(S_UnitSpellImmune(f.hero));
    T_ASSERT(!S_SpellDamage(f.hero, f.enemy, 40));
    T_FEQ(f.hero->health.value, 500, 0.001f);
    re_run_frame_at(f.hero, level.time + 14999);
    T_ASSERT(S_UnitSpellImmune(f.hero));
    re_run_frame_at(f.hero, level.time + 1);
    T_ASSERT(!S_UnitSpellImmune(f.hero));
    T_ASSERT(S_SpellDamage(f.hero, f.enemy, 40));
    T_ASSERT(f.hero->health.value < 500);
    re_done(&f);
}

TEST(retail_item_effect, necklace_of_spell_immunity_protects_its_carrier) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\nC;Y1;X7;K\"DataE1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\n"
        "C;Y2;X3;K\"6\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"1\"\nC;Y2;X7;K\"1\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\n"
        "C;Y2;X1;K\"nspi\"\nC;Y2;X2;K\"AImx\"\nC;Y2;X3;K\"1\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    re_equip(&f, "nspi", 0);
    T_ASSERT(S_UnitSpellImmune(f.hero));
    T_ASSERT(!S_SpellDamage(f.hero, f.enemy, 40));
    T_FEQ(f.hero->health.value, 500, 0.001f);
    T_ASSERT(G_DropItem(f.hero, 0));
    T_ASSERT(!S_UnitSpellImmune(f.hero));
    re_done(&f);
}

TEST(retail_item_effect, runed_bracers_reduce_spell_damage_from_authored_datab) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\nC;Y1;X7;K\"DataE1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\n"
        "C;Y2;X3;K\"6\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"1\"\nC;Y2;X7;K\"1\"\n"
        "C;Y3;X1;K\"AIsr\"\nC;Y3;X2;K\"AIsr\"\n"
        "C;Y3;X3;K\"0\"\nC;Y3;X4;K\"0.33\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\n"
        "C;Y2;X1;K\"brac\"\nC;Y2;X2;K\"AIsr\"\nC;Y2;X3;K\"1\"\nE\n";
    retailEffectFixture_t f;
    float held_damage, unheld_damage;
    re_init(&f, ability_slk, items);
    re_equip(&f, "brac", 0);
    T_FEQ(S_ItemSpellDamageReduction(f.hero), 0.33f, 0.001f);
    T_ASSERT(S_SpellDamage(f.hero, f.enemy, 100));
    held_damage = 500.0f - f.hero->health.value;
    T_ASSERT(G_DropItem(f.hero, 0));
    T_FEQ(S_ItemSpellDamageReduction(f.hero), 0.0f, 0.001f);
    T_ASSERT(S_SpellDamage(f.hero, f.enemy, 100));
    unheld_damage = 500.0f - held_damage - f.hero->health.value;
    T_FEQ(held_damage / unheld_damage, 0.67f, 0.02f);
    re_done(&f);
}

TEST(retail_item_effect, orb_of_frost_slows_attack_and_movement_for_authored_duration) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X12\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\nC;Y1;X7;K\"DataE1\"\n"
        "C;Y1;X8;K\"UnitID1\"\nC;Y1;X9;K\"Dur1\"\nC;Y1;X10;K\"HeroDur1\"\n"
        "C;Y1;X11;K\"BuffID1\"\nC;Y1;X12;K\"levels\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"6\"\n"
        "C;Y2;X5;K\"1\"\nC;Y2;X6;K\"1\"\nC;Y2;X7;K\"1\"\nC;Y2;X12;K\"1\"\n"
        "C;Y3;X1;K\"AIob\"\nC;Y3;X2;K\"AIob\"\nC;Y3;X3;K\"6\"\n"
        "C;Y3;X9;K\"3\"\nC;Y3;X10;K\"1\"\nC;Y3;X11;K\"Bfro\"\nC;Y3;X12;K\"1\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\n"
        "C;Y2;X1;K\"ofro\"\nC;Y2;X2;K\"AIob\"\nC;Y2;X3;K\"true\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    re_equip(&f, "ofro", 0);
    T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, 6, 0.001f);
    S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("Bfro")));
    T_FEQ(unit_status_modifier_total(f.enemy, WC3_STATUS_MOD_MOVE_SPEED_PERCENT), -0.5f, 0.001f);
    T_FEQ(unit_status_modifier_total(f.enemy, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT), -0.25f, 0.001f);
    re_run_frame_at(f.enemy, level.time + 3000);
    T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey("Bfro")));
    T_FEQ(unit_status_modifier_total(f.enemy, WC3_STATUS_MOD_MOVE_SPEED_PERCENT), 0, 0.001f);
    T_FEQ(unit_status_modifier_total(f.enemy, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT), 0, 0.001f);
    re_done(&f);
}

TEST(retail_item_effect, orb_of_slow_grants_air_attack_and_authored_slow_proc) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y4;X12\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\nC;Y1;X7;K\"DataE1\"\n"
        "C;Y1;X8;K\"UnitID1\"\nC;Y1;X9;K\"Dur1\"\nC;Y1;X10;K\"HeroDur1\"\n"
        "C;Y1;X11;K\"BuffID1\"\nC;Y1;X12;K\"levels\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"6\"\n"
        "C;Y2;X5;K\"1\"\nC;Y2;X6;K\"1\"\nC;Y2;X7;K\"1\"\nC;Y2;X12;K\"1\"\n"
        "C;Y3;X1;K\"AIsb\"\nC;Y3;X2;K\"AIsb\"\nC;Y3;X3;K\"13\"\n"
        "C;Y3;X4;K\"100\"\nC;Y3;X8;K\"AIos\"\nC;Y3;X12;K\"1\"\n"
        "C;Y3;X5;K\"100\"\nC;Y3;X6;K\"100\"\n"
        "C;Y4;X1;K\"AIos\"\nC;Y4;X2;K\"Aslo\"\nC;Y4;X3;K\"0.4\"\n"
        "C;Y4;X4;K\"0.3\"\nC;Y4;X9;K\"7\"\nC;Y4;X10;K\"3\"\n"
        "C;Y4;X11;K\"Bslo\"\nC;Y4;X12;K\"1\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\n"
        "C;Y2;X1;K\"oslo\"\nC;Y2;X2;K\"AIsb\"\nC;Y2;X3;K\"true\"\nE\n";
    UnitWeapons_t weapons = { .attacksEnabled = 1 };
    retailEffectFixture_t f;
    edict_t *hero_target;
    re_init(&f, ability_slk, items);
    f.hero->data.UnitWeapons = &weapons;
    f.hero->unit_state->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
    f.enemy->targtype = TARG_AIR;
    T_ASSERT(!S_AttackCanTarget(f.hero, f.enemy));
    re_equip(&f, "oslo", 0);
    T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, 13, 0.001f);
    T_ASSERT(S_AttackCanTarget(f.hero, f.enemy));
    T_EQ(S_AbilityItem(FS_SLKKey("AIos")).ability->proc, CAbilitySlow);
    S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("Bslo")));
    heroabilitystatus_t *air_slow = unit_findstatus(f.enemy, FS_SLKKey("Bslo"));
    T_NOT_NULL(air_slow);
    if (air_slow) T_EQ(air_slow->timestamp, level.time + 7000);
    T_FEQ(S_HumanMoveFactor(f.enemy), 0.6f, 0.001f);
    T_FEQ(unit_status_modifier_total(f.enemy, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT), -0.3f, 0.001f);

    hero_target = alloc_test_unit(FS_SLKKey("Hpal"), 80, 0);
    hero_target->s.player = 1;
    hero_target->spawn_time = level.time;
    hero_target->targtype = TARG_GROUND;
    hero_target->health.value = hero_target->health.max_value = 500;
    hero_target->svflags |= SVF_MONSTER;
    S_ResolveAttackHit(f.hero, hero_target, 20);
    T_ASSERT(S_UnitHasStatus(hero_target, FS_SLKKey("Bslo")));
    heroabilitystatus_t *hero_slow = unit_findstatus(hero_target, FS_SLKKey("Bslo"));
    T_NOT_NULL(hero_slow);
    if (hero_slow) T_EQ(hero_slow->timestamp, level.time + 3000);
    T_ASSERT(G_DropItem(f.hero, 0));
    T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, 0, 0.001f);
    T_ASSERT(!S_AttackCanTarget(f.hero, f.enemy));
    re_done(&f);
}

TEST(retail_item_effect, orb_of_fire_splashes_full_attack_damage_to_nearby_enemies) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataC1\"\n"
        "C;Y1;X5;K\"DataD1\"\nC;Y1;X6;K\"DataE1\"\n"
        "C;Y1;X7;K\"Area1\"\nC;Y1;X8;K\"levels\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"6\"\n"
        "C;Y2;X4;K\"1\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"1\"\nC;Y2;X8;K\"1\"\n"
        "C;Y3;X1;K\"AIfb\"\nC;Y3;X2;K\"AIfb\"\nC;Y3;X3;K\"11\"\n"
        "C;Y3;X6;K\"2\"\nC;Y3;X7;K\"80\"\nC;Y3;X8;K\"1\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"ofir\"\nC;Y2;X2;K\"AIfb\"\nE\n";
    retailEffectFixture_t f;
    edict_t *nearby, *outside, *ranged_nearby, *ally;
    re_init(&f, ability_slk, items);
    f.enemy->s.origin2 = (vec2_t){ 64, 0 };
    nearby = alloc_test_unit(FS_SLKKey("hfoo"), 110, 0);
    outside = alloc_test_unit(FS_SLKKey("hfoo"), 180, 0);
    ranged_nearby = alloc_test_unit(FS_SLKKey("hfoo"), 240, 0);
    ally = alloc_test_unit(FS_SLKKey("hfoo"), 100, 20);
    nearby->s.player = outside->s.player = ranged_nearby->s.player = 1;
    ally->s.player = 0;
    nearby->svflags |= SVF_MONSTER;
    outside->svflags |= SVF_MONSTER;
    ranged_nearby->svflags |= SVF_MONSTER;
    ally->svflags |= SVF_MONSTER;
    nearby->targtype = outside->targtype = ranged_nearby->targtype = ally->targtype = TARG_GROUND;
    nearby->spawn_time = outside->spawn_time = ranged_nearby->spawn_time = ally->spawn_time = level.time;
    nearby->health.value = nearby->health.max_value = 500;
    outside->health.value = outside->health.max_value = 500;
    ranged_nearby->health.value = ranged_nearby->health.max_value = 500;
    ally->health.value = ally->health.max_value = 500;
    re_equip(&f, "ofir", 0);
    T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, 11, 0.001f);
    T_FEQ(S_SpellNumber(FS_SLKKey("AIfb"), ABILITY_NUMBER_AREA, 1), 80, 0.001f);
    T_ASSERT(S_SpellIsAliveTarget(nearby));
    T_ASSERT(S_SpellIsEnemy(f.hero, nearby));
    T_ASSERT(S_AttackHitAllowed(f.hero, f.enemy, nearby));
    S_ResolveAttackHit(f.hero, f.enemy, 100);
    T_FEQ(nearby->health.value, 400, 0.001f);
    T_FEQ(outside->health.value, 500, 0.001f);
    T_FEQ(ally->health.value, 500, 0.001f);
    f.hero->unit_state->attack1.weapon = WPN_MISSILE;
    S_ResolveAttackHit(f.hero, outside, 100);
    T_FEQ(ranged_nearby->health.value, 400, 0.001f);
    re_done(&f);
}

TEST(retail_item_effect, shield_of_honor_provides_authored_command_aura) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
        "C;Y1;X7;K\"targs1\"\nC;Y1;X8;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X4;K\"6\"\n"
        "C;Y2;X6;K\"1\"\nC;Y2;X7;K\"1\"\n"
        "C;Y3;X1;K\"AIcd\"\nC;Y3;X2;K\"AOac\"\n"
        "C;Y3;X3;K\"900\"\nC;Y3;X4;K\"0.1\"\nC;Y3;X5;K\"1\"\nC;Y3;X6;K\"1\"\n"
        "C;Y3;X7;K\"air,ground,friend,self,vuln,invu\"\nC;Y3;X8;K\"BOac\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\n"
        "C;Y2;X1;K\"shhn\"\nC;Y2;X2;K\"AIcd\"\nC;Y2;X3;K\"1\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    f.enemy->s.player = 0;
    re_equip(&f, "shhn", 0);
    level.framenum++;
    T_FEQ(S_CommandAuraAttackBonus(f.enemy), 0.1f, 0.001f);
    Vector2_set(&f.enemy->s.origin2, 1000, 0);
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_CommandAuraAttackBonus(f.enemy), 0.0f, 0.001f);
    Vector2_set(&f.enemy->s.origin2, 64, 0);
    T_ASSERT(G_DropItem(f.hero, 0));
    level.framenum++;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_CommandAuraAttackBonus(f.enemy), 0.0f, 0.001f);
    re_done(&f);
}

TEST(retail_item_effect, bladebane_armor_grants_nearby_units_one_armor) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y4;X11\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"DataB1\"\nC;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\n"
        "C;Y1;X7;K\"DataE1\"\nC;Y1;X8;K\"Area1\"\nC;Y1;X9;K\"targs1\"\n"
        "C;Y1;X10;K\"BuffID1\"\nC;Y1;X11;K\"levels\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"6\"\n"
        "C;Y2;X4;K\"1\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"1\"\nC;Y2;X7;K\"1\"\nC;Y2;X11;K\"1\"\n"
        "C;Y3;X1;K\"AId7\"\nC;Y3;X2;K\"AIde\"\nC;Y3;X3;K\"7\"\nC;Y3;X11;K\"1\"\n"
        "C;Y4;X1;K\"AIad\"\nC;Y4;X2;K\"AHad\"\nC;Y4;X3;K\"1\"\nC;Y4;X8;K\"900\"\n"
        "C;Y4;X9;K\"air,ground,friend,self,vuln,invu\"\nC;Y4;X10;K\"BHad\"\nC;Y4;X11;K\"1\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"blba\"\nC;Y2;X2;K\"AId7,AIad\"\nE\n";
    retailEffectFixture_t f;
    float const base_armor = 0.0f;
    re_init(&f, ability_slk, items);
    f.enemy->s.player = 0;
    re_equip(&f, "blba", 0);
    level.framenum++;
    T_FEQ(S_DevotionArmorBonus(f.enemy), 1.0f, 0.001f);
    T_FEQ(G_UnitArmorValue(f.enemy), base_armor + 1.0f, 0.001f);
    Vector2_set(&f.enemy->s.origin2, 1000, 0);
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(f.enemy), 0.0f, 0.001f);
    T_FEQ(G_UnitArmorValue(f.enemy), base_armor, 0.001f);
    re_done(&f);
}

TEST(retail_item_effect, gloves_of_spell_mastery_control_summoned_units_from_inventory) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X11\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs1\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
        "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"BuffID1\"\n"
        "C;Y1;X10;K\"DataA1\"\nC;Y1;X11;K\"DataC1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X5;K\"1\"\nC;Y2;X6;K\"1\"\nC;Y2;X7;K\"1\"\nC;Y2;X9;K\"1\"\n"
        "C;Y2;X10;K\"6\"\nC;Y2;X11;K\"1\"\n"
        "C;Y3;X1;K\"AIcm\"\nC;Y3;X2;K\"Acmg\"\nC;Y3;X3;K\"1\"\n"
        "C;Y3;X4;K\"air,ground,enemy,neutral,ward\"\nC;Y3;X5;K\"25\"\n"
        "C;Y3;X6;K\"5\"\nC;Y3;X7;K\"700\"\nC;Y3;X8;K\"0.3\"\nC;Y3;X9;K\"Bcmg\"\nC;Y3;X10;K\"5\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y1;X3;K\"uses\"\nC;Y1;X4;K\"perishable\"\n"
        "C;Y2;X1;K\"gvsm\"\nC;Y2;X2;K\"AIcm,AIi6\"\nC;Y2;X3;K\"1\"\nC;Y2;X4;K\"true\"\nE\n";
    retailEffectFixture_t f;
    edict_t *summoner, *summon, *player, *gloves;
    gameClient_t *client;
    re_init(&f, ability_slk, items);
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    player = &g_edicts[0];
    player->client->ps.number = 0;
    client = player->client;
    summoner = alloc_test_unit(FS_SLKKey("oshm"), 256, 0);
    summon = f.enemy;
    summoner->s.player = summon->s.player = 1;
    summoner->svflags |= SVF_MONSTER;
    summoner->spawn_time = level.time;
    summon->targtype = TARG_GROUND;
    summon->owner = summoner;
    summon->summon_ability = FS_SLKKey("Asum");
    summon->health.value = 200;
    f.hero->mana.value = f.hero->mana.max_value = 100;
    G_SelectEntity(client, f.hero);
    gloves = re_equip(&f, "gvsm", 0);
    G_GetItem(gloves)->charges = 1;
    G_UseItem(f.hero, 0);
    T_NOT_NULL(client->menu.on_entity_selected);
    if (client->menu.on_entity_selected) client->menu.on_entity_selected(player, summon);
    T_EQ(summon->s.player, 0u);
    T_EQ(summon->owner, f.hero);
    re_done(&f);
}

TEST(retail_item_effect, warsong_battle_drums_provide_authored_command_aura) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataC1\"\n"
        "C;Y1;X6;K\"targs1\"\nC;Y1;X7;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X4;K\"6\"\nC;Y2;X5;K\"1\"\n"
        "C;Y3;X1;K\"AIcd\"\nC;Y3;X2;K\"AOac\"\n"
        "C;Y3;X3;K\"900\"\nC;Y3;X4;K\"0.1\"\n"
        "C;Y3;X6;K\"air,ground,friend,self,vuln,invu\"\nC;Y3;X7;K\"BOac\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"ward\"\nC;Y2;X2;K\"AIcd\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    f.enemy->s.player = 0;
    re_equip(&f, "ward", 0);
    level.framenum++;
    T_FEQ(S_CommandAuraAttackBonus(f.enemy), 0.1f, 0.001f);
    re_done(&f);
}

TEST(retail_item_effect, legion_doom_horn_provides_authored_unholy_aura) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
        "C;Y1;X7;K\"targs1\"\nC;Y1;X8;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X4;K\"6\"\nC;Y2;X6;K\"1\"\n"
        "C;Y3;X1;K\"AIau\"\nC;Y3;X2;K\"AUau\"\n"
        "C;Y3;X3;K\"900\"\nC;Y3;X4;K\"0.1\"\nC;Y3;X5;K\"0.5\"\n"
        "C;Y3;X7;K\"air,ground,friend,self,vuln,invu\"\nC;Y3;X8;K\"BUau\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\n"
        "C;Y2;X1;K\"lgdh\"\nC;Y2;X2;K\"AIau\"\nC;Y2;X3;K\"1\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    f.enemy->s.player = 0;
    re_equip(&f, "lgdh", 0);
    level.framenum++;
    T_FEQ(S_UnholyMoveBonus(f.enemy), 0.1f, 0.001f);
    T_FEQ(S_UnholyHealthRegen(f.enemy), 0.5f, 0.001f);
    Vector2_set(&f.enemy->s.origin2, 1000, 0);
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_UnholyMoveBonus(f.enemy), 0.0f, 0.001f);
    T_FEQ(S_UnholyHealthRegen(f.enemy), 0.0f, 0.001f);
    Vector2_set(&f.enemy->s.origin2, 64, 0);
    T_ASSERT(G_DropItem(f.hero, 0));
    level.framenum++;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_UnholyMoveBonus(f.enemy), 0.0f, 0.001f);
    T_FEQ(S_UnholyHealthRegen(f.enemy), 0.0f, 0.001f);
    re_done(&f);
}

TEST(retail_item_effect, allerias_flute_boosts_nearby_ranged_units) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"targs1\"\nC;Y1;X7;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X4;K\"6\"\nC;Y2;X5;K\"1\"\n"
        "C;Y3;X1;K\"AIar\"\nC;Y3;X2;K\"AEar\"\n"
        "C;Y3;X3;K\"900\"\nC;Y3;X4;K\"0.1\"\n"
        "C;Y3;X6;K\"air,ground,friend,self,vuln,invu\"\nC;Y3;X7;K\"BEar\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"afac\"\nC;Y2;X2;K\"AIar\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    f.enemy->s.player = 0;
    f.enemy->unit_state->attack1.type = ATK_NORMAL;
    re_equip(&f, "afac", 0);
    level.framenum++;
    T_FEQ(S_TrueshotAttackBonus(f.enemy), 0.0f, 0.001f);
    f.enemy->unit_state->attack1.weapon = WPN_MISSILE;
    T_FEQ(S_TrueshotAttackBonus(f.enemy), 0.1f, 0.001f);
    Vector2_set(&f.enemy->s.origin2, 1000, 0);
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_TrueshotAttackBonus(f.enemy), 0.0f, 0.001f);
    re_done(&f);
}

TEST(retail_item_effect, mindstaff_provides_authored_brilliance_aura) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"targs1\"\nC;Y1;X7;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X4;K\"6\"\nC;Y2;X5;K\"1\"\n"
        "C;Y3;X1;K\"AIba\"\nC;Y3;X2;K\"AHab\"\n"
        "C;Y3;X3;K\"900\"\nC;Y3;X4;K\"0.75\"\n"
        "C;Y3;X6;K\"air,ground,friend,self,vuln,invu\"\nC;Y3;X7;K\"BHab\"\n"
        "C;Y4;X1;K\"AI2m\"\nC;Y4;X2;K\"AImm\"\nC;Y4;X4;K\"200\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"mnsf\"\nC;Y2;X2;K\"AI2m,AIba\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    f.enemy->s.player = 0;
    re_equip(&f, "mnsf", 0);
    level.framenum++;
    T_FEQ(S_BrillianceManaRegen(f.enemy), 0.75f, 0.001f);
    Vector2_set(&f.enemy->s.origin2, 1000, 0);
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_BrillianceManaRegen(f.enemy), 0.0f, 0.001f);
    re_done(&f);
}

TEST(retail_item_effect, rusty_mining_pick_bash_stuns_on_attack) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y4;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"DataC1\"\nC;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\nC;Y1;X7;K\"DataB1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"6\"\nC;Y2;X4;K\"1\"\n"
        "C;Y3;X1;K\"AItg\"\nC;Y3;X2;K\"AItg\"\nC;Y3;X3;K\"1\"\n"
        "C;Y4;X1;K\"AIbx\"\nC;Y4;X2;K\"AHbh\"\nC;Y4;X3;K\"100\"\n"
        "C;Y4;X4;K\"25\"\nC;Y4;X5;K\"2\"\nC;Y4;X6;K\"2\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"rump\"\nC;Y2;X2;K\"AItg,AIbx\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    f.enemy->unit_state->attack1.type = ATK_NORMAL;
    f.enemy->targtype = TARG_GROUND;
    re_equip(&f, "rump", 0);
    S_ResolveAttackHit(f.hero, f.enemy, 10);
    T_ASSERT(f.enemy->health.value < 500.0f);
    T_NOT_NULL(unit_findstatus(f.enemy, MAKEFOURCC('B','s','t','u')));
    T_EQ(unit_findstatus(f.enemy, MAKEFOURCC('B','s','t','u'))->timestamp, level.time + 2000);
    re_done(&f);
}

TEST(retail_item_effect, ancient_janggo_provides_authored_endurance_aura) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\nC;Y1;X7;K\"targs1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X4;K\"6\"\nC;Y2;X6;K\"1\"\n"
        "C;Y3;X1;K\"AIae\"\nC;Y3;X2;K\"AOae\"\nC;Y3;X3;K\"900\"\n"
        "C;Y3;X4;K\"0.1\"\nC;Y3;X5;K\"0.05\"\nC;Y3;X7;K\"air,ground,friend,self,vuln,invu\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\n"
        "C;Y2;X1;K\"ajen\"\nC;Y2;X2;K\"AIae\"\nC;Y2;X3;K\"1\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    f.enemy->s.player = 0;
    re_equip(&f, "ajen", 0);
    level.framenum++;
    T_FEQ(S_EnduranceMoveBonus(f.enemy), 0.1f, 0.001f);
    T_FEQ(S_EnduranceAttackBonus(f.enemy), 0.05f, 0.001f);
    Vector2_set(&f.enemy->s.origin2, 1000, 0);
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_EnduranceMoveBonus(f.enemy), 0.0f, 0.001f);
    T_FEQ(S_EnduranceAttackBonus(f.enemy), 0.0f, 0.001f);
    re_done(&f);
}

TEST(retail_item_effect, gem_of_true_sight_detects_enemies_from_carrier) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Rng1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataC1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X4;K\"6\"\nC;Y2;X5;K\"1\"\n"
        "C;Y3;X1;K\"Adt1\"\nC;Y3;X2;K\"Adet\"\nC;Y3;X3;K\"1100\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\n"
        "C;Y2;X1;K\"gemt\"\nC;Y2;X2;K\"Adt1\"\nC;Y2;X3;K\"1\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    re_equip(&f, "gemt", 0);
    T_ASSERT(S_UnitIsDetectedByPlayer(f.enemy, 0));
    Vector2_set(&f.enemy->s.origin2, 1200, 0);
    T_ASSERT(!S_UnitIsDetectedByPlayer(f.enemy, 0));
    Vector2_set(&f.enemy->s.origin2, 64, 0);
    T_ASSERT(G_DropItem(f.hero, 0));
    T_ASSERT(!S_UnitIsDetectedByPlayer(f.enemy, 0));
    re_done(&f);
}


TEST(retail_item_effect, item_cooldown_id_blocks_shared_item_use) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"levels\"\nC;Y1;X4;K\"targs\"\n"
        "C;Y1;X5;K\"Area1\"\nC;Y1;X6;K\"Dur1\"\n"
        "C;Y1;X7;K\"Cool1\"\nC;Y1;X8;K\"CasterArt\"\n"
        "C;Y2;X1;K\"AItb\"\nC;Y2;X2;K\"AItb\"\n"
        "C;Y2;X3;K\"1\"\nC;Y2;X4;K\"air,ground,enemy\"\n"
        "C;Y2;X5;K\"100\"\nC;Y2;X6;K\"20\"\n"
        "C;Y2;X7;K\"30\"\n"
        "C;Y2;X8;K\"Abilities\\\\Spells\\\\Items\\\\AItb\\\\AItbTarget.mdl\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"dust\"\nC;Y2;X2;K\"AItb\"\nE\n";
    static ItemData_t item_data = { .abilList = "AItb", .cooldownID = "A001" };
    retailEffectFixture_t f;
    edict_t *player, *dust;
    gameClient_t *client;
    uint32_t const shared_cooldown = MAKEFOURCC('A','0','0','1');

    re_init(&f, ability_slk, items);
    player = G_GetPlayerEntityByNumber(0);
    client = player ? player->client : NULL;
    T_NOT_NULL(client);
    if (!client) { re_done(&f); return; }
    f.hero->s.player = 0;
    G_SelectEntity(client, f.hero);
    dust = re_equip(&f, "dust", 0);
    dust->data.ItemData = &item_data;

    G_UseItem(f.hero, 0);
    T_NOT_NULL(unit_findstatus(f.enemy, FS_SLKKey("Bdet")));
    T_ASSERT(!S_SpellCooldownReady(f.hero, shared_cooldown));
    unit_removestatus(f.enemy, unit_findstatus(f.enemy, FS_SLKKey("Bdet")), STATUS_REMOVE_SCRIPT);
    G_UseItem(f.hero, 0);
    T_NULL(unit_findstatus(f.enemy, FS_SLKKey("Bdet")));
    T_ASSERT(!S_SpellCooldownReady(f.hero, shared_cooldown));

    level.time += 30000;
    G_UseItem(f.hero, 0);
    T_NOT_NULL(unit_findstatus(f.enemy, FS_SLKKey("Bdet")));
    re_done(&f);
}

/* This item is stocked as oli2 -> AIll, with parent AIsb. The legacy AIlb orb
 * is not this item. AIll owns the +damage pickup handler while the on-hit
 * effect dispatches the separately authored AIpg Purge. */
TEST(retail_item_effect, oli2_AIll_resolves_attack_bonus_handler) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X11\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\n"
        "C;Y1;X7;K\"DataE1\"\nC;Y1;X8;K\"UnitID1\"\n"
        "C;Y1;X11;K\"DataA1\"\n"
        "C;Y2;X1;K\"AIll\"\nC;Y2;X2;K\"AIsb\"\n"
        "C;Y2;X3;K\"5\"\nC;Y2;X4;K\"35\"\n"
        "C;Y2;X5;K\"10\"\nC;Y2;X6;K\"35\"\n"
        "C;Y2;X7;K\"2\"\nC;Y2;X8;K\"AIpg\"\nC;Y2;X12;K\"1\"\n"
        "C;Y3;X1;K\"AInv\"\nC;Y3;X2;K\"AInv\"\nC;Y3;X3;K\"6\"\n"
        "C;Y3;X5;K\"1\"\nC;Y3;X6;K\"1\"\nC;Y3;X7;K\"1\"\nC;Y3;X12;K\"1\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"oli2\"\nC;Y2;X2;K\"AIll\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    edict_t *item = re_equip(&f, "oli2", 0);
    T_STREQ(G_ItemAbilityList(item), "AIll");
    abilityitem_t const ability = S_AbilityItem(FS_SLKKey("AIll"));
    T_EQ(ability.code, FS_SLKKey("AIll"));
    T_NOT_NULL(ability.ability);
    if (ability.ability) T_EQ(ability.ability->proc, CAbilitySpellEffectBonus);
    T_EQ(G_AbilityCode(FS_SLKKey("AIll")), FS_SLKKey("AIsb"));
    re_done(&f);
}

/* A custom amount must change both the effective armor and physical damage. */
TEST(retail_item_effect, ocor_nonstock_data_changes_armor_and_damage) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\n"
        "C;Y1;X7;K\"BuffID1\"\nC;Y1;X8;K\"DataE1\"\n"
        "C;Y1;X9;K\"DataC1\"\nC;Y1;X10;K\"DataD1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\n"
        "C;Y2;X3;K\"6\"\nC;Y2;X8;K\"1\"\nC;Y2;X9;K\"1\"\nC;Y2;X10;K\"1\"\n"
        "C;Y3;X1;K\"AIcb\"\nC;Y3;X2;K\"AIcb\"\n"
        "C;Y3;X3;K\"5\"\nC;Y3;X4;K\"7\"\n"
        "C;Y3;X5;K\"3\"\nC;Y3;X6;K\"3\"\n"
        "C;Y3;X7;K\"BIcb\"\nC;Y3;X8;K\"2\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\n"
        "C;Y2;X1;K\"ocor\"\nC;Y2;X2;K\"AIcb\"\nC;Y2;X3;K\"true\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    float const base = G_UnitArmorValue(f.enemy);
    re_equip(&f, "ocor", 0);
    S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("BIcb")));
    T_FEQ(G_UnitArmorValue(f.enemy), base - 7, 0.001f);
    T_EQ(G_AttackDamage(f.hero, f.enemy, 100), 135);
    re_run_frame_at(f.enemy, level.time + 3000);
    T_FEQ(G_UnitArmorValue(f.enemy), base, 0.001f);
    re_done(&f);
}

/* Exercise production attacks and serialized status descriptors. Synthetic
 * rows retain stock targets/columns while aliases change magnitude and BuffID. */
static void re_corruption(corruptMode_t mode) {
    char abilities[3200];
    bool const alias = mode == CORRUPT_ALIAS || mode == CORRUPT_CUSTOM_BUFF || mode == CORRUPT_RANK || mode == CORRUPT_WEAKER;
    bool const native = mode == CORRUPT_NATIVE || mode == CORRUPT_RANK;
    bool const ranged = mode == CORRUPT_RANGED || mode == CORRUPT_RANGED_TYPE;
    bool const weapon = ranged || mode == CORRUPT_MELEE || mode == CORRUPT_MISS;
    bool const blocked = mode == CORRUPT_MANUAL || mode == CORRUPT_AIR ||
        mode == CORRUPT_INVULNERABLE || mode == CORRUPT_CARRY || mode == CORRUPT_FROST_FIRST || mode == CORRUPT_MISS;
    cstring_t buff = mode == CORRUPT_CUSTOM_BUFF ? "B900" : "BIcb";
    cstring_t code = alias ? "A900" : "AIcb";
    float amount = mode == CORRUPT_RANK ? 9 : alias ? 7 : 4;
    char const *items = "ID;PWXL;N;EBB;Y3;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\n"
        "C;Y2;X1;K\"ocor\"\nC;Y2;X2;K\"AIcb\"\nC;Y2;X3;K\"true\"\n"
        "C;Y3;X1;K\"ofro\"\nC;Y3;X2;K\"AIob\"\nC;Y3;X3;K\"true\"\nE\n";
    retailEffectFixture_t f;
    UnitAbilities_t const immune = { .abilList = "Amim" };
    UnitAbilities_t const evasive = { .abilList = "AEev" };
    UnitWeapons_t const weapons = { .attacksEnabled = 1 };
    ItemData_t const alias_item = { .id = MAKEFOURCC('o','c','o','r'), .abilList = "A900", .droppable = true };
    uint32_t target_number;
    heroabilitystatus_t *status;

    snprintf(abilities, sizeof(abilities), "ID;PWXL;N;EBB;Y7;X18\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
        "C;Y1;X7;K\"DataD1\"\nC;Y1;X8;K\"DataE1\"\nC;Y1;X9;K\"Dur1\"\n"
        "C;Y1;X10;K\"HeroDur1\"\nC;Y1;X11;K\"BuffID1\"\nC;Y1;X12;K\"targs1\"\n"
        "C;Y1;X13;K\"DataB2\"\nC;Y1;X14;K\"Dur2\"\nC;Y1;X15;K\"HeroDur2\"\n"
        "C;Y1;X16;K\"BuffID2\"\nC;Y1;X17;K\"targs2\"\nC;Y1;X18;K\"DataA2\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"6\"\nC;Y2;X6;K\"%d\"\nC;Y2;X7;K\"1\"\nC;Y2;X8;K\"1\"\n"
        "C;Y3;X1;K\"AIcb\"\nC;Y3;X2;K\"AIcb\"\nC;Y3;X3;K\"1\"\n"
        "C;Y3;X4;K\"5\"\nC;Y3;X5;K\"4\"\nC;Y3;X8;K\"2\"\n"
        "C;Y3;X9;K\"5\"\nC;Y3;X10;K\"2\"\nC;Y3;X11;K\"BIcb\"\n"
        "C;Y3;X12;K\"%s\"\n"
        "C;Y4;X1;K\"A900\"\nC;Y4;X2;K\"AIcb\"\nC;Y4;X3;K\"2\"\n"
        "C;Y4;X4;K\"5\"\nC;Y4;X5;K\"7\"\nC;Y4;X13;K\"9\"\n"
        "C;Y4;X8;K\"2\"\nC;Y4;X9;K\"5\"\nC;Y4;X10;K\"5\"\n"
        "C;Y4;X11;K\"%s\"\nC;Y4;X12;K\"ground,air,ward\"\n"
        "C;Y4;X14;K\"5\"\nC;Y4;X15;K\"5\"\nC;Y4;X16;K\"BIcb\"\n"
        "C;Y4;X17;K\"ground,air,ward\"\nC;Y4;X18;K\"5\"\n"
        "C;Y5;X1;K\"AIob\"\nC;Y5;X2;K\"AIob\"\nC;Y5;X3;K\"1\"\n"
        "C;Y5;X4;K\"6\"\nC;Y5;X9;K\"3\"\nC;Y5;X11;K\"Bfro\"\n"
        "C;Y6;X1;K\"Amim\"\nC;Y6;X2;K\"Amim\"\nC;Y6;X3;K\"1\"\n"
        "C;Y7;X1;K\"AEev\"\nC;Y7;X2;K\"AEev\"\nC;Y7;X3;K\"1\"\nC;Y7;X4;K\"1\"\nE\n",
        mode != CORRUPT_CARRY, mode == CORRUPT_AIR ? "ground" : "ground,air,ward", buff);
    re_init(&f, abilities, items);
    if (mode == CORRUPT_HERO) {
        f.enemy->class_id = FS_SLKKey("Hpal");
        f.enemy->data.UnitBalance = f.hero->data.UnitBalance;
        f.enemy->unit_state->hero.level = 1;
        T_ASSERT(G_UnitIsHero(f.enemy));
    }
    f.hero->unit_state->hero.level = 1;
    f.enemy->unit_state->armor_value = 6;
    f.enemy->unit_state->defense_type = 7;
    f.hero->unit_state->attack1.weapon = WPN_NORMAL;
    f.hero->unit_state->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
    f.hero->data.UnitWeapons = &weapons;
    if (mode == CORRUPT_IMMUNE) f.enemy->data.UnitAbilities = &immune;
    if (mode == CORRUPT_MISS) f.enemy->data.UnitAbilities = &evasive;
    if (mode == CORRUPT_RANGED_TYPE) f.enemy->unit_state->defense_type = 0;
    if (mode == CORRUPT_SOURCE_ILLUSION) f.hero->aiflags |= AI_ILLUSION;
    if (mode == CORRUPT_VICTIM_ILLUSION) f.enemy->aiflags |= AI_ILLUSION;
    if (mode == CORRUPT_SOURCE_ILLUSION || mode == CORRUPT_VICTIM_ILLUSION) amount = 0;
    if (mode == CORRUPT_AIR) f.enemy->targtype = TARG_AIR;
    if (mode == CORRUPT_INVULNERABLE) G_SetUnitFlag(f.enemy, UNIT_FLAG_INVULNERABLE, true);
    if (native) {
        f.hero_abilities.abilList = mode == CORRUPT_RANK ? "AInv,A900" : "AInv,AIcb";
        if (mode == CORRUPT_RANK) T_EQ(G_UnitSetAbilityLevel(f.hero, FS_SLKKey(code), 2), 2);
    } else if (mode == CORRUPT_CARRY) {
        edict_t *item = alloc_test_unit(FS_SLKKey("ocor"), 16, 0);
        item->targtype = TARG_ITEM;
        G_AddItem(item); G_GetItem(item)->in_world = true; G_GetItem(item)->inventory_slot = -1;
        T_ASSERT(G_AddItemToSlot(f.hero, item, 0));
    } else {
        edict_t *item = re_equip(&f, "ocor", mode == CORRUPT_FROST_FIRST ? 1 : 0);
        if (alias) item->data.ItemData = &alias_item;
        if (mode == CORRUPT_FROST_FIRST) re_equip(&f, "ofro", 0);
    }
    if (weapon) {
        f.hero->unit_state->attack1.damageBase = 100;
        f.hero->unit_state->attack1.numberOfDice = 0;
        f.hero->unit_state->attack1.range = 128;
        T_ASSERT(S_AttackCanTarget(f.hero, f.enemy));
        T_ASSERT(S_OrderAttack(f.hero, f.enemy));
        if (ranged) {
            f.hero->unit_state->attack1.weapon = WPN_MISSILE;
            f.hero->unit_state->attack1.projectile.speed = 1000;
            f.hero->unit_state->attack1.projectile.model = G_RegisterModel("TestUI\\Models\\anim_pulse.mdx");
            attack_ranged(f.hero);
        } else attack_melee(f.hero);
        T_ASSERT(!S_UnitIsDisarmed(f.hero));
        f.hero->wait = 0.01f;
        f.hero->currentmove->think(f.hero);
        if (ranged) {
            edict_t *missile = NULL;
            FILTER_EDICTS(ent, ent->owner == f.hero && ent->movetype == MOVETYPE_FLYMISSILE) {
                missile = ent; break;
            }
            T_NOT_NULL(missile);
            T_FEQ(f.enemy->health.value, 500, .001f);
            if (mode == CORRUPT_RANGED_TYPE) f.hero->unit_state->attack1.type = ATK_PIERCE;
            FOR_LOOP(tick, 20) {
                if (!missile || !missile->inuse || f.enemy->health.value < 500) break;
                re_run_frame_at(missile, level.time + FRAMETIME);
            }
        }
        /* First hit includes the held +5, against armor 6-4, not armor six. */
        T_FEQ(f.enemy->health.value, mode == CORRUPT_MISS ? 500 : 407, .001f);
    } else if (mode == CORRUPT_MANUAL) {
        S_ResolveAttackHitWithArrow(f.hero, f.enemy, 20, FS_SLKKey("AHfa"), 1);
    } else S_ResolveAttackHit(f.hero, f.enemy, 20);
    status = unit_findstatus(f.enemy, FS_SLKKey(buff));
    if (blocked) {
        T_NULL(status);
        T_FEQ(G_UnitArmorValue(f.enemy), 6, .001f);
        re_done(&f);
        return;
    }
    T_NOT_NULL(status);
    T_FEQ(G_UnitArmorValue(f.enemy), 6 - amount, .001f);
    if (status) T_EQ(status->source_ability, FS_SLKKey(code));
    if (mode == CORRUPT_REFRESH) {
        re_run_frame_at(f.enemy, level.time + 3000);
        S_ResolveAttackHit(f.hero, f.enemy, 20);
        T_FEQ(G_UnitArmorValue(f.enemy), 2, .001f);
        re_run_frame_at(f.enemy, level.time + 2001);
        T_FEQ(G_UnitArmorValue(f.enemy), 2, .001f);
    }
    if (mode == CORRUPT_WEAKER) {
        /* Both aliases add the same +5 attack bonus; only their debuff differs. */
        T_ASSERT(G_DropItem(f.hero, 0));
        re_equip(&f, "ocor", 0);
        T_STREQ(G_ItemAbilityList(G_InventoryItem(f.hero, 0)), "AIcb");
        S_ResolveAttackHit(f.hero, f.enemy, 20);
        T_FEQ(G_UnitArmorValue(f.enemy), 2, .001f);
    }
    if (mode == CORRUPT_HERO) {
        re_run_frame_at(f.enemy, level.time + 1999);
        T_FEQ(G_UnitArmorValue(f.enemy), 2, .001f);
    }
    if (mode == CORRUPT_DROP) {
        T_ASSERT(G_DropItem(f.hero, 0));
        T_FEQ(G_UnitArmorValue(f.enemy), 2, .001f);
    }
    if (mode == CORRUPT_MAGIC_DISPEL) {
        T_EQ(S_DispelUnitMagic(f.enemy), 0);
        T_FEQ(G_UnitArmorValue(f.enemy), 2, .001f);
    }
    if (mode == CORRUPT_BASIC_REMOVE || mode == CORRUPT_EX_REMOVE) {
        char script[800];
        snprintf(script, sizeof(script),
            "function clean takes nothing returns nothing\n"
            "  call %s\nendfunction\n"
            "function main takes nothing returns nothing\n"
            "  local group targets = CreateGroup()\n"
            "  call GroupEnumUnitsOfPlayer(targets, Player(1), null)\n"
            "  call ForGroup(targets, function clean)\n"
            "  call DestroyGroup(targets)\nendfunction\n",
            mode == CORRUPT_BASIC_REMOVE ? "UnitRemoveBuffs(GetEnumUnit(), false, true)" :
                "UnitRemoveBuffsEx(GetEnumUnit(), false, true, false, false, false, false, false)");
        T_ASSERT(run_test_jass(script));
        T_NULL(unit_findstatus(f.enemy, FS_SLKKey(buff)));
        T_FEQ(G_UnitArmorValue(f.enemy), 6, .001f);
    }
    if (mode == CORRUPT_SAVE) {
        cstring_t path = Test_TempPath("corruption-status-save.bin");
        target_number = f.enemy->s.number;
        T_ASSERT(WriteGame(path));
        T_ASSERT(ReadGame(path));
        f.enemy = g_edicts + target_number;
        T_FEQ(G_UnitArmorValue(f.enemy), 2, .001f);
        T_EQ(S_DispelUnitMagic(f.enemy), 0);
        remove(path);
    }
    if (mode == CORRUPT_DISPEL && status) unit_removestatus(f.enemy, status, STATUS_REMOVE_DISPEL);
    else re_run_frame_at(f.enemy, level.time + (mode == CORRUPT_HERO ? 1 : 5000));
    T_NULL(unit_findstatus(f.enemy, FS_SLKKey(buff)));
    T_FEQ(G_UnitArmorValue(f.enemy), 6, .001f);
    re_done(&f);
}

TEST(retail_item_effect, corruption_stock_armor_and_expiry) { re_corruption(CORRUPT_STOCK); }
TEST(retail_item_effect, corruption_item_alias_owns_armor) { re_corruption(CORRUPT_ALIAS); }
TEST(retail_item_effect, corruption_custom_buff_retains_modifier) { re_corruption(CORRUPT_CUSTOM_BUFF); }
TEST(retail_item_effect, corruption_native_ability) { re_corruption(CORRUPT_NATIVE); }
TEST(retail_item_effect, corruption_ranked_native_alias) { re_corruption(CORRUPT_RANK); }
TEST(retail_item_effect, corruption_refresh_extends_without_stacking) { re_corruption(CORRUPT_REFRESH); }
TEST(retail_item_effect, corruption_status_removal_restores_armor) { re_corruption(CORRUPT_DISPEL); }
TEST(retail_item_effect, corruption_item_drop_retains_existing_debuff) { re_corruption(CORRUPT_DROP); }
TEST(retail_item_effect, corruption_save_roundtrip_retains_armor_and_expiry) { re_corruption(CORRUPT_SAVE); }
TEST(retail_item_effect, corruption_melee_first_hit_uses_reduced_armor) { re_corruption(CORRUPT_MELEE); }
TEST(retail_item_effect, corruption_manual_arrow_suppresses_item) { re_corruption(CORRUPT_MANUAL); }
TEST(retail_item_effect, corruption_spell_immunity_keeps_physical_modifier) { re_corruption(CORRUPT_IMMUNE); }
TEST(retail_item_effect, corruption_illusion_source_does_not_reduce_armor) { re_corruption(CORRUPT_SOURCE_ILLUSION); }
TEST(retail_item_effect, corruption_illusion_victim_does_not_reduce_armor) { re_corruption(CORRUPT_VICTIM_ILLUSION); }
TEST(retail_item_effect, corruption_authored_target_mask_rejects_air) { re_corruption(CORRUPT_AIR); }
TEST(retail_item_effect, corruption_invulnerability_rejects_status) { re_corruption(CORRUPT_INVULNERABLE); }
TEST(retail_item_effect, corruption_carry_only_inventory_rejects_modifier) { re_corruption(CORRUPT_CARRY); }
TEST(retail_item_effect, corruption_lower_slot_respects_frost_orb) { re_corruption(CORRUPT_FROST_FIRST); }
TEST(retail_item_effect, corruption_ranged_first_hit_uses_reduced_armor) { re_corruption(CORRUPT_RANGED); }
TEST(retail_item_effect, corruption_ranged_keeps_launch_type) { re_corruption(CORRUPT_RANGED_TYPE); }
TEST(retail_item_effect, corruption_missed_weapon_hit_does_not_apply) { re_corruption(CORRUPT_MISS); }
TEST(retail_item_effect, corruption_magic_dispel_retains_debuff) { re_corruption(CORRUPT_MAGIC_DISPEL); }
TEST(retail_item_effect, corruption_basic_jass_removal_restores_armor) { re_corruption(CORRUPT_BASIC_REMOVE); }
TEST(retail_item_effect, corruption_script_removal_without_type_filter) { re_corruption(CORRUPT_EX_REMOVE); }
TEST(retail_item_effect, corruption_weaker_refresh_replaces_amount) { re_corruption(CORRUPT_WEAKER); }
TEST(retail_item_effect, corruption_hero_duration_is_independent) { re_corruption(CORRUPT_HERO); }

TEST(retail_item_effect, oli2_AIll_applies_authored_attack_bonus_and_purge_proc) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y4;X12\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\n"
        "C;Y1;X7;K\"DataE1\"\nC;Y1;X8;K\"UnitID1\"\n"
        "C;Y1;X9;K\"Dur1\"\nC;Y1;X10;K\"HeroDur1\"\n"
        "C;Y1;X11;K\"BuffID1\"\nC;Y1;X12;K\"levels\"\n"
        "C;Y2;X1;K\"AIll\"\nC;Y2;X2;K\"AIsb\"\n"
        "C;Y2;X3;K\"5\"\nC;Y2;X4;K\"100\"\n"
        "C;Y2;X5;K\"10\"\nC;Y2;X6;K\"35\"\n"
        "C;Y2;X7;K\"2\"\nC;Y2;X8;K\"AIpg\"\nC;Y2;X12;K\"1\"\n"
        "C;Y3;X1;K\"AIpg\"\nC;Y3;X2;K\"Aprg\"\n"
        "C;Y3;X3;K\"5\"\nC;Y3;X5;K\"150\"\nC;Y3;X6;K\"3\"\n"
        "C;Y3;X7;K\"1\"\nC;Y3;X9;K\"15\"\n"
        "C;Y3;X10;K\"5\"\nC;Y3;X11;K\"Bprg\"\nC;Y3;X12;K\"1\"\n"
        "C;Y4;X1;K\"AInv\"\nC;Y4;X2;K\"AInv\"\nC;Y4;X3;K\"6\"\n"
        "C;Y4;X5;K\"1\"\nC;Y4;X6;K\"1\"\nC;Y4;X7;K\"1\"\nC;Y4;X12;K\"1\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"oli2\"\nC;Y2;X2;K\"AIll\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    re_equip(&f, "oli2", 0);
    T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, 5, 0.001f);
    heroabilitystatus_t *buff = unit_applystatus(f.enemy, &(status_application_t){
        .buff = "Binf", .level = 1, .duration = 30.0f,
        .buff_flags = WC3_STATUS_BUFF_MAGICAL | WC3_STATUS_BUFF_POSITIVE
    });
    T_NOT_NULL(buff);
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("Binf")));
    S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey("Binf")));
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("Bprg")));
    T_FEQ(S_PurgeMoveReduction(f.enemy), 1.0f, 0.001f); /* DataD=35 pauses the target */
    re_done(&f);
}

TEST(retail_item_effect, oli2_AIll_damages_enemy_summon_with_nested_ai_pg_value) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y4;X12\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\n"
        "C;Y1;X7;K\"DataE1\"\nC;Y1;X8;K\"UnitID1\"\n"
        "C;Y1;X9;K\"Dur1\"\nC;Y1;X10;K\"HeroDur1\"\n"
        "C;Y1;X11;K\"BuffID1\"\nC;Y1;X12;K\"levels\"\n"
        "C;Y2;X1;K\"AIll\"\nC;Y2;X2;K\"AIsb\"\n"
        "C;Y2;X3;K\"5\"\nC;Y2;X4;K\"100\"\n"
        "C;Y2;X5;K\"10\"\nC;Y2;X6;K\"100\"\n"
        "C;Y2;X7;K\"2\"\nC;Y2;X8;K\"AIpg\"\nC;Y2;X12;K\"1\"\n"
        "C;Y3;X1;K\"AIpg\"\nC;Y3;X2;K\"Aprg\"\n"
        "C;Y3;X3;K\"5\"\nC;Y3;X5;K\"150\"\nC;Y3;X6;K\"3\"\n"
        "C;Y3;X7;K\"1\"\nC;Y3;X9;K\"15\"\nC;Y3;X10;K\"5\"\n"
        "C;Y3;X11;K\"Bprg\"\nC;Y3;X12;K\"1\"\n"
        "C;Y4;X1;K\"AInv\"\nC;Y4;X2;K\"AInv\"\nC;Y4;X3;K\"6\"\n"
        "C;Y4;X5;K\"1\"\nC;Y4;X6;K\"1\"\nC;Y4;X7;K\"1\"\nC;Y4;X12;K\"1\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"oli2\"\nC;Y2;X2;K\"AIll\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    re_equip(&f, "oli2", 0);
    f.enemy->owner = f.hero;
    f.enemy->summon_ability = FS_SLKKey("Awat");
    f.enemy->health.value = f.enemy->health.max_value = 500;
    S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_FEQ(f.enemy->health.value, 330, 0.001f);
    re_done(&f);
}

/* chance zero must never trigger the nested Purge ability. */
TEST(retail_item_effect, oli2_AIll_respects_zero_proc_chance) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y4;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"UnitID1\"\nC;Y1;X6;K\"DataC1\"\n"
        "C;Y1;X7;K\"DataD1\"\nC;Y1;X8;K\"DataE1\"\nC;Y1;X9;K\"levels\"\n"
        "C;Y2;X1;K\"AIll\"\nC;Y2;X2;K\"AIsb\"\nC;Y2;X3;K\"5\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"AIpg\"\nC;Y2;X6;K\"1\"\n"
        "C;Y3;X1;K\"AIpg\"\nC;Y3;X2;K\"Aprg\"\nC;Y3;X5;K\"150\"\nC;Y3;X6;K\"1\"\n"
        "C;Y4;X1;K\"AInv\"\nC;Y4;X2;K\"AInv\"\nC;Y4;X3;K\"6\"\n"
        "C;Y4;X5;K\"1\"\nC;Y4;X6;K\"1\"\nC;Y4;X7;K\"1\"\nC;Y4;X8;K\"1\"\nC;Y4;X9;K\"1\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"oli2\"\nC;Y2;X2;K\"AIll\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    re_equip(&f, "oli2", 0);
    unit_addtimedstatus(f.enemy, "Binf", 1, 30.0f);
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("Binf")));
    S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("Binf")));
    T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey("Bprg")));
    re_done(&f);
}

/* AIpl is caster-only; AIsl applies in its authored area. Both stop on damage
 * through the ordinary attack event, and recovery ticks through G_RunEntity. */
TEST(retail_item_effect, regeneration_items_follow_target_damage_and_scheduler) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"HeroDur1\"\n"
        "C;Y1;X7;K\"Area1\"\nC;Y1;X8;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AIsl\"\nC;Y2;X2;K\"AIsl\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"30\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"10\"\nC;Y2;X7;K\"80\"\nC;Y2;X8;K\"BIrg\"\n"
        "C;Y3;X1;K\"AIpl\"\nC;Y3;X2;K\"AIpl\"\nC;Y3;X3;K\"1\"\n"
        "C;Y3;X4;K\"0\"\nC;Y3;X5;K\"100\"\nC;Y3;X6;K\"10\"\nC;Y3;X7;K\"80\"\nC;Y3;X8;K\"BIrg\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X2\nC;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y2;X1;K\"test\"\nC;Y2;X2;K\"AIsl,AIpl\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    edict_t *ally = alloc_test_unit(FS_SLKKey("hfoo"), 40, 0);
    ally->s.player = 0; ally->svflags |= SVF_MONSTER; ally->spawn_time = level.time;
    f.hero->health.value = 400; ally->health.value = 400;
    f.hero->mana.value = 300; ally->mana.value = 300;
    static UnitBalance_t hero_balance;
    memset(&hero_balance, 0, sizeof(hero_balance));
    hero_balance.strength = 10;
    f.hero->data.UnitBalance = ally->data.UnitBalance = &hero_balance;
    abilityitem_t item = S_AbilityItem(FS_SLKKey("AIsl"));
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    T_ASSERT(S_AbilityMessage(f.hero, A_ITEM_USE, &call));
    T_FEQ(f.hero->health.value, 403, 0.001f);
    T_FEQ(ally->health.value, 403, 0.001f);
    re_run_frame_at(f.hero, level.time + 1000);
    T_FEQ(f.hero->health.value, 406, 0.001f);
    S_ResolveAttackHit(f.enemy, f.hero, 5);
    T_NULL(unit_findstatus(f.hero, FS_SLKKey("BIrg")));
    T_NOT_NULL(unit_findstatus(ally, FS_SLKKey("BIrg")));
    T_FEQ(f.hero->health.value, 401, 0.001f);

    /* Damage at the exact expiry timestamp interrupts before the scheduler can
     * apply the final due regeneration pulse. */
    item = S_AbilityItem(FS_SLKKey("AIsl")); call.item = &item;
    T_ASSERT(S_AbilityMessage(f.hero, A_ITEM_USE, &call));
    float const before_expiry_hit = f.hero->health.value;
    level.time += 10000;
    S_ResolveAttackHit(f.enemy, f.hero, 5);
    T_NULL(unit_findstatus(f.hero, FS_SLKKey("BIrg")));
    T_FEQ(f.hero->health.value, before_expiry_hit - 5, 0.001f);

    re_run_frame_at(ally, level.time + 1000);
    T_ASSERT(ally->health.value > 403.0f);

    item = S_AbilityItem(FS_SLKKey("AIpl")); call.item = &item;
    T_ASSERT(S_AbilityMessage(f.hero, A_ITEM_USE, &call));
    T_FEQ(f.hero->mana.value, 310, 0.001f);
    T_FEQ(ally->mana.value, 300, 0.001f);
    re_run_frame_at(f.hero, level.time + 1000);
    T_FEQ(f.hero->mana.value, 320, 0.001f);
    S_ResolveAttackHit(f.enemy, f.hero, 5);
    T_NULL(unit_findstatus(f.hero, FS_SLKKey("BIrg")));
    re_run_frame_at(f.hero, level.time + 1000);
    T_FEQ(f.hero->mana.value, 320, 0.001f);

    item = S_AbilityItem(FS_SLKKey("AIsl")); call.item = &item;
    float const before_final_regen = f.hero->health.value;
    T_ASSERT(S_AbilityMessage(f.hero, A_ITEM_USE, &call));
    T_FEQ(f.hero->health.value, before_final_regen + 3, 0.001f);
    T_ASSERT(S_SpellDamage(f.hero, f.enemy, 5));
    T_NULL(unit_findstatus(f.hero, FS_SLKKey("BIrg")));
    float const after_spell_interrupt = f.hero->health.value;
    re_run_frame_at(f.hero, level.time + 1000);
    T_FEQ(f.hero->health.value, after_spell_interrupt, 0.001f);
    re_done(&f);
}


/* AIp1-AIp4 are caster-only replenishment potions. AIp5-AIp6 are area
 * replenishment scrolls. The retail AbilityData rows provide each amount,
 * duration, area, and BIrg lifecycle. */
TEST(retail_item_effect, replenishment_potions_and_scrolls_use_authored_targets_and_interrupt) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y7;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"Dur1\"\nC;Y1;X7;K\"HeroDur1\"\n"
        "C;Y1;X8;K\"Area1\"\nC;Y1;X9;K\"BuffID1\"\nC;Y1;X10;K\"targs1\"\n"
        "C;Y2;X1;K\"AIp1\"\nC;Y2;X2;K\"AIp1\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"100\"\nC;Y2;X5;K\"25\"\nC;Y2;X6;K\"30\"\nC;Y2;X7;K\"30\"\nC;Y2;X8;K\"0\"\nC;Y2;X9;K\"BIrg\"\nC;Y2;X10;K\"friend,self,organic\"\n"
        "C;Y3;X1;K\"AIp5\"\nC;Y3;X2;K\"AIp5\"\nC;Y3;X3;K\"1\"\n"
        "C;Y3;X4;K\"250\"\nC;Y3;X5;K\"100\"\nC;Y3;X6;K\"45\"\nC;Y3;X7;K\"45\"\nC;Y3;X8;K\"100\"\nC;Y3;X9;K\"BIrg\"\nC;Y3;X10;K\"friend,self,organic\"\n"
        "C;Y4;X1;K\"AIp6\"\nC;Y4;X2;K\"AIp6\"\nC;Y4;X3;K\"1\"\n"
        "C;Y4;X4;K\"450\"\nC;Y4;X5;K\"150\"\nC;Y4;X6;K\"45\"\nC;Y4;X7;K\"45\"\nC;Y4;X8;K\"100\"\nC;Y4;X9;K\"BIrg\"\nC;Y4;X10;K\"friend,self,organic\"\nE\n";
    retailEffectFixture_t f;
    edict_t *nearby, *outside, *target;
    abilityitem_t item;
    abilityCall_t call;
    static UnitBalance_t no_regen;
    memset(&no_regen, 0, sizeof(no_regen));
    no_regen.healthRegenType = "none";
    re_init(&f, ability_slk, "ID;PWXL;N;EBB;Y2;X2\nC;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y2;X1;K\"test\"\nC;Y2;X2;K\"AIp1,AIp5\"\nE\n");
    f.hero->data.UnitBalance = &no_regen;
    f.hero->health.value = 100; f.hero->health.max_value = 500;
    f.hero->mana.value = 50; f.hero->mana.max_value = 250;
    nearby = alloc_test_unit(FS_SLKKey("hfoo"), 40, 0);
    outside = alloc_test_unit(FS_SLKKey("hfoo"), 160, 0);
    nearby->s.player = outside->s.player = 0;
    nearby->svflags |= SVF_MONSTER; outside->svflags |= SVF_MONSTER;
    nearby->spawn_time = outside->spawn_time = level.time;
    nearby->targtype = outside->targtype = TARG_GROUND;
    nearby->data.UnitBalance = outside->data.UnitBalance = &no_regen;
    nearby->health.value = 100; nearby->health.max_value = 500;
    nearby->mana.value = 50; nearby->mana.max_value = 250;
    outside->health.value = 100; outside->health.max_value = 500;
    outside->mana.value = 50; outside->mana.max_value = 250;

    item = S_AbilityItem(FS_SLKKey("AIp1"));
    T_EQ(item.ability ? item.ability->proc : 0, CAbilityItemRegeneration);
    for (uint32_t i = 2; i <= 4; i++) { char code[] = {'A', 'I', 'p', (char)('0' + i), '\0'}; T_EQ(S_AbilityItem(FS_SLKKey(code)).ability->proc, CAbilityItemRegeneration); }
    call = MAKE(abilityCall_t, .item = &item);
    T_ASSERT(G_AbilityLevel(FS_SLKKey("AIp1"), 1) != NULL);
    T_ASSERT(S_SpellIsAliveTarget(f.hero));
    T_ASSERT(S_SpellIsFriend(f.hero, f.hero));
    T_ASSERT(S_SpellAllowsAreaTarget(FS_SLKKey("AIp1"), f.hero, f.hero));
    T_FEQ(S_SpellHeroDuration(FS_SLKKey("AIp1"), 1, f.hero), 30.0f, 0.001f);
    T_ASSERT(G_AbilityLevel(FS_SLKKey("AIp1"), 1)->area >= 0.0f);
    T_ASSERT(S_AbilityMessage(f.hero, A_ITEM_USE, &call));
    T_FEQ(f.hero->health.value, 100.0f + 100.0f / 30.0f, 0.001f);
    T_FEQ(f.hero->mana.value, 50.0f + 25.0f / 30.0f, 0.001f);
    T_NULL(unit_findstatus(nearby, FS_SLKKey("BIrg")));
    S_ResolveAttackHit(f.enemy, f.hero, 5);
    T_NULL(unit_findstatus(f.hero, FS_SLKKey("BIrg")));

    target = nearby;
    item = S_AbilityItem(FS_SLKKey("AIp5")); call.item = &item;
    T_ASSERT(S_AbilityMessage(f.hero, A_ITEM_USE, &call));
    T_NOT_NULL(unit_findstatus(nearby, FS_SLKKey("BIrg")));
    T_FEQ(nearby->health.value, 100.0f + 250.0f / 45.0f, 0.001f);
    T_FEQ(nearby->mana.value, 50.0f + 100.0f / 45.0f, 0.001f);
    T_ASSERT(S_SpellIsAliveTarget(nearby));
    T_ASSERT(S_SpellIsFriend(f.hero, nearby));
    T_ASSERT(S_SpellAllowsAreaTarget(FS_SLKKey("AIp5"), f.hero, nearby));
    T_NULL(unit_findstatus(outside, FS_SLKKey("BIrg")));
    S_ResolveAttackHit(f.enemy, target, 5);
    T_NULL(unit_findstatus(target, FS_SLKKey("BIrg")));
    float const after_damage = target->health.value;
    re_run_frame_at(target, level.time + 1000);
    T_FEQ(target->health.value, after_damage, 0.001f);
    target->health.value = 100; target->mana.value = 50;
    item = S_AbilityItem(FS_SLKKey("AIp6")); call.item = &item;
    T_ASSERT(S_AbilityMessage(f.hero, A_ITEM_USE, &call));
    T_FEQ(target->health.value, 100.0f + 450.0f / 45.0f, 0.001f);
    T_FEQ(target->mana.value, 50.0f + 150.0f / 45.0f, 0.001f);
    re_done(&f);
}


/* AIpr is the full Clarity Potion: caster-only mana restoration over 45 s. */
TEST(retail_item_effect, clarity_potion_regenerates_mana_and_interrupts_on_damage) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"DataB1\"\nC;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\nC;Y1;X7;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AIpr\"\nC;Y2;X2;K\"AIpr\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"200\"\nC;Y2;X5;K\"45\"\nC;Y2;X6;K\"45\"\nC;Y2;X7;K\"BIrg\"\nE\n";
    retailEffectFixture_t f;
    static UnitBalance_t no_regen;
    memset(&no_regen, 0, sizeof(no_regen));
    no_regen.healthRegenType = "none";
    re_init(&f, ability_slk, "ID;PWXL;N;EBB;Y2;X2\nC;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y2;X1;K\"pclr\"\nC;Y2;X2;K\"AIpr\"\nE\n");
    f.hero->data.UnitBalance = &no_regen;
    f.hero->mana.value = 100; f.hero->mana.max_value = 500;
    abilityitem_t item = S_AbilityItem(FS_SLKKey("AIpr"));
    T_EQ(item.ability ? item.ability->proc : 0, CAbilityItemRegeneration);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    T_ASSERT(S_AbilityMessage(f.hero, A_ITEM_USE, &call));
    T_FEQ(f.hero->health.value, 500.0f, 0.001f);
    T_FEQ(f.hero->mana.value, 100.0f + 200.0f / 45.0f, 0.001f);
    T_NOT_NULL(unit_findstatus(f.hero, FS_SLKKey("BIrg")));
    S_ResolveAttackHit(f.enemy, f.hero, 10);
    T_NULL(unit_findstatus(f.hero, FS_SLKKey("BIrg")));
    float const after_interrupt = f.hero->mana.value;
    re_run_frame_at(f.hero, level.time + 1000);
    T_FEQ(f.hero->mana.value, after_interrupt, 0.001f);
    re_done(&f);
}


/* Retail Flare Gun (fgun/AIfa) is a charged, point-targeted Flare. */
TEST(retail_item_effect, flare_gun_targets_a_point_and_reveals_authored_radius) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"Area1\"\n"
        "C;Y2;X1;K\"AIfa\"\nC;Y2;X2;K\"AIfa\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"20\"\nC;Y2;X5;K\"100\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y1;X3;K\"uses\"\nC;Y1;X4;K\"perishable\"\n"
        "C;Y2;X1;K\"fgun\"\nC;Y2;X2;K\"AIfa\"\nC;Y2;X3;K\"3\"\nC;Y2;X4;K\"true\"\nE\n";
    retailEffectFixture_t f;
    edict_t *player, *item;
    vec2_t center = { 128, 128 }, nearby = { 180, 128 }, outside = { 512, 128 };
    cstring_t point_click[] = { "point", "128", "128" };
    re_init(&f, ability_slk, items);
    player = &g_edicts[0];
    player->client->ps.number = 0;
    game.clients[0].ps.rdflags &= ~RDF_NOFOG;
    f.hero->s.player = 0; f.hero->svflags |= SVF_MONSTER; f.hero->targtype = TARG_GROUND;
    f.hero->runtime.sight_radius.day = f.hero->runtime.sight_radius.night = 0;
    G_SelectEntity(player->client, f.hero);
    item = re_equip(&f, "fgun", 0);
    G_GetItem(item)->charges = 3;
    G_FowInit(); G_FowConnectPlayer(0);
    G_FowUpdate();
    abilityitem_t flare = S_AbilityItem(FS_SLKKey("AIfa"));
    T_NOT_NULL(flare.ability);
    if (!flare.ability) { G_FowShutdown(); re_done(&f); return; }
    T_EQ(flare.ability->proc, CAbilityFlare);
    cstring_t inventory_click[] = { "inventory", "0" };
    G_ClientCommand(player, 2, inventory_click);
    T_NOT_NULL(player->client->menu.on_location_selected);
    T_EQ(G_ItemCharges(item), 3);
    G_ClientCommand(player, 3, point_click);
    T_NULL(player->client->menu.on_location_selected);
    T_EQ(G_ItemCharges(item), 2);
    T_ASSERT(G_FowPointIsVisible(0, &center));
    T_ASSERT(G_FowPointIsVisible(0, &nearby));
    T_ASSERT(!G_FowPointIsVisible(0, &outside));
    FILTER_EDICTS(thinker, thinker->inuse && thinker->owner == f.hero &&
                  thinker->class_id == FS_SLKKey("AIfa") && G_GetChannel(thinker)) {
        T_FEQ(thinker->s.origin2.x, center.x, 0.001f);
        T_FEQ(thinker->s.origin2.y, center.y, 0.001f);
        T_EQ(thinker->spawn_time, level.time + 20000);
        G_FowShutdown(); re_done(&f); return;
    }
    T_ASSERT(false);
    G_FowShutdown(); re_done(&f);
}

/* Retail Crystal Ball (crys/AIta) reveals the selected point for ten seconds. */
TEST(retail_item_effect, crystal_ball_targets_a_point_and_reveals_authored_radius) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"Area1\"\nC;Y1;X6;K\"DataA1\"\nC;Y1;X7;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AIta\"\nC;Y2;X2;K\"AIta\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"10\"\nC;Y2;X5;K\"900\"\nC;Y2;X6;K\"3\"\nC;Y2;X7;K\"Bdet\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"perishable\"\n"
        "C;Y2;X1;K\"crys\"\nC;Y2;X2;K\"AIta\"\nC;Y2;X3;K\"false\"\nE\n";
    retailEffectFixture_t f;
    edict_t *player;
    vec2_t center = { 128, 128 }, nearby = { 768, 128 }, outside = { -900, 128 };
    cstring_t point_click[] = { "point", "128", "128" };
    re_init(&f, ability_slk, items);
    player = &g_edicts[0];
    player->client->ps.number = 0;
    game.clients[0].ps.rdflags &= ~RDF_NOFOG;
    f.hero->s.player = 0; f.hero->svflags |= SVF_MONSTER; f.hero->targtype = TARG_GROUND;
    f.hero->runtime.sight_radius.day = f.hero->runtime.sight_radius.night = 0;
    G_SelectEntity(player->client, f.hero);
    re_equip(&f, "crys", 0);
    f.enemy->s.origin2 = nearby;
    f.enemy->s.origin.x = nearby.x; f.enemy->s.origin.y = nearby.y;
    f.enemy->runtime.sight_radius.day = f.enemy->runtime.sight_radius.night = 0;
    f.enemy->s.renderfx |= RF_HIDDEN;
    T_ASSERT(unit_addtimedstatus(f.enemy, "Binv", 1, 20.0f));
    T_ASSERT(!S_UnitIsDetectedByPlayer(f.enemy, 0));
    G_FowInit(); G_FowConnectPlayer(0); G_FowUpdate();
    T_ASSERT(!G_FowPointIsVisible(0, &outside));
    abilityitem_t reveal = S_AbilityItem(FS_SLKKey("AIta"));
    T_NOT_NULL(reveal.ability);
    if (!reveal.ability) { G_FowShutdown(); re_done(&f); return; }
    T_EQ(reveal.ability->proc, CAbilityItemDetectAoe);
    G_UseItem(f.hero, 0);
    T_NOT_NULL(player->client->menu.on_location_selected);
    G_ClientCommand(player, 3, point_click);
    T_NULL(player->client->menu.on_location_selected);
    T_ASSERT(G_FowPointIsVisible(0, &center));
    T_ASSERT(G_FowPointIsVisible(0, &nearby));
    T_ASSERT(!G_FowPointIsVisible(0, &outside));
    T_ASSERT(S_UnitIsDetectedByPlayer(f.enemy, 0));
    /* Detection belongs to the revealed area, not the units present at cast. */
    f.enemy->s.origin2 = outside;
    T_ASSERT(!S_UnitIsDetectedByPlayer(f.enemy, 0));
    edict_t *entrant = alloc_test_unit(FS_SLKKey("hfoo"), outside.x, outside.y);
    entrant->s.player = 1;
    entrant->svflags |= SVF_MONSTER;
    entrant->s.renderfx |= RF_HIDDEN;
    T_ASSERT(unit_addtimedstatus(entrant, "Binv", 1, 20.0f));
    T_ASSERT(!S_UnitIsDetectedByPlayer(entrant, 0));
    entrant->s.origin2 = nearby;
    T_ASSERT(S_UnitIsDetectedByPlayer(entrant, 0));
    T_ASSERT(!S_UnitIsDetectedByPlayer(entrant, 2));
    f.enemy->s.origin2 = nearby;
    FILTER_EDICTS(thinker, thinker->inuse && thinker->owner == f.hero &&
                  thinker->class_id == FS_SLKKey("AIta") && G_GetChannel(thinker)) {
        T_FEQ(thinker->s.origin2.x, center.x, 0.001f);
        T_FEQ(thinker->s.origin2.y, center.y, 0.001f);
        T_EQ(thinker->spawn_time, level.time + 10000);
        level.time += 10000;
        G_RunEntity(thinker);
        G_RunEntity(f.enemy);
        G_FowUpdate();
        T_ASSERT(!S_UnitIsDetectedByPlayer(f.enemy, 0));
        T_ASSERT(!G_FowPointIsVisible(0, &center));
        G_FowShutdown(); re_done(&f); return;
    }
    T_ASSERT(false);
    G_FowShutdown(); re_done(&f);
}

TEST(retail_item_effect, diamond_of_summoning_targets_area_and_relocates_up_to_twelve_owned_units) {
    static UnitData_t const foot_data = { .moveTypeName = "foot" };
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"targs1\"\n"
        "C;Y2;X1;K\"AUds\"\nC;Y2;X2;K\"AUds\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"700\"\nC;Y2;X5;K\"12\"\nC;Y2;X6;K\"99999\"\n"
        "C;Y2;X7;K\"air,ground,vuln,invu,player\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y1;X3;K\"uses\"\nC;Y1;X4;K\"perishable\"\n"
        "C;Y2;X1;K\"dsum\"\nC;Y2;X2;K\"AUds\"\nC;Y2;X3;K\"1\"\nC;Y2;X4;K\"true\"\nE\n";
    retailEffectFixture_t f;
    edict_t *player, *item, *owned[13];
    vec2_t source = { 1024, 0 };
    cstring_t point_click[] = { "point", "1024", "0" };
    re_init(&f, ability_slk, items);
    player = &g_edicts[0];
    player->client->ps.number = 0;
    game.clients[0].ps.rdflags &= ~RDF_NOFOG;
    f.hero->s.player = 0; f.hero->svflags |= SVF_MONSTER; f.hero->targtype = TARG_GROUND;
    f.hero->collision = 32;
    f.hero->data.UnitData = &foot_data;
    G_SelectEntity(player->client, f.hero);
    item = re_equip(&f, "dsum", 0);
    G_GetItem(item)->charges = 1;
    FOR_LOOP(i, 13) {
        owned[i] = alloc_test_unit(FS_SLKKey("hfoo"), source.x, source.y);
        owned[i]->s.player = 0; owned[i]->svflags |= SVF_MONSTER;
        owned[i]->targtype = TARG_GROUND;
        owned[i]->collision = 32;
        owned[i]->data.UnitData = &foot_data;
        owned[i]->movetype = MOVETYPE_STEP;
        owned[i]->spawn_time = level.time;
        gi.LinkEntity(owned[i]);
    }
    f.enemy->s.origin2 = source;
    f.enemy->s.player = 1;
    f.enemy->collision = 32;
    f.enemy->data.UnitData = &foot_data;
    gi.LinkEntity(f.hero);
    gi.LinkEntity(f.enemy);
    G_FowInit(); G_FowConnectPlayer(0);
    G_UseItem(f.hero, 0);
    T_NOT_NULL(player->client->menu.on_location_selected);
    if (!player->client->menu.on_location_selected) { G_FowShutdown(); re_done(&f); return; }
    G_ClientCommand(player, 3, point_click);
    T_NULL(player->client->menu.on_location_selected);
    T_EQ(G_ItemCharges(item), 0);
    uint32_t moved = 0;
    FOR_LOOP(i, 13) if (Vector2_distance(&owned[i]->s.origin2, &source) > 500.0f) moved++;
    T_EQ(moved, 12);
    T_FEQ(Vector2_distance(&owned[12]->s.origin2, &source), 0.0f, 0.001f);
    T_FEQ(f.enemy->s.origin2.x, source.x, 0.001f);
    T_FEQ(f.enemy->s.origin2.y, source.y, 0.001f);
    for (uint32_t i = 0; i < 12; i++) for (uint32_t j = i + 1; j < 12; j++)
        T_ASSERT(Vector2_distance(&owned[i]->s.origin2, &owned[j]->s.origin2) > 0.01f);
    G_FowShutdown(); re_done(&f);
}

TEST(retail_item_effect, spider_silk_targets_and_ensnares_an_enemy_flyer) {
    static UnitData_t const flyer_data = { .moveTypeName = "fly", .moveHeight = 180.0f };
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"BuffID1\"\n"
        "C;Y1;X8;K\"DataA1\"\nC;Y1;X9;K\"DataB1\"\nC;Y1;X10;K\"DataC1\"\n"
        "C;Y2;X1;K\"AIwb\"\nC;Y2;X2;K\"AIwb\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"20\"\nC;Y2;X5;K\"7\"\nC;Y2;X6;K\"400\"\nC;Y2;X7;K\"Bwea,Bweb\"\n"
        "C;Y2;X8;K\"0.6\"\nC;Y2;X9;K\"200\"\nC;Y2;X10;K\"128\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"uses\"\nC;Y1;X4;K\"perishable\"\n"
        "C;Y2;X1;K\"silk\"\nC;Y2;X2;K\"AIwb\"\nC;Y2;X3;K\"1\"\nC;Y2;X4;K\"true\"\nE\n";
    retailEffectFixture_t f;
    edict_t *player, *item;
    char number[16];
    cstring_t select[] = { "select", number };
    re_init(&f, ability_slk, items);
    player = &g_edicts[0];
    player->client->ps.number = 0;
    f.hero->s.player = 0;
    f.enemy->s.player = 1;
    f.enemy->targtype = TARG_AIR;
    f.enemy->aiflags |= AI_FLYING;
    f.enemy->unitinfo.FlyHeight = 180.0f;
    f.enemy->data.UnitData = &flyer_data;
    item = re_equip(&f, "silk", 0);
    G_GetItem(item)->charges = 1;
    G_SelectEntity(player->client, f.hero);
    abilityitem_t web = S_AbilityItem(FS_SLKKey("AIwb"));
    T_NOT_NULL(web.ability);
    if (!web.ability) { re_done(&f); return; }
    T_EQ(web.ability->proc, CAbilityWeb);
    G_UseItem(f.hero, 0);
    T_NOT_NULL(player->client->menu.on_entity_selected);
    snprintf(number, sizeof(number), "%u", f.enemy->s.number);
    G_ClientCommand(player, 2, select);
    T_NULL(player->client->menu.on_entity_selected);
    T_ASSERT(S_UnitIsEnsnared(f.enemy));
    T_EQ(G_UnitStatusLevel(f.enemy, FS_SLKKey("Bwea")), 1);
    T_ASSERT(!(f.enemy->aiflags & AI_FLYING));
    T_EQ(f.enemy->targtype, TARG_GROUND);
    level.time += 1000;
    S_RunAbilityUpdates(f.enemy);
    T_FEQ(f.enemy->unitinfo.FlyHeight, 0.0f, 0.001f);
    T_EQ(G_ItemCharges(item), 0);
    re_done(&f);
}

/* Wand of Shadowsight must enter unit targeting from the inventory item path,
 * then grant its caster temporary vision of the chosen target. */
TEST(retail_item_effect, wand_of_shadowsight_targets_unit_from_inventory) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
        "C;Y1;X4;K\"HeroDur1\"\nC;Y1;X5;K\"DataA1\"\n"
        "C;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"levels\"\n"
        "C;Y2;X1;K\"Ashs\"\nC;Y2;X2;K\"Ashs\"\n"
        "C;Y2;X3;K\"5\"\nC;Y2;X4;K\"5\"\n"
        "C;Y2;X5;K\"0\"\nC;Y2;X6;K\"Bshs\"\nC;Y2;X7;K\"1\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"perishable\"\n"
        "C;Y2;X1;K\"wshs\"\nC;Y2;X2;K\"Ashs\"\nC;Y2;X3;K\"true\"\nE\n";
    retailEffectFixture_t f;
    edict_t *player, *item;
    char number[16];
    cstring_t select[] = { "select", number };
    re_init(&f, ability_slk, items);
    player = &g_edicts[0];
    player->client->ps.number = 0;
    game.clients[0].ps.rdflags &= ~RDF_NOFOG;
    f.hero->s.player = 0;
    f.hero->runtime.sight_radius.day = f.hero->runtime.sight_radius.night = 600.0f;
    f.enemy->s.player = 1;
    f.enemy->s.origin2 = (vec2_t){ 512, 0 };
    f.enemy->s.origin.x = 512;
    f.enemy->runtime.sight_radius.day = f.enemy->runtime.sight_radius.night = 0;
    f.enemy->unit_state->armor_value = 8.0f;
    G_SelectEntity(player->client, f.hero);
    item = re_equip(&f, "wshs", 0);
    G_GetItem(item)->charges = 3;
    T_NOT_NULL(G_ItemAbilityList(item));
    T_NOT_NULL(FindAbilityForCommand("Ashs"));
    T_ASSERT(S_AbilityHasCommand(FindAbilityForCommand("Ashs")));
    G_FowInit(); G_FowConnectPlayer(0); G_FowConnectPlayer(2); G_FowUpdate();
    T_ASSERT(G_FowPlayerCanSeeEntity(0, f.enemy));

    G_UseItem(f.hero, 0);
    T_NOT_NULL(player->client->menu.on_entity_selected);
    if (!player->client->menu.on_entity_selected) { G_FowShutdown(); re_done(&f); return; }
    T_EQ(G_ItemCharges(item), 3);
    snprintf(number, sizeof(number), "%u", f.enemy->s.number);
    G_ClientCommand(player, 2, select);
    T_NULL(player->client->menu.on_entity_selected);
    T_EQ(G_ItemCharges(item), 2);
    f.hero->runtime.sight_radius.day = f.hero->runtime.sight_radius.night = 0;
    f.enemy->s.origin2 = (vec2_t){ 1024, 0 };
    f.enemy->s.origin.x = 1024;
    G_FowUpdate();
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("Bshs")));
    T_EQ(unit_findstatus(f.enemy, FS_SLKKey("Bshs"))->data, 1);
    T_FEQ(G_UnitArmorValue(f.enemy), 8.0f, 0.001f);
    T_ASSERT(G_FowPlayerCanSeeEntity(0, f.enemy));
    T_ASSERT(!G_FowPlayerCanSeeEntity(2, f.enemy));

    level.time += 5000;
    G_RunEntity(f.enemy);
    G_FowUpdate();
    T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey("Bshs")));
    T_ASSERT(!G_FowPlayerCanSeeEntity(0, f.enemy));
    G_FowShutdown();
    re_done(&f);
}

TEST(retail_item_effect, talisman_of_evasion_causes_attacks_to_miss_using_item_data) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y2;X1;K\"AIev\"\nC;Y2;X2;K\"AEev\"\nC;Y2;X3;K\"1\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"evtl\"\nC;Y2;X2;K\"AIev\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    f.enemy->unit_state->attack1.type = ATK_NORMAL;
    f.enemy->unit_state->attack1.targetsAllowed = TARG_GROUND;
    float const initial_health = f.hero->health.value;
    S_ResolveAttackHit(f.enemy, f.hero, 10);
    T_FEQ(f.hero->health.value, initial_health - 10.0f, 0.001f);
    re_equip(&f, "evtl", 0);
    f.hero->health.value = initial_health;
    S_ResolveAttackHit(f.enemy, f.hero, 10);
    T_FEQ(f.hero->health.value, initial_health, 0.001f);
    re_done(&f);
}



TEST(retail_item_effect, pnvu_click_has_timed_invulnerability_and_cooldown) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y4;X11\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"levels\"\nC;Y1;X4;K\"targs\"\n"
        "C;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\n"
        "C;Y1;X7;K\"Cool1\"\nC;Y1;X8;K\"BuffID1\"\n"
        "C;Y1;X9;K\"DataA1\"\nC;Y1;X10;K\"DataC1\"\nC;Y1;X11;K\"DataD1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\n"
        "C;Y2;X3;K1\nC;Y2;X9;K6\nC;Y2;X10;K1\nC;Y2;X11;K1\n"
        "C;Y3;X1;K\"AIvu\"\nC;Y3;X2;K\"AAsm\"\n"
        "C;Y3;X3;K1\nC;Y3;X4;K\"vuln,invu\"\n"
        "C;Y3;X5;K2\nC;Y3;X6;K2\nC;Y3;X7;K8\nC;Y3;X8;K\"Bvul\"\n"
        "C;Y4;X1;K\"A001\"\nC;Y4;X2;K\"A001\"\n"
        "C;Y4;X3;K1\nC;Y4;X4;K\"ground,enemy,vuln\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y1;X3;K\"uses\"\nC;Y1;X4;K\"usable\"\n"
        "C;Y1;X5;K\"perishable\"\nC;Y1;X6;K\"cooldownID\"\n"
        "C;Y2;X1;K\"pnvu\"\nC;Y2;X2;K\"AIvu\"\n"
        "C;Y2;X3;K2\nC;Y2;X4;K1\nC;Y2;X5;K0\nC;Y2;X6;K\"AIvu\"\nE\n";
    retailEffectFixture_t f;
    edict_t *player, *potion;
    re_init(&f, ability_slk, items);
    player = G_GetPlayerEntityByNumber(0);
    T_NOT_NULL(player);
    if (!player || !player->client) { re_done(&f); return; }
    G_SelectEntity(player->client, f.hero);
    potion = re_equip(&f, "pnvu", 0);
    G_GetItem(potion)->charges = 2;
    T_ASSERT(!S_UnitInvulnerable(f.hero));
    G_UseItem(f.hero, 0);
    T_ASSERT(S_UnitHasStatus(f.hero, FS_SLKKey("Bvul")));
    T_ASSERT(S_UnitInvulnerable(f.hero));
    T_EQ(G_GetItem(potion)->charges, 1);
    T_ASSERT(!S_SpellCooldownReady(f.hero, FS_SLKKey("AIvu")));
    T_ASSERT(!S_SpellAllowsTarget(FS_SLKKey("A001"), f.enemy, f.hero));
    T_ASSERT(!S_SpellDamage(f.hero, f.enemy, 17));
    G_UseItem(f.hero, 0);
    T_EQ(G_GetItem(potion)->charges, 1); /* no consumption on cooldown */
    G_SetUnitFlag(f.hero, UNIT_FLAG_INVULNERABLE, true);
    re_run_frame_at(f.hero, level.time + 2001);
    T_ASSERT(!S_UnitHasStatus(f.hero, FS_SLKKey("Bvul")));
    T_ASSERT(S_UnitInvulnerable(f.hero)); /* independent owner survives */
    G_SetUnitFlag(f.hero, UNIT_FLAG_INVULNERABLE, false);
    T_ASSERT(!S_UnitInvulnerable(f.hero));
    T_ASSERT(S_SpellAllowsTarget(FS_SLKKey("A001"), f.enemy, f.hero));
    re_done(&f);
}

/* Nonstock fire damage and radius catch accidental inheritance of ANpi's
 * 220 radius or organic-only targeting. Dropping the source must stop pulses. */

TEST(retail_item_effect, clfm_item_immolation_pulses_and_stops_on_drop) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"levels\"\nC;Y1;X4;K\"targs\"\n"
        "C;Y1;X5;K\"Area1\"\nC;Y1;X6;K\"DataA1\"\n"
        "C;Y1;X7;K\"BuffID1\"\nC;Y1;X8;K\"DataC1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\n"
        "C;Y2;X3;K1\nC;Y2;X6;K6\nC;Y2;X8;K1\n"
        "C;Y3;X1;K\"AIcf\"\nC;Y3;X2;K\"ANpi\"\n"
        "C;Y3;X3;K1\nC;Y3;X4;K\"ground,enemy,neutral\"\n"
        "C;Y3;X5;K96\nC;Y3;X6;K13\nC;Y3;X7;K\"BIcf\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"clfm\"\nC;Y2;X2;K\"AIcf\"\nE\n";
    retailEffectFixture_t f;
    edict_t *mechanical;
    re_init(&f, ability_slk, items);
    mechanical = alloc_test_unit(FS_SLKKey("hfoo"), 72, 0);
    mechanical->s.player = 1;
    mechanical->svflags |= SVF_MONSTER;
    mechanical->spawn_time = level.time;
    mechanical->targtype = TARG_MECHANICAL;
    mechanical->health.value = mechanical->health.max_value = 500;
    { edict_t *item = re_equip(&f, "clfm", 0); G_GetItem(item)->droppable_set = true; G_GetItem(item)->droppable = true; }
    T_ASSERT(S_UnitHasStatus(f.hero, FS_SLKKey("BIcf")));
    level.time += 1000; S_RunAbilityUpdates(f.hero);
    T_FEQ(f.enemy->health.value, 487.0f, 0.001f);
    T_FEQ(mechanical->health.value, 487.0f, 0.001f);
    T_ASSERT(G_DropItem(f.hero, 0));
    T_ASSERT(!S_UnitHasStatus(f.hero, FS_SLKKey("BIcf")));
    level.time += 1000; S_RunAbilityUpdates(f.hero);
    T_FEQ(f.enemy->health.value, 487.0f, 0.001f);
    T_FEQ(mechanical->health.value, 487.0f, 0.001f);
    re_done(&f);
}

/* AIad is an item alias whose code='aura'; the generic AHad-family parent
 * cannot be used to determine either ownership or the armor bonus. */

TEST(retail_item_effect, lion_horn_provides_data_driven_devotion_aura) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"levels\"\nC;Y1;X4;K\"targs\"\n"
        "C;Y1;X5;K\"Area1\"\nC;Y1;X6;K\"DataA1\"\n"
        "C;Y1;X7;K\"BuffID1\"\nC;Y1;X8;K\"DataC1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\n"
        "C;Y2;X3;K1\nC;Y2;X6;K6\nC;Y2;X8;K1\n"
        "C;Y3;X1;K\"AIad\"\nC;Y3;X2;K\"aura\"\n"
        "C;Y3;X3;K1\nC;Y3;X4;K\"air,ground,friend,self,vuln,invu\"\n"
        "C;Y3;X5;K900\nC;Y3;X6;K2.25\nC;Y3;X7;K\"BHad\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"lhst\"\nC;Y2;X2;K\"AIad\"\nE\n";
    retailEffectFixture_t f;
    edict_t *ally;
    re_init(&f, ability_slk, items);
    ally = alloc_test_unit(FS_SLKKey("hfoo"), 100, 0);
    ally->s.player = f.hero->s.player;
    ally->svflags |= SVF_MONSTER;
    ally->targtype = TARG_GROUND;
    ally->spawn_time = level.time;
    T_FEQ(S_DevotionArmorBonus(ally), 0.0f, 0.001f);
    { edict_t *item = re_equip(&f, "lhst", 0); G_GetItem(item)->droppable_set = true; G_GetItem(item)->droppable = true; }
    T_FEQ(S_DevotionArmorBonus(ally), 2.25f, 0.001f);
    T_ASSERT(G_DropItem(f.hero, 0));
    T_FEQ(S_DevotionArmorBonus(ally), 0.0f, 0.001f);
    re_done(&f);
}

/* Chain dispel is unit-targeted and reads DataC, not the Priest's Area
 * dispel count. A nonstock 2-unit chain must not touch the third status. */

TEST(retail_item_effect, wneu_chain_dispel_is_bounded_by_authored_target_count) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"levels\"\nC;Y1;X4;K\"targs\"\n"
        "C;Y1;X5;K\"Area1\"\nC;Y1;X6;K\"DataC1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K1\nC;Y2;X6;K1\nC;Y2;X7;K6\n"
        "C;Y3;X1;K\"AIdc\"\nC;Y3;X2;K\"AIdc\"\nC;Y3;X3;K1\n"
        "C;Y3;X4;K\"air,ground,enemy,vuln\"\n"
        "C;Y3;X5;K150\nC;Y3;X6;K2\nC;Y3;X7;K0\nC;Y3;X8;K0\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"wneu\"\nC;Y2;X2;K\"AIdc\"\nE\n";
    retailEffectFixture_t f;
    edict_t *second, *third;
    abilityitem_t spell;
    spellTarget_t target;
    abilityCall_t call;
    edict_t *item;
    re_init(&f, ability_slk, items);
    f.hero_abilities.abilList = "AInv,AIdc";
    second = alloc_test_unit(FS_SLKKey("hfoo"), 128, 0);
    third = alloc_test_unit(FS_SLKKey("hfoo"), 192, 0);
    second->s.player = third->s.player = 1;
    second->svflags |= SVF_MONSTER;
    third->svflags |= SVF_MONSTER;
    second->targtype = third->targtype = TARG_GROUND;
    second->spawn_time = third->spawn_time = level.time;
    T_ASSERT(unit_addtimedstatus(f.enemy, "B000", 1, 30));
    T_ASSERT(unit_addtimedstatus(second, "B001", 1, 30));
    T_ASSERT(unit_addtimedstatus(third, "B002", 1, 30));
    item = re_equip(&f, "wneu", 0);
    G_GetItem(item)->droppable_set = true; G_GetItem(item)->droppable = true;
    spell = S_AbilityItem(FS_SLKKey("AIdc"));
    T_FEQ(S_SpellData(FS_SLKKey("AIdc"), 1, 3), 2.0f, 0.001f);
    T_FEQ(S_SpellNumber(FS_SLKKey("AIdc"), ABILITY_NUMBER_AREA, 1), 150.0f, 0.001f);
    target = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = f.enemy);
    call = MAKE(abilityCall_t, .item = &spell, .target = &target);
    T_ASSERT(S_AbilityMessage(f.hero, A_VALIDATE, &call));
    T_ASSERT(S_SpellIsEnemy(f.hero, second));
    T_ASSERT(S_SpellAllowsAreaTarget(FS_SLKKey("AIdc"), f.hero, second));
    T_ASSERT(S_AbilityMessage(f.hero, A_EXECUTE, &call));
    T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey("B000")));
    T_ASSERT(!S_UnitHasStatus(second, FS_SLKKey("B001")));
    T_ASSERT(S_UnitHasStatus(third, FS_SLKKey("B002")));
    T_ASSERT(G_DropItem(f.hero, 0));
    re_done(&f);
}


/* All four shdt abilities are inherited from ItemData, not a special-case
 * inventory item type. The stock values are data assertions; the nonstock
 * fire value proves the shared AIcf handler also owns shield flames. */

TEST(retail_item_effect, shield_of_deathlord_has_four_reversible_item_effects) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y6;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"levels\"\nC;Y1;X4;K\"DataA1\"\n"
        "C;Y1;X5;K\"Area1\"\nC;Y1;X6;K\"targs\"\n"
        "C;Y1;X7;K\"BuffID1\"\nC;Y1;X8;K\"DataC1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\n"
        "C;Y2;X3;K1\nC;Y2;X4;K6\nC;Y2;X8;K1\n"
        "C;Y3;X1;K\"AIlf\"\nC;Y3;X2;K\"AIml\"\nC;Y3;X3;K1\nC;Y3;X4;K150\n"
        "C;Y4;X1;K\"AImz\"\nC;Y4;X2;K\"AImm\"\nC;Y4;X3;K1\nC;Y4;X4;K100\n"
        "C;Y5;X1;K\"AId0\"\nC;Y5;X2;K\"AIde\"\nC;Y5;X3;K1\nC;Y5;X4;K10\n"
        "C;Y6;X1;K\"AIcf\"\nC;Y6;X2;K\"ANpi\"\nC;Y6;X3;K1\n"
        "C;Y6;X4;K7\nC;Y6;X5;K160\n"
        "C;Y6;X6;K\"ground,enemy,neutral\"\nC;Y6;X7;K\"BIcf\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"shdt\"\nC;Y2;X2;K\"AIlf,AImz,AId0,AIcf\"\nE\n";
    retailEffectFixture_t f;
    float defense;
    re_init(&f, ability_slk, items);
    defense = G_UnitArmorValue(f.hero);
    { edict_t *item = re_equip(&f, "shdt", 0); G_GetItem(item)->droppable_set = true; G_GetItem(item)->droppable = true; }
    T_FEQ(f.hero->health.max_value, 650.0f, 0.001f);
    T_FEQ(f.hero->mana.max_value, 600.0f, 0.001f);
    T_FEQ(G_UnitArmorValue(f.hero), defense + 10.0f, 0.001f);
    T_ASSERT(S_UnitHasStatus(f.hero, FS_SLKKey("BIcf")));
    level.time += 1000;
    S_RunAbilityUpdates(f.hero);
    T_FEQ(f.enemy->health.value, 493.0f, 0.001f);
    T_ASSERT(G_DropItem(f.hero, 0));
    T_FEQ(f.hero->health.max_value, 500.0f, 0.001f);
    T_FEQ(f.hero->mana.max_value, 500.0f, 0.001f);
    T_FEQ(G_UnitArmorValue(f.hero), defense, 0.001f);
    T_ASSERT(!S_UnitHasStatus(f.hero, FS_SLKKey("BIcf")));
    re_done(&f);
}

/* Supplied 3.0.1 data uses AIsx=18%, rather than the older web page's 15%.
 * The custom rows independently check authored values and rank selection. */
static char const item_rate_abilities[] =
    "ID;PWXL;N;E\nB;X8;Y11\n"
    "C;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"DataA1\"\n"
    "C;X4;K\"DataC1\"\nC;X5;K\"DataD1\"\nC;X6;K\"DataE1\"\n"
    "C;X7;K\"levels\"\nC;X8;K\"DataA2\"\n"
    "C;X1;Y2;K\"AInv\"\nC;X2;K\"AInv\"\nC;X3;K6\nC;X4;K1\nC;X5;K1\nC;X6;K1\nC;X7;K1\n"
    "C;X1;Y3;K\"A000\"\nC;X2;K\"AInv\"\nC;X3;K6\nC;X4;K0\nC;X5;K1\nC;X6;K1\nC;X7;K1\n"
    "C;X1;Y4;K\"AIms\"\nC;X2;K\"AIms\"\nC;X3;K60\nC;X7;K1\n"
    "C;X1;Y5;K\"B001\"\nC;X2;K\"AIms\"\nC;X3;K37\nC;X7;K2\nC;X8;K90\n"
    "C;X1;Y6;K\"AIsx\"\nC;X2;K\"AIas\"\nC;X3;K0.18\nC;X7;K1\n"
    "C;X1;Y7;K\"G001\"\nC;X2;K\"AIas\"\nC;X3;K0.27\nC;X7;K1\n"
    "C;X1;Y8;K\"AIrm\"\nC;X2;K\"AIrm\"\nC;X3;K0.5\nC;X7;K1\n"
    "C;X1;Y9;K\"AIrn\"\nC;X2;K\"AIrm\"\nC;X3;K0.25\nC;X7;K1\n"
    "C;X1;Y10;K\"M001\"\nC;X2;K\"AIrm\"\nC;X3;K0.35\nC;X7;K1\nE\n";

static char const item_rate_items[] =
    "ID;PWXL;N;E\nB;X3;Y7\n"
    "C;X1;Y1;K\"itemID\"\nC;X2;K\"abilList\"\nC;X3;K\"droppable\"\n"
    "C;X1;Y2;K\"bspd\"\nC;X2;K\"AIms\"\nC;X3;K1\n"
    "C;X1;Y3;K\"gcel\"\nC;X2;K\"AIsx\"\nC;X3;K1\n"
    "C;X1;Y4;K\"rwiz\"\nC;X2;K\"AIrm\"\nC;X3;K1\n"
    "C;X1;Y5;K\"mnst\"\nC;X2;K\"AIrn\"\nC;X3;K1\n"
    "C;X1;Y6;K\"I001\"\nC;X2;K\"B001\"\nC;X3;K1\n"
    "C;X1;Y7;K\"I002\"\nC;X2;K\"G001,M001\"\nC;X3;K1\nE\n";

static void item_rate_init(retailEffectFixture_t *f) {
    re_init(f, item_rate_abilities, item_rate_items);
    f->hero->unitinfo.MoveSpeed = 300.0f;
    f->hero->unit_state->hero.agi = 0;
    f->hero->unit_state->hero.intel = 20;
    f->hero->unit_state->mana_regen_bonus = 0.5f;
    f->hero->unit_state->attack1.cooldown = 1.2f;
    f->hero->unit_state->attack1.damagePoint = 0.2f;
    f->hero->unit_state->attack1.backswingPoint = 0.0f;
    f->hero->collision = 0.0f;
    f->hero->stand = unit_stand;
    f->hero->unitinfo.PropWindow = DEG2RAD(360.0f);
    unit_stand(f->hero);
}

static float item_rate_move_step(edict_t *unit) {
    vec2_t const before = unit->s.origin2;
    vec2_t const destination = {before.x + 512.0f, before.y};
    unit->s.angle = 0.0f;
    T_ASSERT(unit_issueorder(unit, "move", &destination));
    T_NOT_NULL(unit->currentmove);
    T_NOT_NULL(unit->currentmove->think);
    unit->currentmove->think(unit);
    return Vector2_distance(&before, &unit->s.origin2);
}

static float item_rate_mana_tick(edict_t *unit) {
    unit->movetype = MOVETYPE_NONE;
    unit_stand(unit);
    unit->mana.value = 100.0f;
    G_RunEntity(unit);
    return unit->mana.value - 100.0f;
}

TEST(retail_item_effect, item_rate_boots_change_real_move_steps_without_duplicate_stacking) {
    retailEffectFixture_t f;
    float const frame_seconds = 10.0f / FRAMETIME;
    item_rate_init(&f);
    T_FEQ(item_rate_move_step(f.hero), 300.0f * frame_seconds, 0.001f);
    re_equip(&f, "bspd", 0);
    T_FEQ(item_rate_move_step(f.hero), 360.0f * frame_seconds, 0.001f);
    re_equip(&f, "bspd", 1);
    T_FEQ(item_rate_move_step(f.hero), 360.0f * frame_seconds, 0.001f);
    T_ASSERT(G_DropItem(f.hero, 0));
    T_FEQ(item_rate_move_step(f.hero), 360.0f * frame_seconds, 0.001f);
    T_ASSERT(G_DropItem(f.hero, 1));
    T_FEQ(item_rate_move_step(f.hero), 300.0f * frame_seconds, 0.001f);
    re_done(&f);
}

TEST(retail_item_effect, item_rate_gloves_stack_authored_fraction_and_reverse_cooldown) {
    retailEffectFixture_t f;
    item_rate_init(&f);
    re_equip(&f, "gcel", 0);
    attack_melee_cooldown(f.hero);
    T_FEQ(f.hero->wait, 1.0f / 1.18f, 0.001f);
    re_equip(&f, "gcel", 1);
    attack_melee_cooldown(f.hero);
    T_FEQ(f.hero->wait, 1.0f / 1.36f, 0.001f);
    T_ASSERT(G_DropItem(f.hero, 0));
    attack_melee_cooldown(f.hero);
    T_FEQ(f.hero->wait, 1.0f / 1.18f, 0.001f);
    T_ASSERT(G_DropItem(f.hero, 1));
    attack_melee_cooldown(f.hero);
    T_FEQ(f.hero->wait, 1.0f, 0.001f);
    re_done(&f);
}

TEST(retail_item_effect, item_rate_mana_mask_and_stone_scale_intrinsic_regen_and_drop) {
    retailEffectFixture_t f;
    float baseline;
    item_rate_init(&f);
    baseline = item_rate_mana_tick(f.hero);
    T_ASSERT(baseline > 0.0f);
    re_equip(&f, "rwiz", 0);
    T_FEQ(item_rate_mana_tick(f.hero), baseline * 1.5f, 0.00002f);
    re_equip(&f, "mnst", 1);
    T_FEQ(item_rate_mana_tick(f.hero), baseline * 1.75f, 0.00002f);
    T_ASSERT(G_DropItem(f.hero, 0));
    T_FEQ(item_rate_mana_tick(f.hero), baseline * 1.25f, 0.00002f);
    T_ASSERT(G_DropItem(f.hero, 1));
    T_FEQ(item_rate_mana_tick(f.hero), baseline, 0.00002f);
    re_done(&f);
}

TEST(retail_item_effect, item_rate_custom_aliases_obey_inventory_use_permission) {
    retailEffectFixture_t f;
    float baseline;
    item_rate_init(&f);
    baseline = item_rate_mana_tick(f.hero);
    re_equip(&f, "I001", 0);
    re_equip(&f, "I002", 1);
    T_FEQ(unit_movedistance(f.hero), 337.0f * 10.0f / FRAMETIME, 0.001f);
    attack_melee_cooldown(f.hero);
    T_FEQ(f.hero->wait, 1.0f / 1.27f, 0.001f);
    T_FEQ(item_rate_mana_tick(f.hero), baseline * 1.35f, 0.00002f);
    f.hero_abilities.abilList = "A000";
    T_ASSERT(!G_InventoryCanUseItems(f.hero));
    T_FEQ(unit_movedistance(f.hero), 300.0f * 10.0f / FRAMETIME, 0.001f);
    attack_melee_cooldown(f.hero);
    T_FEQ(f.hero->wait, 1.0f, 0.001f);
    T_FEQ(item_rate_mana_tick(f.hero), baseline, 0.00002f);
    re_done(&f);
}

TEST(retail_item_effect, item_rate_direct_ability_levels_do_not_require_inventory_use) {
    retailEffectFixture_t f;
    item_rate_init(&f);
    f.hero_abilities.abilList = "A000";
    T_ASSERT(G_ActorAddSkill(f.hero, FS_SLKKey("B001")));
    T_FEQ(unit_movedistance(f.hero), 337.0f * 10.0f / FRAMETIME, 0.001f);
    T_EQ(G_UnitSetAbilityLevel(f.hero, FS_SLKKey("B001"), 2), 2);
    T_FEQ(unit_movedistance(f.hero), 390.0f * 10.0f / FRAMETIME, 0.001f);
    T_ASSERT(G_ActorRemoveSkill(f.hero, FS_SLKKey("B001")));
    T_FEQ(unit_movedistance(f.hero), 300.0f * 10.0f / FRAMETIME, 0.001f);
    re_done(&f);
}

TEST(retail_item_effect, item_rate_inventory_rederives_after_real_save_load) {
    retailEffectFixture_t f;
    cstring_t path = Test_TempPath("item-rate-save.bin");
    uint32_t hero_index;
    item_rate_init(&f);
    ((mapInfo_t *)level.mapinfo)->fileFormat = 0;
    re_equip(&f, "bspd", 0);
    re_equip(&f, "gcel", 1);
    re_equip(&f, "rwiz", 2);
    hero_index = f.hero->s.number;
    T_ASSERT(WriteGame(path));
    T_ASSERT(ReadGame(path));
    f.hero = g_edicts + hero_index;
    T_NOT_NULL(G_InventoryItem(f.hero, 0));
    T_NOT_NULL(G_InventoryItem(f.hero, 1));
    T_NOT_NULL(G_InventoryItem(f.hero, 2));
    T_FEQ(unit_movedistance(f.hero), 360.0f * 10.0f / FRAMETIME, 0.001f);
    attack_melee_cooldown(f.hero);
    T_FEQ(f.hero->wait, 1.0f / 1.18f, 0.001f);
    T_FEQ(item_rate_mana_tick(f.hero), 1.5f * 1.5f * (FRAMETIME / 1000.0f), 0.00002f);
    T_ASSERT(G_DropItem(f.hero, 0));
    T_ASSERT(G_DropItem(f.hero, 1));
    T_ASSERT(G_DropItem(f.hero, 2));
    T_FEQ(unit_movedistance(f.hero), 300.0f * 10.0f / FRAMETIME, 0.001f);
    attack_melee_cooldown(f.hero);
    T_FEQ(f.hero->wait, 1.0f, 0.001f);
    remove(path);
    re_done(&f);
}


/* Contrast legacy AIos with the authored nested A901.
 * Historical CAbilitySpellEffectBonus separately selects the hero chance. */
static void re_nested_orb_candidate(bool alias, bool hero_zero) {
    char abilities[4096], items[768];
    retailEffectFixture_t f;
    snprintf(abilities, sizeof(abilities),
        "ID;PWXL;N;EBB;Y6;X13\n"
        "C;Y1;X1;K\"alias\"\n"
        "C;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"DataA1\"\n"
        "C;Y1;X5;K\"DataB1\"\n"
        "C;Y1;X6;K\"DataC1\"\n"
        "C;Y1;X7;K\"DataD1\"\n"
        "C;Y1;X8;K\"DataE1\"\n"
        "C;Y1;X9;K\"UnitID1\"\n"
        "C;Y1;X10;K\"Dur1\"\n"
        "C;Y1;X11;K\"HeroDur1\"\n"
        "C;Y1;X12;K\"BuffID1\"\n"
        "C;Y1;X13;K\"targs1\"\n"
        "C;Y2;X1;K\"AInv\"\n"
        "C;Y2;X2;K\"AInv\"\n"
        "C;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"6\"\n"
        "C;Y2;X6;K\"1\"\n"
        "C;Y2;X7;K\"1\"\n"
        "C;Y2;X8;K\"1\"\n"
        "C;Y3;X1;K\"AIsb\"\n"
        "C;Y3;X2;K\"AIsb\"\n"
        "C;Y3;X3;K\"1\"\n"
        "C;Y3;X4;K\"13\"\n"
        "C;Y3;X5;K\"100\"\n"
        "C;Y3;X6;K\"%d\"\n"
        "C;Y3;X7;K\"100\"\n"
        "C;Y3;X8;K\"2\"\n"
        "C;Y3;X9;K\"%s\"\n"
        "C;Y4;X1;K\"A900\"\n"
        "C;Y4;X2;K\"AIsb\"\n"
        "C;Y4;X3;K\"1\"\n"
        "C;Y4;X4;K\"19\"\n"
        "C;Y4;X5;K\"100\"\n"
        "C;Y4;X6;K\"100\"\n"
        "C;Y4;X7;K\"100\"\n"
        "C;Y4;X8;K\"2\"\n"
        "C;Y4;X9;K\"A901\"\n"
        "C;Y5;X1;K\"AIos\"\n"
        "C;Y5;X2;K\"Aslo\"\n"
        "C;Y5;X3;K\"1\"\n"
        "C;Y5;X4;K\".4\"\n"
        "C;Y5;X5;K\".3\"\n"
        "C;Y5;X10;K\"7\"\n"
        "C;Y5;X11;K\"3\"\n"
        "C;Y5;X12;K\"Bslo\"\n"
        "C;Y5;X13;K\"ground,air,enemy\"\n"
        "C;Y6;X1;K\"A901\"\n"
        "C;Y6;X2;K\"Aslo\"\n"
        "C;Y6;X3;K\"1\"\n"
        "C;Y6;X4;K\".67\"\n"
        "C;Y6;X5;K\".43\"\n"
        "C;Y6;X10;K\"13\"\n"
        "C;Y6;X11;K\"6\"\n"
        "C;Y6;X12;K\"B901\"\n"
        "C;Y6;X13;K\"ground,air,enemy\"\n"
        "E\n", hero_zero ? 0 : 100, hero_zero ? "AIos" : "A901");
    snprintf(items, sizeof(items),
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\n"
        "C;Y1;X2;K\"abilList\"\n"
        "C;Y1;X3;K\"droppable\"\n"
        "C;Y2;X1;K\"oslo\"\n"
        "C;Y2;X2;K\"%s\"\n"
        "C;Y2;X3;K\"true\"\n"
        "E\n",
        alias ? "A900" : "AIsb");
    re_init(&f, abilities, items);
    if (hero_zero) {
        f.enemy->class_id = FS_SLKKey("Hpal");
        f.enemy->data.UnitBalance = f.hero->data.UnitBalance;
        f.enemy->unit_state->hero.level = 1;
        T_ASSERT(G_UnitIsHero(f.enemy));
    }
    re_equip(&f, "oslo", 0);
    T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, alias ? 19 : 13, 0.001f);
    S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_ASSERT(f.enemy->health.value < 500);
    if (hero_zero) {
        T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey("Bslo")));
        T_FEQ(unit_status_modifier_total(f.enemy, WC3_STATUS_MOD_MOVE_SPEED_PERCENT), 0, 0.001f);
    } else {
        T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("B901")));
        T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey("Bslo")));
        T_FEQ(unit_status_modifier_total(f.enemy, WC3_STATUS_MOD_MOVE_SPEED_PERCENT), -0.67f, 0.001f);
        T_FEQ(unit_status_modifier_total(f.enemy, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT), -0.43f, 0.001f);
    }
    re_done(&f);
}
TEST(retail_item_effect, nested_orb_authored_unit_id) { re_nested_orb_candidate(false, false); }
TEST(retail_item_effect, nested_orb_alias_unit_id) { re_nested_orb_candidate(true, false); }
TEST(retail_item_effect, nested_orb_independent_hero_chance) { re_nested_orb_candidate(false, true); }


/* Both base rows are present, as in installed data. In particular, a zero
 * chance AIll must not acquire the separate AIsb row's guaranteed Slow. */
static void re_nested_orb_matrix(nestedMode_t mode) {
    char abilities[10000], items[700];
    retailEffectFixture_t f;
    bool const lightning = mode == NESTED_LIGHTNING_ZERO || mode == NESTED_LIGHTNING_ONCE;
    bool const hero = mode == NESTED_HERO_SUCCESS || mode == NESTED_SUMMON_HERO;
    bool const summon = mode == NESTED_SUMMON_ZERO || mode == NESTED_SUMMON_SUCCESS ||
                        mode == NESTED_SUMMON_HERO || mode == NESTED_LIGHTNING_ONCE;
    bool const blocked = mode == NESTED_ZERO || mode == NESTED_SUMMON_ZERO ||
        mode == NESTED_SUMMON_HERO || mode == NESTED_DROP || mode == NESTED_INNER_AIR ||
        mode == NESTED_MANUAL || mode == NESTED_FROST_PRIORITY || mode == NESTED_UNSUPPORTED ||
        mode == NESTED_CYCLE || mode == NESTED_LIGHTNING_ZERO || mode == NESTED_CARRY;
    int const ordinary_chance = mode == NESTED_ZERO || mode == NESTED_HERO_SUCCESS ||
        mode == NESTED_SUMMON_SUCCESS || mode == NESTED_NATIVE_RANK ? 0 : 100;
    int const hero_chance = mode == NESTED_SUMMON_HERO ? 0 : 100;
    int const summon_chance = mode == NESTED_SUMMON_ZERO ? 0 : 100;
    cstring_t nested = mode == NESTED_UNSUPPORTED ? "ZZZZ" : mode == NESTED_CYCLE ? "A900" :
        mode == NESTED_NATIVE_RANK ? "AIos" : "A901";
    cstring_t buff = lightning ? "Bprg" : mode == NESTED_LEGACY ? "Bslo" : "B901";
    uint32_t target_number;
    float baseline_move;

    snprintf(abilities, sizeof(abilities), mode == NESTED_LEGACY ? nested_legacy_slk : nested_modern_slk,
        ordinary_chance, hero_chance, summon_chance, nested,
        mode == NESTED_INNER_AIR ? "ground,enemy" : "ground,air,enemy",
        mode == NESTED_LIGHTNING_ZERO ? 0 : 100);
    snprintf(items, sizeof(items), "ID;PWXL;N;EBB;Y3;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\n"
        "C;Y2;X1;K\"oslo\"\nC;Y2;X2;K\"%s\"\nC;Y2;X3;K\"true\"\n"
        "C;Y3;X1;K\"ofro\"\nC;Y3;X2;K\"AIob\"\nC;Y3;X3;K\"true\"\nE\n",
        lightning ? "AIll" : "A900");
    re_init(&f, abilities, items);
    f.hero->unit_state->hero.level = 1;
    f.hero->stand = unit_stand; f.hero->die = unit_die;
    f.enemy->unitinfo.MoveSpeed = 300;
    f.enemy->stand = unit_stand; f.enemy->die = unit_die;
    if (hero) {
        f.enemy->class_id = FS_SLKKey("Hpal");
        f.enemy->data.UnitBalance = f.hero->data.UnitBalance;
        f.enemy->unit_state->hero.level = 1;
        T_ASSERT(G_UnitIsHero(f.enemy));
    }
    if (summon) { f.enemy->summon_ability = FS_SLKKey("Awat"); f.enemy->owner = f.hero; }
    if (mode == NESTED_NATIVE_RANK) {
        f.hero_abilities.abilList = "AInv,A900";
        T_EQ(G_UnitSetAbilityLevel(f.hero, FS_SLKKey("A900"), 2), 2);
    } else {
        re_equip(&f, "oslo", mode == NESTED_FROST_PRIORITY ? 1 : 0);
        if (mode == NESTED_FROST_PRIORITY) re_equip(&f, "ofro", 0);
    }
    if (mode == NESTED_DROP) T_ASSERT(G_DropItem(f.hero, 0));
    if (mode == NESTED_CARRY) {
        f.hero_abilities.abilList = "AIv0";
        T_ASSERT(!G_InventoryCanUseItems(f.hero));
    }
    if (mode == NESTED_INNER_AIR || mode == NESTED_OUTER_AIR) f.enemy->targtype = TARG_AIR;
    baseline_move = unit_movedistance(f.enemy);
    T_ASSERT(baseline_move > 0);
    if (mode == NESTED_MANUAL)
        S_ResolveAttackHitWithArrow(f.hero, f.enemy, 20, FS_SLKKey("AHfa"), 1);
    else S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_ASSERT(f.enemy->health.value < 500);
    if (blocked) {
        T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey(buff)));
        if (lightning) T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey("Bslo")));
        re_done(&f); return;
    }
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey(buff)));
    if (mode == NESTED_LIGHTNING_ONCE) {
        T_FEQ(f.enemy->health.value, 330, 0.001f);
        T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey("Bslo")));
        re_done(&f); return;
    }
    T_FEQ(unit_movedistance(f.enemy), baseline_move * 0.33f, 0.001f);
    T_FEQ(unit_status_modifier_total(f.enemy, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT), -0.43f, 0.001f);
    if (mode == NESTED_SAVE) {
        cstring_t filename = Test_TempPath("nested-orb-custom-slow.bin");
        target_number = f.enemy->s.number;
        T_ASSERT(WriteGame(filename)); T_ASSERT(ReadGame(filename));
        f.enemy = g_edicts + target_number;
        T_FEQ(unit_status_modifier_total(f.enemy, WC3_STATUS_MOD_MOVE_SPEED_PERCENT), -0.67f, 0.001f);
        T_ASSERT(unit_findstatus(f.enemy, FS_SLKKey(buff))->buff_flags & WC3_STATUS_BUFF_MAGICAL);
        remove(filename);
    }
    if (mode == NESTED_DISPEL || mode == NESTED_SAVE) T_EQ(S_DispelUnitMagic(f.enemy), 1);
    else re_run_frame_at(f.enemy, level.time + (hero ? 6000 : 13000));
    T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey(buff)));
    T_FEQ(unit_status_modifier_total(f.enemy, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT), 0, 0.001f);
    if (mode != NESTED_SAVE) T_FEQ(unit_movedistance(f.enemy), baseline_move, 0.001f);
    re_done(&f);
}
TEST(retail_item_effect, nested_orb_zero_chance) { re_nested_orb_matrix(NESTED_ZERO); }
TEST(retail_item_effect, nested_orb_hero_uses_data_c) { re_nested_orb_matrix(NESTED_HERO_SUCCESS); }
TEST(retail_item_effect, nested_orb_summon_zero_data_d) { re_nested_orb_matrix(NESTED_SUMMON_ZERO); }
TEST(retail_item_effect, nested_orb_summon_uses_data_d) { re_nested_orb_matrix(NESTED_SUMMON_SUCCESS); }
TEST(retail_item_effect, nested_orb_hero_precedes_summon) { re_nested_orb_matrix(NESTED_SUMMON_HERO); }
TEST(retail_item_effect, nested_orb_native_rank_uses_unit_id_2) { re_nested_orb_matrix(NESTED_NATIVE_RANK); }
TEST(retail_item_effect, nested_orb_drop_disables_new_proc) { re_nested_orb_matrix(NESTED_DROP); }
TEST(retail_item_effect, nested_orb_custom_expiry_restores_motion) { re_nested_orb_matrix(NESTED_EXPIRE); }
TEST(retail_item_effect, nested_orb_custom_magic_dispel_restores_motion) { re_nested_orb_matrix(NESTED_DISPEL); }
TEST(retail_item_effect, nested_orb_custom_save_preserves_removal) { re_nested_orb_matrix(NESTED_SAVE); }
TEST(retail_item_effect, nested_orb_air_target_uses_authored_mask) { re_nested_orb_matrix(NESTED_OUTER_AIR); }
TEST(retail_item_effect, nested_orb_nested_mask_rejects_air) { re_nested_orb_matrix(NESTED_INNER_AIR); }
TEST(retail_item_effect, nested_orb_manual_arrow_suppresses_proc) { re_nested_orb_matrix(NESTED_MANUAL); }
TEST(retail_item_effect, nested_orb_higher_frost_suppresses_proc) { re_nested_orb_matrix(NESTED_FROST_PRIORITY); }
TEST(retail_item_effect, nested_orb_unknown_nested_has_no_legacy_substitute) {
    re_nested_orb_matrix(NESTED_UNSUPPORTED);
}
TEST(retail_item_effect, nested_orb_cyclic_passive_is_not_a_spell) { re_nested_orb_matrix(NESTED_CYCLE); }
TEST(retail_item_effect, nested_orb_roc_columns_and_omitted_buff) { re_nested_orb_matrix(NESTED_LEGACY); }
TEST(retail_item_effect, nested_orb_lightning_zero_ignores_other_base_row) {
    re_nested_orb_matrix(NESTED_LIGHTNING_ZERO);
}
TEST(retail_item_effect, nested_orb_lightning_summon_damage_once) { re_nested_orb_matrix(NESTED_LIGHTNING_ONCE); }
TEST(retail_item_effect, nested_orb_carry_only_disables_proc) { re_nested_orb_matrix(NESTED_CARRY); }


/* Original SpellEffectBonus requires nested eligibility flag 4. Blizzard
 * returns 0x1100002; Purge 0x108004 and Slow 0x100004. A point-only cast
 * must not receive the unit-target payload from a weapon hit. */
TEST(retail_item_effect, nested_orb_rejects_point_only_nested_spell) {
    char abilities[10000];
    retailEffectFixture_t f;
    uint32_t entities_before;
    snprintf(abilities, sizeof(abilities), nested_modern_slk,
        100, 100, 100, "AHbz", "ground,air,enemy", 100);
    re_init(&f, abilities, nested_hit_items);
    re_equip(&f, "oslo", 0);
    T_EQ(S_SpellTargetTypeForCode(S_AbilityItem(FS_SLKKey("AHbz")).ability, FS_SLKKey("AHbz")),
         SPELL_TARGET_POINT);
    entities_before = globals.num_edicts;
    S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_FEQ(f.enemy->health.value, 480, 0.001f);
    T_EQ(globals.num_edicts, entities_before);
    T_ASSERT(!S_SpellIsChanneling(f.hero));
    re_done(&f);
}

TEST(retail_item_effect, nested_orb_requires_unit_hit_payload) {
    char abilities[10000];
    retailEffectFixture_t f;
    abilityitem_t ability;
    spellTarget_t target;
    abilityCall_t call;
    snprintf(abilities, sizeof(abilities), nested_modern_slk,
        100, 100, 100, "AIos", "ground,air,enemy", 100);
    re_init(&f, abilities, nested_hit_items);
    ability = S_AbilityItem(FS_SLKKey("A900"));
    /* Retain a valid pointer as a sentinel: the inactive union member must
     * never be read for a payload tagged as a point. */
    target = MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .entity = f.enemy);
    call = MAKE(abilityCall_t, .item = &ability, .target = &target);
    T_ASSERT(!S_AbilityMessage(f.hero, A_EXECUTE, &call));
    T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey("Bslo")));
    re_done(&f);
}

#endif /* BZ_TESTS */
