/* Versioned item classifications and authored equipment metadata. */
#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "jass/jass.h"

void setup_test_world(void);
bool run_test_jass(cstring_t source);
bool run_test_jass_error(cstring_t source, cstring_t expected);
slkTestData_t *parse_slk_string(cstring_t source);
void free_slk_rows(slkTestData_t *rows);

static slkTestData_t *classification_fixture(bool tft) {
    char source[2048];
    snprintf(source, sizeof(source),
        "ID;PWXL;N;E\nB;X7;Y4;D0\n"
        "C;X1;Y1;K\"itemID\"\nC;X2;K\"%s\"\nC;X3;K\"file\"\n"
        "C;X4;K\"Level\"\nC;X5;K\"pickRandom\"\n"
        "C;X6;K\"equipment\"\nC;X7;K\"tag\"\n"
        "C;X1;Y2;K\"ic00\"\nC;X2;K\"Permanent\"\n"
        "C;X3;K\"TestUI\\Models\\anim_pulse.mdx\"\n"
        "C;X4;K41\nC;X5;K1\nC;X6;K\"None\"\nC;X7;K\"Undefined\"\n"
        "C;X1;Y3;K\"ic01\"\nC;X2;K\"Equipment\"\n"
        "C;X3;K\"TestUI\\Models\\anim_pulse.mdx\"\n"
        "C;X4;K47\nC;X5;K1\nC;X6;K\"Ring\"\nC;X7;K\"Shop\"\n"
        "C;X1;Y4;K\"ic02\"\nC;X2;K\"Unknown\"\n"
        "C;X3;K\"TestUI\\Models\\anim_pulse.mdx\"\n"
        "C;X4;K53\nC;X5;K1\nC;X6;K\"None\"\nC;X7;K\"Undefined\"\nE\n",
        tft ? "class" : "itemClass");
    return parse_slk_string(source);
}

TEST(wc3_items, classification_legacy_any_and_unknown_are_separate) {
    for (uint32_t schema = 0; schema < 2; schema++) {
        setup_test_world();
        slkTestData_t *rows = classification_fixture(schema != 0), *old = G_SetSLKRows("ItemData", rows);
        T_ASSERT(run_test_jass(
            "native ChooseRandomItem takes integer level returns integer\n"
            "native ChooseRandomItemEx takes itemtype kind, integer level returns integer\n"
            "globals\n"
            " constant itemtype ITEM_TYPE_UNKNOWN = ConvertItemType(7)\n"
            " constant itemtype ITEM_TYPE_ANY = ConvertItemType(8)\n"
            "endglobals\n"
            "function main takes nothing returns nothing\n"
            " local item known = CreateItem('ic00', 64., 64.)\n"
            " local item unknown = CreateItem('ic02', 96., 64.)\n"
            " call BJassAssert(GetItemType(known) == ITEM_TYPE_PERMANENT, \"legacy permanent changed\")\n"
            " call BJassAssert(GetItemType(unknown) == ITEM_TYPE_UNKNOWN, \"legacy unknown changed\")\n"
            " call BJassAssert(ChooseRandomItem(41) == 'ic00', \"legacy unfiltered random failed\")\n"
            " call BJassAssert(ChooseRandomItemEx(ITEM_TYPE_ANY, 41) == 'ic00', \"legacy any failed\")\n"
            " call BJassAssert(ChooseRandomItemEx(ITEM_TYPE_UNKNOWN, 41) == 0, \"legacy unknown treated as any\")\n"
            " call SetPlayerState(Player(0), ConvertPlayerState(1), 17)\n"
            "endfunction\n"));
        T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 17);
        G_SetSLKRows("ItemData", old); free_slk_rows(rows);
    }
}

TEST(wc3_items, classification_modern_equipment_and_unknown_are_separate) {
    for (uint32_t schema = 0; schema < 2; schema++) {
        setup_test_world();
        slkTestData_t *rows = classification_fixture(schema != 0), *old = G_SetSLKRows("ItemData", rows);
        T_ASSERT(run_test_jass(
            "native ChooseRandomItem takes integer level returns integer\n"
            "native ChooseRandomItemEx takes itemtype kind, integer level returns integer\n"
            "globals\n"
            " constant itemtype ITEM_TYPE_EQUIPMENT = ConvertItemType(7)\n"
            " constant itemtype ITEM_TYPE_UNKNOWN = ConvertItemType(8)\n"
            " constant itemtype ITEM_TYPE_ANY = ConvertItemType(9)\n"
            "endglobals\n"
            "function main takes nothing returns nothing\n"
            " local item gear = CreateItem('ic01', 64., 64.)\n"
            " local item unknown = CreateItem('ic02', 96., 64.)\n"
            " call BJassAssert(GetItemType(gear) == ITEM_TYPE_EQUIPMENT, \"equipment classified as unknown\")\n"
            " call BJassAssert(GetItemType(unknown) == ITEM_TYPE_UNKNOWN, \"modern unknown has legacy ID\")\n"
            " call BJassAssert(ChooseRandomItem(41) == 'ic00', \"modern unfiltered random failed\")\n"
            " call BJassAssert(ChooseRandomItemEx(ITEM_TYPE_ANY, 41) == 'ic00', \"modern any has legacy ID\")\n"
            " call BJassAssert(ChooseRandomItemEx(ITEM_TYPE_EQUIPMENT, 47) == 'ic01', \"equipment random failed\")\n"
            " call BJassAssert(ChooseRandomItemEx(ITEM_TYPE_UNKNOWN, 41) == 0, \"modern unknown treated as any\")\n"
            " call BJassAssert(ChooseRandomItemEx(ITEM_TYPE_UNKNOWN, 53) == 'ic02', \"unknown random failed\")\n"
            " call SetPlayerState(Player(0), ConvertPlayerState(1), 19)\n"
            "endfunction\n"));
        T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 19);
        G_SetSLKRows("ItemData", old); free_slk_rows(rows);
    }
}

TEST(wc3_items, classification_modern_any_selects_authored_candidates) {
    setup_test_world();
    slkTestData_t *rows = classification_fixture(true), *old = G_SetSLKRows("ItemData", rows);
    T_ASSERT(run_test_jass(
        "native ChooseRandomItemEx takes itemtype kind, integer level returns integer\n"
        "globals\n constant itemtype ITEM_TYPE_ANY = ConvertItemType(9)\nendglobals\n"
        "function main takes nothing returns nothing\n"
        " call BJassAssert(ChooseRandomItemEx(ITEM_TYPE_ANY, 41) == 'ic00', \"modern any excludes valid candidates\")\n"
        " call BJassAssert(ChooseRandomItemEx(ITEM_TYPE_ANY, 47) == 'ic01', \"modern any excludes equipment\")\n"
        " call BJassAssert(ChooseRandomItemEx(ITEM_TYPE_ANY, 500) == 0, \"empty pool invented an item\")\n"
        " call SetPlayerState(Player(0), ConvertPlayerState(1), 29)\n"
        "endfunction\n"));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 29);
    G_SetSLKRows("ItemData", old); free_slk_rows(rows);
}

TEST(wc3_items, classification_equipment_metadata_reads_authored_stock_rows) {
    setup_test_world();
    slkTestData_t *rows = classification_fixture(false), *old = G_SetSLKRows("ItemData", rows);
    T_ASSERT(run_test_jass(
        "type equipmentType extends handle\ntype itemTag extends handle\n"
        "native ConvertEquipmentType takes integer n returns equipmentType\n"
        "native ConvertItemTag takes integer n returns itemTag\n"
        "native GetItemEquipmentType takes item i returns equipmentType\n"
        "native BlzGetItemEquipmentType takes item i returns equipmentType\n"
        "native GetItemTag takes item i returns itemTag\n"
        "native BlzGetItemTag takes item i returns itemTag\n"
        "function main takes nothing returns nothing\n"
        " local item ordinary = CreateItem('ic00', 64., 64.)\n"
        " local item gear = CreateItem('ic01', 96., 64.)\n"
        " call BJassAssert(GetItemEquipmentType(ordinary) == ConvertEquipmentType(0), \"ordinary equipment type changed\")\n"
        " call BJassAssert(GetItemTag(ordinary) == ConvertItemTag(0), \"ordinary tag changed\")\n"
        " call BJassAssert(GetItemEquipmentType(gear) == ConvertEquipmentType(5), \"authored ring type lost\")\n"
        " call BJassAssert(BlzGetItemEquipmentType(gear) == ConvertEquipmentType(5), \"Blz authored ring type lost\")\n"
        " call BJassAssert(GetItemTag(gear) == ConvertItemTag(7), \"authored shop tag lost\")\n"
        " call BJassAssert(BlzGetItemTag(gear) == ConvertItemTag(7), \"Blz authored shop tag lost\")\n"
        " call SetPlayerState(Player(0), ConvertPlayerState(1), 31)\n"
        "endfunction\n"));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 31);
    G_SetSLKRows("ItemData", old); free_slk_rows(rows);
}

TEST(wc3_items, classification_equipment_metadata_uses_slk_and_map_inheritance) {
    unitModification_t mods[] = {
        { .modID = MAKEFOURCC('i','e','q','u'), .type = mod_string, .data = "Head" },
        { .modID = MAKEFOURCC('i','t','a','g'), .type = mod_string, .data = "Boss Drop" },
    };
    unitData_t original = { .originalUnitID = MAKEFOURCC('i','c','0','1'), .numbeOfModifications = 2, .modifications = mods };
    unitData_t custom = { .originalUnitID = MAKEFOURCC('i','c','0','1'), .newUnitID = MAKEFOURCC('i','c','0','3') };
    mapInfo_t map = { .num_originalItems = 1, .originalItems = &original, .num_userCreatedItems = 1, .userCreatedItems = &custom };
    for (uint32_t schema = 0; schema < 2; schema++) {
        setup_test_world();
        slkTestData_t *rows = classification_fixture(schema != 0), *old = G_SetSLKRows("ItemData", rows);
        mapInfo_t const *old_map = level.mapinfo;
        level.mapinfo = &map; G_SetMapUnitOverrides(&map);
        T_ASSERT(run_test_jass(
            "type equipmentType extends handle\ntype itemTag extends handle\n"
            "native ConvertEquipmentType takes integer n returns equipmentType\n"
            "native ConvertItemTag takes integer n returns itemTag\n"
            "native GetItemEquipmentType takes item i returns equipmentType\n"
            "native BlzGetItemEquipmentType takes item i returns equipmentType\n"
            "native GetItemTag takes item i returns itemTag\n"
            "native BlzGetItemTag takes item i returns itemTag\n"
            "function main takes nothing returns nothing\n"
            " local item ordinary = CreateItem('ic00', 64., 64.)\n"
            " local item base = CreateItem('ic01', 96., 64.)\n"
            " local item custom = CreateItem('ic03', 128., 64.)\n"
            " call BJassAssert(GetItemEquipmentType(ordinary) == ConvertEquipmentType(0), \"ordinary item acquired equipment type\")\n"
            " call BJassAssert(GetItemTag(ordinary) == ConvertItemTag(0), \"ordinary item acquired tag\")\n"
            " call BJassAssert(GetItemEquipmentType(base) == ConvertEquipmentType(1), \"base equipment override missing\")\n"
            " call BJassAssert(BlzGetItemTag(base) == ConvertItemTag(3), \"base tag override missing\")\n"
            " call BJassAssert(BlzGetItemEquipmentType(custom) == ConvertEquipmentType(1), \"custom equipment inheritance missing\")\n"
            " call BJassAssert(GetItemTag(custom) == ConvertItemTag(3), \"custom tag inheritance missing\")\n"
            " call SetPlayerState(Player(0), ConvertPlayerState(1), 23)\n"
            "endfunction\n"));
        T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 23);
        G_SetMapUnitOverrides(NULL); level.mapinfo = old_map;
        G_SetSLKRows("ItemData", old); free_slk_rows(rows);
    }
}

TEST(wc3_items, classification_modern_values_and_metadata_survive_fresh_vm_save) {
    char const source[] =
        "type equipmentType extends handle\ntype itemTag extends handle\n"
        "native ConvertEquipmentType takes integer n returns equipmentType\n"
        "native ConvertItemTag takes integer n returns itemTag\n"
        "native GetItemEquipmentType takes item i returns equipmentType\n"
        "native GetItemTag takes item i returns itemTag\n"
        "native ChooseRandomItemEx takes itemtype kind, integer level returns integer\n"
        "globals\n"
        " constant itemtype ITEM_TYPE_EQUIPMENT = ConvertItemType(7)\n"
        " constant itemtype ITEM_TYPE_UNKNOWN = ConvertItemType(8)\n"
        " constant itemtype ITEM_TYPE_ANY = ConvertItemType(9)\n"
        " item gear = null\nitemtype savedKind = null\nitemtype savedAny = null\n"
        " equipmentType savedSlot = null\nitemTag savedTag = null\nendglobals\n"
        "function main takes nothing returns nothing\n"
        " set gear = CreateItem('ic01', 64., 64.)\n"
        " set savedKind = GetItemType(gear)\nset savedAny = ITEM_TYPE_ANY\n"
        " set savedSlot = GetItemEquipmentType(gear)\nset savedTag = GetItemTag(gear)\n"
        "endfunction\n"
        "function afterLoad takes nothing returns nothing\n"
        " call BJassAssert(savedKind == ITEM_TYPE_EQUIPMENT, \"saved equipment class lost\")\n"
        " call BJassAssert(GetItemType(gear) == savedKind, \"restored entity class differs\")\n"
        " call BJassAssert(savedAny == ITEM_TYPE_ANY, \"saved modern any lost\")\n"
        " call BJassAssert(ChooseRandomItemEx(savedAny, 41) == 'ic00', \"restored modern any failed\")\n"
        " call BJassAssert(savedSlot == ConvertEquipmentType(5), \"saved equipment type lost\")\n"
        " call BJassAssert(GetItemEquipmentType(gear) == savedSlot, \"restored metadata binding lost\")\n"
        " call BJassAssert(savedTag == ConvertItemTag(7), \"saved tag lost\")\n"
        " call BJassAssert(GetItemTag(gear) == savedTag, \"restored tag binding lost\")\n"
        " call SetPlayerState(Player(0), ConvertPlayerState(1), 43)\nendfunction\n";
    setup_test_world();
    slkTestData_t *rows = classification_fixture(true), *old = G_SetSLKRows("ItemData", rows);
    char const *path = Test_TempPath("modern-item-classification.sav");
    T_ASSERT(run_test_jass(source));
    T_ASSERT(WriteGame(path));
    T_ASSERT(run_test_jass(source));
    T_ASSERT(ReadGame(path));
    jass_callbyname(level.vm, "afterLoad", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 43);
    G_SetSLKRows("ItemData", old); free_slk_rows(rows);
}

TEST(wc3_items, classification_invalid_equipment_metadata_is_not_silently_none) {
    setup_test_world();
    unitModification_t mod = { .modID = MAKEFOURCC('i','e','q','u'), .type = mod_string, .data = "Unrecognized Slot" };
    unitData_t item = { .originalUnitID = MAKEFOURCC('s','p','r','o'), .numbeOfModifications = 1, .modifications = &mod };
    mapInfo_t map = { .num_originalItems = 1, .originalItems = &item };
    mapInfo_t const *old_map = level.mapinfo;
    level.mapinfo = &map; G_SetMapUnitOverrides(&map);
    T_ASSERT(run_test_jass_error(
        "type equipmentType extends handle\n"
        "native GetItemEquipmentType takes item i returns equipmentType\n"
        "function main takes nothing returns nothing\n"
        " local equipmentType result = GetItemEquipmentType(CreateItem('spro',64.,64.))\n"
        "endfunction\n", "GetItemEquipmentType: unrecognized authored equipment type"));
    G_SetMapUnitOverrides(NULL); level.mapinfo = old_map;
}
#endif
