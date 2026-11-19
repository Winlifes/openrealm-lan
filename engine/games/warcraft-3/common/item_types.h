#ifndef WC3_ITEM_TYPES_H
#define WC3_ITEM_TYPES_H

/* UI\UnitEditorData.txt enum values, paired with common.j's typed converters.
 * Any is a selector constant, not an authored ItemData value. */
static cstring_t const item_equipment_names[] = {
    "None", "Head", "Chest", "Gloves", "Boots", "Ring", "Primary", "Offhand", "Trinket"
};
static cstring_t const item_tag_names[] = {
    "Undefined", "Droppable", "Quest Reward", "Boss Drop", "Secret", "Puzzle", "World", "Shop"
};

#endif
