#include "g_local.h"
#include <ctype.h>

/* AbilityMetaData.slk: these six profile fields have distinct object-data IDs.
 * Research strings are scalar; the other four fields are per-rank lists. */
static struct {
    cstring_t profile;
    uint32_t object_field;
    bool repeated;
} const ability_text_fields[ABILITY_TEXT_COUNT] = {
    { "Tip", MAKEFOURCC('a','t','p','1'), true },
    { "Untip", MAKEFOURCC('a','u','t','1'), true },
    { "Ubertip", MAKEFOURCC('a','u','b','1'), true },
    { "Unubertip", MAKEFOURCC('a','u','u','1'), true },
    { "Researchtip", MAKEFOURCC('a','r','e','t'), false },
    { "Researchubertip", MAKEFOURCC('a','r','u','t'), false },
};

static string_t AbilityTextCopy(cstring_t text, size_t length) {
    string_t result = gi.MemAlloc(length + 1);
    if (result) { memcpy(result, text, length); result[length] = '\0'; }
    return result;
}

static bool AbilityTextKeyValid(uint32_t ability, abilityTextField_t field, int32_t rank, int32_t player) {
    AbilityData_t const *row = G_AbilityData(ability);
    return ability && (uint32_t)field < ABILITY_TEXT_COUNT && player >= -1 && player < MAX_PLAYERS &&
        rank >= 0 && row->id == ability && rank < MAX(1, row->levels);
}

static unitData_t const *AbilityTextMapRecord(uint32_t ability) {
    mapInfo_t const *info = level.mapinfo;
    if (!info) return NULL;
    FOR_LOOP(i, info->num_userCreatedAbilities)
        if (info->userCreatedAbilities[i].newUnitID == ability) return info->userCreatedAbilities + i;
    FOR_LOOP(i, info->num_originalAbilities)
        if (info->originalAbilities[i].originalUnitID == ability) return info->originalAbilities + i;
    return NULL;
}

/* Select quoted CSV without the shared, bounded parse_segment buffer. Tooltips
 * can contain commas and exceed a command-card label's display capacity. */
static string_t AbilityTextProfileCopy(cstring_t text, uint32_t rank) {
    if (!text) return NULL;
    for (;;) {
        cstring_t start = text, end, next;
        while (*start && isspace((unsigned char)*start)) start++;
        if (*start == '"') {
            start++;
            end = strchr(start, '"');
            if (!end) end = start + strlen(start);
            next = *end ? strchr(end + 1, ',') : NULL;
        } else {
            end = strchr(start, ',');
            if (!end) end = start + strlen(start);
            next = *end ? end : NULL;
        }
        if (!rank || !next) return AbilityTextCopy(start, (size_t)(end - start));
        rank--;
        text = next + 1;
    }
}

string_t G_CopyAbilityTooltipText(uint32_t ability, abilityTextField_t field, int32_t rank, int32_t player) {
    abilityTextOverride_t const *global = NULL;
    if (!AbilityTextKeyValid(ability, field, rank, player)) return NULL;
    FOR_LOOP(i, level.ability_text_count) {
        abilityTextOverride_t const *entry = level.ability_text + i;
        if (entry->ability != ability || entry->field != (uint32_t)field || entry->rank != (uint32_t)rank) continue;
        if (player >= 0 && entry->player == player) return AbilityTextCopy(entry->text, strlen(entry->text));
        if (entry->player == -1) global = entry;
    }
    if (global) return AbilityTextCopy(global->text, strlen(global->text));
    FOR_LOOP(depth, 16) {
        unitData_t const *record = AbilityTextMapRecord(ability);
        if (record) FOR_LOOP(m, record->numbeOfModifications) {
            unitModification_t const *mod = record->modifications + m;
            uint32_t const object_rank = ability_text_fields[field].repeated ? (uint32_t)rank + 1 : 0;
            if (mod->modID == ability_text_fields[field].object_field && mod->level == object_rank &&
                mod->type == mod_string && mod->data) {
                cstring_t value = G_LevelString((cstring_t)mod->data);
                return AbilityTextCopy(value, strlen(value));
            }
        }
        char code[5] = {0};
        memcpy(code, &ability, 4);
        cstring_t value = FindConfigValue(code, ability_text_fields[field].profile);
        if (value) {
            string_t selected = AbilityTextProfileCopy(value, ability_text_fields[field].repeated ? (uint32_t)rank : 0);
            if (!selected) return NULL;
            value = G_LevelString(selected);
            if (value == selected) return selected;
            string_t localized = AbilityTextCopy(value, strlen(value));
            gi.MemFree(selected);
            return localized;
        }
        if (!record || !record->newUnitID || record->originalUnitID == ability) return NULL;
        ability = record->originalUnitID;
    }
    return NULL;
}

void G_ClearAbilityTooltipText(void) {
    FOR_LOOP(i, level.ability_text_count) gi.MemFree(level.ability_text[i].text);
    if (level.ability_text) gi.MemFree(level.ability_text);
    level.ability_text = NULL;
    level.ability_text_count = 0;
}

bool G_SetAbilityTooltipText(uint32_t ability, abilityTextField_t field, int32_t rank, int32_t player, cstring_t text) {
    if (!text || !AbilityTextKeyValid(ability, field, rank, player)) return false;
    string_t copy = AbilityTextCopy(text, strlen(text));
    if (!copy) return false;
    uint32_t slot = level.ability_text_count;
    FOR_LOOP(i, level.ability_text_count) {
        abilityTextOverride_t *entry = level.ability_text + i;
        if (entry->ability == ability && entry->field == (uint32_t)field &&
            entry->rank == (uint32_t)rank && entry->player == player) { slot = i; break; }
    }
    if (slot == level.ability_text_count) {
        if (slot >= UINT32_MAX / sizeof(abilityTextOverride_t) - 1) { gi.MemFree(copy); return false; }
        abilityTextOverride_t *grown = gi.MemAlloc((slot + 1) * sizeof(*grown));
        if (!grown) { gi.MemFree(copy); return false; }
        if (slot) memcpy(grown, level.ability_text, slot * sizeof(*grown));
        if (level.ability_text) gi.MemFree(level.ability_text);
        level.ability_text = grown;
        level.ability_text_count++;
    } else gi.MemFree(level.ability_text[slot].text);
    level.ability_text[slot] = (abilityTextOverride_t){ ability, (uint32_t)field, (uint32_t)rank, player, copy };
    /* A synchronized write runs on every client and supersedes earlier local
     * values. Later local writes continue to shadow the synchronized value. */
    if (player == -1) for (uint32_t i = level.ability_text_count; i-- > 0;) {
        abilityTextOverride_t *entry = level.ability_text + i;
        if (entry->ability == ability && entry->field == (uint32_t)field && entry->rank == (uint32_t)rank && entry->player >= 0) {
            gi.MemFree(entry->text);
            *entry = level.ability_text[--level.ability_text_count];
        }
    }
    FOR_LOOP(i, game.max_clients) {
        gameClient_t *client = game.clients + i;
        if (player == -1 || client->ps.number == player) client->commands_dirty = true;
    }
    return true;
}

static int AbilityTextCompareKeys(void const *left, void const *right) {
    abilityTextOverride_t const *a = *(abilityTextOverride_t const * const *)left;
    abilityTextOverride_t const *b = *(abilityTextOverride_t const * const *)right;
    if (a->ability != b->ability) return a->ability > b->ability ? 1 : -1;
    if (a->field != b->field) return a->field > b->field ? 1 : -1;
    if (a->rank != b->rank) return a->rank > b->rank ? 1 : -1;
    return (a->player > b->player) - (a->player < b->player);
}

bool G_ValidateAbilityTooltipText(void) {
    uint32_t count = level.ability_text_count;
    if (!count) return true;
    if (!level.ability_text || count > UINT32_MAX / sizeof(abilityTextOverride_t *)) return false;
    abilityTextOverride_t const **sorted = gi.MemAlloc(count * sizeof(*sorted));
    if (!sorted) return false;
    bool valid = true;
    FOR_LOOP(i, count) {
        abilityTextOverride_t const *entry = level.ability_text + i;
        sorted[i] = entry;
        if (entry->rank > INT32_MAX || !entry->text ||
            !AbilityTextKeyValid(entry->ability, (abilityTextField_t)entry->field, (int32_t)entry->rank, entry->player)) {
            valid = false; break;
        }
    }
    if (valid) {
        qsort(sorted, count, sizeof(*sorted), AbilityTextCompareKeys);
        for (uint32_t i = 1; i < count; i++) if (!AbilityTextCompareKeys(sorted + i - 1, sorted + i)) {
            valid = false; break;
        }
    }
    gi.MemFree(sorted);
    return valid;
}
