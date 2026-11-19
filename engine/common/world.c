#include "common.h"
#include "mapinfo_read.h"
#include "games/warcraft-3/common/war3map_vertices.h"
#include <ctype.h>

#ifndef _WIN32
#include <strings.h>
#endif

struct world_state world = { 0 };
static PATHSTR cm_loaded_map = { 0 };
static uint32_t cm_map_checksum;

#define CM_MAP_CRC32_POLY 0xedb88320u // CRC-32 polynomial; identifies the authoritative bytes accepted by the map loader

/* Hash the exact virtual map file supplied to the format loader for Q2-style client map validation. */
static uint32_t CM_CalcMapChecksum(uint8_t const *bytes, uint32_t size) {
    uint32_t crc = 0xffffffffu;

    FOR_LOOP(i, size) {
        uint32_t bit;
        crc ^= bytes[i];
        for (bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (CM_MAP_CRC32_POLY & (uint32_t)-(int32_t)(crc & 1));
    }
    return ~crc;
}

void CM_ReadPathMap(handle_t archive);

void PF_TextRemoveComments(string_t buffer);
BOMStatus PF_TextRemoveBom(string_t buffer);

uint32_t SFileReadStringLength(handle_t file) {
    uint32_t filePosition = SFileSetFilePointer(file, 0, 0, FILE_CURRENT);
    uint32_t stringLength = 1;
    /* Scan in blocks: a 1-byte SFileReadFile re-inflates the whole sector, which made
     * maps with large object data (e.g. 500 KB of w3a strings) load for many minutes. */
    while (true) {
        char chunk[512];
        uint32_t got = 0;
        if (!SFileReadFile(file, chunk, sizeof(chunk), &got, NULL) && got == 0) {
            break;
        }
        if (got == 0) {
            break;
        }
        char const *nul = memchr(chunk, 0, got);
        if (nul) {
            stringLength += (uint32_t)(nul - chunk);
            break;
        }
        stringLength += got;
    }
    SFileSetFilePointer(file, filePosition, 0, FILE_BEGIN);
    return stringLength;
}

void SFileReadString(handle_t file, string_t *lppString) {
    uint32_t stringLength = SFileReadStringLength(file);
    *lppString = MemAlloc(stringLength);
    SFileReadFile(file, *lppString, stringLength, NULL, NULL);
    /* A truncated map may end without the requested NUL. */
    (*lppString)[stringLength - 1] = '\0';
}

static uint32_t SFileBytesRemaining(handle_t file) {
    uint32_t position = SFileSetFilePointer(file, 0, 0, FILE_CURRENT);
    uint32_t size = SFileGetFileSize(file, NULL);

    if (position >= size) {
        return 0;
    }
    return size - position;
}

static bool CM_ReadInfoInto(handle_t archive, mapInfo_t *info, bool setup_only) {
    handle_t file;

    if (!archive || !info) {
        return false;
    }
    if (!SFileOpenFileEx(archive, "war3map.w3i", SFILE_OPEN_FROM_MPQ, &file)) {
        return false;
    }
    uint32_t size=SFileGetFileSize(file,NULL),consumed=0;
    void *data=MemAlloc(size);
    bool valid=SFileReadFile(file,data,size,NULL,NULL)&&MapInfo_ParseSetup(data,size,info,MemAlloc,MemFree,&consumed);
    MemFree(data);
    if(!valid){SFileCloseFile(file);return false;}
    SFileSetFilePointer(file,consumed,0,FILE_BEGIN);

    if (setup_only || SFileBytesRemaining(file) <= 1) {
        SFileCloseFile(file);
        return true;
    }
    SFileReadFile(file, &info->num_upgradeAvailabilities, sizeof(uint32_t), NULL, NULL);
    info->upgradeAvailabilities = MemAlloc(sizeof(mapUpgradeAvailability_t) * info->num_upgradeAvailabilities);
    FOR_LOOP(i, info->num_upgradeAvailabilities) {
        mapUpgradeAvailability_t *upgrade = &info->upgradeAvailabilities[i];
        SFileReadFile(file, &upgrade->playerFlags, sizeof(uint32_t), NULL, NULL);
        SFileReadFile(file, &upgrade->upgradeID, sizeof(uint32_t), NULL, NULL);
        SFileReadFile(file, &upgrade->levelOfTheUpgrade, sizeof(uint32_t), NULL, NULL);
        SFileReadFile(file, &upgrade->availability, sizeof(upgradeAvailability_t), NULL, NULL);
    }

    if (SFileBytesRemaining(file) <= 1) {
        SFileCloseFile(file);
        return true;
    }
    SFileReadFile(file, &info->num_techAvailabilities, sizeof(uint32_t), NULL, NULL);
    info->techAvailabilities = MemAlloc(sizeof(mapTechAvailability_t) * info->num_techAvailabilities);
    FOR_LOOP(i, info->num_techAvailabilities) {
        mapTechAvailability_t *tech = &info->techAvailabilities[i];
        SFileReadFile(file, &tech->playerFlags, sizeof(uint32_t), NULL, NULL);
        SFileReadFile(file, &tech->techID, sizeof(uint32_t), NULL, NULL);
    }

    if (SFileBytesRemaining(file) <= 1) {
        SFileCloseFile(file);
        return true;
    }
    SFileReadFile(file, &info->num_randomUnits, sizeof(uint32_t), NULL, NULL);
    info->randomUnits = MemAlloc(sizeof(mapRandomUnitTable_t) * info->num_randomUnits);
    FOR_LOOP(i, info->num_randomUnits) {
        mapRandomUnitTable_t *table = &info->randomUnits[i];
        SFileReadFile(file, &table->tableNumber, sizeof(uint32_t), NULL, NULL);
        SFileReadString(file, &table->tableName);
        SFileReadFile(file, &table->num_positions, sizeof(uint32_t), NULL, NULL);
        if (table->num_positions > 0) {
            table->positionTypes = MemAlloc(sizeof(mapRandomGroupPositionType_t) * table->num_positions);
            SFileReadFile(file,
                          table->positionTypes,
                          sizeof(mapRandomGroupPositionType_t) * table->num_positions,
                          NULL,
                          NULL);
        }
        SFileReadFile(file, &table->num_units, sizeof(uint32_t), NULL, NULL);
        if (table->num_units > 0) {
            table->units = MemAlloc(sizeof(mapRandomUnit_t) * table->num_units);
        }
        FOR_LOOP(j, table->num_units) {
            mapRandomUnit_t *unit = &table->units[j];
            SFileReadFile(file, &unit->chance, sizeof(uint32_t), NULL, NULL);
            if (table->num_positions > 0) {
                unit->itemIDs = MemAlloc(sizeof(uint32_t) * table->num_positions);
                SFileReadFile(file, unit->itemIDs, sizeof(uint32_t) * table->num_positions, NULL, NULL);
            }
        }
    }

    if (info->fileFormat >= 25 && SFileBytesRemaining(file) > 1) {
        SFileReadFile(file, &info->num_randomItems, sizeof(uint32_t), NULL, NULL);
        info->randomItems = MemAlloc(sizeof(mapRandomItemTable_t) * info->num_randomItems);
        FOR_LOOP(i, info->num_randomItems) {
            mapRandomItemTable_t *table = &info->randomItems[i];
            SFileReadFile(file, &table->tableNumber, sizeof(uint32_t), NULL, NULL);
            SFileReadString(file, &table->tableName);
            SFileReadFile(file, &table->num_sets, sizeof(uint32_t), NULL, NULL);
            if (table->num_sets > 0) {
                table->sets = MemAlloc(sizeof(mapRandomItemSet_t) * table->num_sets);
            }
            FOR_LOOP(j, table->num_sets) {
                mapRandomItemSet_t *set = &table->sets[j];
                SFileReadFile(file, &set->num_items, sizeof(uint32_t), NULL, NULL);
                if (set->num_items > 0) {
                    set->items = MemAlloc(sizeof(mapRandomItem_t) * set->num_items);
                    SFileReadFile(file, set->items, sizeof(mapRandomItem_t) * set->num_items, NULL, NULL);
                }
            }
        }
    }

    SFileCloseFile(file);
    return true;
}

static void __attribute__((unused)) CM_ReadInfo(handle_t archive) {
    CM_ReadInfoInto(archive, &world.info, false);
}

void CM_FreeMapInfo(mapInfo_t *mapInfo) {
    mapTrigStr_t *string = mapInfo ? mapInfo->strings : NULL;

    if (!mapInfo) {
        return;
    }
    FOR_LOOP(i, MAX_PLAYERS) {
        SAFE_DELETE(mapInfo->players[i].playerName, MemFree);
    }
    FOR_LOOP(i, mapInfo->num_teams) {
        SAFE_DELETE(mapInfo->teams[i].name, MemFree);
    }
    FOR_LOOP(i, mapInfo->num_randomUnits) {
        FOR_LOOP(j, mapInfo->randomUnits[i].num_units) {
            SAFE_DELETE(mapInfo->randomUnits[i].units[j].itemIDs, MemFree);
        }
        SAFE_DELETE(mapInfo->randomUnits[i].units, MemFree);
        SAFE_DELETE(mapInfo->randomUnits[i].positionTypes, MemFree);
        SAFE_DELETE(mapInfo->randomUnits[i].tableName, MemFree);
    }
    FOR_LOOP(i, mapInfo->num_randomItems) {
        FOR_LOOP(j, mapInfo->randomItems[i].num_sets) {
            SAFE_DELETE(mapInfo->randomItems[i].sets[j].items, MemFree);
        }
        SAFE_DELETE(mapInfo->randomItems[i].sets, MemFree);
        SAFE_DELETE(mapInfo->randomItems[i].tableName, MemFree);
    }
    SAFE_DELETE(mapInfo->mapName, MemFree);
    SAFE_DELETE(mapInfo->mapAuthor, MemFree);
    SAFE_DELETE(mapInfo->mapDescription, MemFree);
    SAFE_DELETE(mapInfo->playersRecommended, MemFree);
    SAFE_DELETE(mapInfo->loadingScreenModel, MemFree);
    SAFE_DELETE(mapInfo->loadingScreenText, MemFree);
    SAFE_DELETE(mapInfo->loadingScreenTitle, MemFree);
    SAFE_DELETE(mapInfo->loadingScreenSubtitle, MemFree);
    SAFE_DELETE(mapInfo->prologueScreenModel, MemFree);
    SAFE_DELETE(mapInfo->prologueScreenText, MemFree);
    SAFE_DELETE(mapInfo->prologueScreenTitle, MemFree);
    SAFE_DELETE(mapInfo->prologueScreenSubtitle, MemFree);
    SAFE_DELETE(mapInfo->soundEnvironment, MemFree);
    SAFE_DELETE(mapInfo->teams, MemFree);
    SAFE_DELETE(mapInfo->upgradeAvailabilities, MemFree);
    SAFE_DELETE(mapInfo->techAvailabilities, MemFree);
    SAFE_DELETE(mapInfo->randomUnits, MemFree);
    SAFE_DELETE(mapInfo->randomItems, MemFree);
    SAFE_DELETE(mapInfo->weatherRegions, MemFree);
    while (string) {
        mapTrigStr_t *next = string->next;
        MemFree(string);
        string = next;
    }
    SAFE_DELETE(mapInfo->mapscript, MemFree);
}

#define CM_PLACEMENT_ROC_VERSION 7
#define CM_PLACEMENT_TFT_VERSION 8
#define CM_DOO_MAGIC MAKEFOURCC('W', '3', 'd', 'o')
#define CM_DROPPABLE_ITEM_DISK_SIZE 8
#define CM_INVENTORY_ITEM_DISK_SIZE 8
#define CM_MODIFIED_ABILITY_DISK_SIZE 12

typedef struct {
    uint32_t version;
    uint32_t subversion;
    uint32_t count;
    bool tft;
    bool skins;
    bool units;
} cmPlacementHeader_t;

static bool CM_ReadPlacementHeader(handle_t file, cstring_t filename, cmPlacementHeader_t *header) {
    uint32_t magic;

    if (!file || !header) {
        return false;
    }
    memset(header, 0, sizeof(*header));
    if (!SFileReadFile(file, &magic, sizeof(magic), NULL, NULL) ||
        !SFileReadFile(file, &header->version, sizeof(header->version), NULL, NULL) ||
        !SFileReadFile(file, &header->subversion, sizeof(header->subversion), NULL, NULL) ||
        !SFileReadFile(file, &header->count, sizeof(header->count), NULL, NULL)) {
        fprintf(stderr, "CM_ReadPlacementHeader: short %s header\n", filename);
        return false;
    }
    if (magic != CM_DOO_MAGIC) {
        fprintf(stderr, "CM_ReadPlacementHeader: invalid %s magic 0x%08x\n", filename, (unsigned)magic);
        return false;
    }
    if (header->version != CM_PLACEMENT_ROC_VERSION &&
        header->version != CM_PLACEMENT_TFT_VERSION && header->version != 12 && header->version != 13) {
        fprintf(stderr,
                "CM_ReadPlacementHeader: unsupported %s version %u subversion %u\n",
                filename,
                (unsigned)header->version,
                (unsigned)header->subversion);
        return false;
    }
    /* Unit subversion 9 still omits hero attributes and the item-table word in 3.0. */
    header->tft = header->version >= CM_PLACEMENT_TFT_VERSION && header->subversion >= 11;
    header->skins = header->version >= 12 || (header->version == 8 &&
        (world.info.gameVersionMajor > 1 || (world.info.gameVersionMajor == 1 && world.info.gameVersionMinor >= 32)));
    header->units = !strcmp(filename, "war3mapUnits.doo");
    return true;
}

static bool CM_ReadCount(handle_t file, uint32_t elem_size, uint32_t *count, cstring_t context) {
    uint32_t max_count;

    if (!file || !count || elem_size == 0) {
        return false;
    }
    *count = 0;
    if (!SFileReadFile(file, count, sizeof(*count), NULL, NULL)) {
        fprintf(stderr, "CM_ReadCount: short count for %s\n", context);
        return false;
    }
    max_count = SFileBytesRemaining(file) / elem_size;
    if (*count > max_count) {
        fprintf(stderr,
                "CM_ReadCount: invalid %s count %u max=%u remaining=%u elem=%u\n",
                context,
                (unsigned)*count,
                (unsigned)max_count,
                (unsigned)SFileBytesRemaining(file),
                (unsigned)elem_size);
        return false;
    }
    return true;
}

static void CM_FreeDroppedItemSets(uint32_t num_sets, droppableItemSet_t *sets) {
    if (!sets) {
        return;
    }
    FOR_LOOP(i, num_sets) {
        SAFE_DELETE(sets[i].droppableItems, MemFree);
    }
    MemFree(sets);
}

static void CM_FreeDoodadPlacementData(doodad_t *doodad) {
    if (!doodad) {
        return;
    }
    CM_FreeDroppedItemSets(doodad->num_droppedItemSets, doodad->droppableItemSets);
    SAFE_DELETE(doodad->inventoryItems, MemFree);
    SAFE_DELETE(doodad->modifiedAbilities, MemFree);
    SAFE_DELETE(doodad->diffAvailUnits, MemFree);
    SAFE_DELETE(doodad->extraWords, MemFree);
}

/* 3.0 extension words are retained verbatim; they are not interpreted as extra placements. */
static bool CM_ReadPlacementTail(handle_t file, doodad_t *obj, cmPlacementHeader_t const *header) {
    uint32_t count = 0, words = 2 + (header->version == 13 && !header->units);
    if (header->version < 12) return true;
    if (!SFileReadFile(file, obj->placementData, words * sizeof(uint32_t), NULL, NULL) ||
        !CM_ReadCount(file, 9 * sizeof(uint32_t), &count, "placement extension")) return false;
    ARRAY_COUNT(obj->extraWords) = count * 9;
    if (!count) return true;
    obj->extraWords = MemAlloc(count * 9 * sizeof(uint32_t));
    return SFileReadFile(file, obj->extraWords, count * 9 * sizeof(uint32_t), NULL, NULL);
}

static bool CM_ReadDroppableItems(handle_t file, uint32_t *num, droppableItem_t **data, cstring_t context) {
    uint32_t count;

    if (!num || !data) {
        return false;
    }
    *num = 0;
    *data = NULL;
    if (!CM_ReadCount(file, CM_DROPPABLE_ITEM_DISK_SIZE, &count, context)) {
        return false;
    }
    *num = count;
    if (count > 0) {
        *data = MemAlloc(count * sizeof(**data));
    }
    FOR_LOOP(i, count) {
        uint32_t chance;

        SFileReadFile(file, &(*data)[i].itemID, sizeof(uint32_t), NULL, NULL);
        SFileReadFile(file, &chance, sizeof(chance), NULL, NULL);
        (*data)[i].chanceToDrop = (int)chance;
    }
    return true;
}

static bool CM_ReadDroppedItemSets(handle_t file, uint32_t *num_sets, droppableItemSet_t **sets, cstring_t context) {
    uint32_t count;

    if (!num_sets || !sets) {
        return false;
    }
    *num_sets = 0;
    *sets = NULL;
    if (!CM_ReadCount(file, sizeof(uint32_t), &count, context)) {
        return false;
    }
    *num_sets = count;
    if (count > 0) {
        *sets = MemAlloc(count * sizeof(**sets));
    }
    FOR_LOOP(i, count) {
        droppableItemSet_t *set = &(*sets)[i];
        char item_context[128];
        uint32_t num_items;

        snprintf(item_context, sizeof(item_context), "%s item set %u", context, (unsigned)i);
        if (!CM_ReadDroppableItems(file, &num_items, &set->droppableItems, item_context)) {
            CM_FreeDroppedItemSets(count, *sets);
            *sets = NULL;
            *num_sets = 0;
            return false;
        }
        set->num_droppableItems = (int)num_items;
    }
    return true;
}

static void __attribute__((unused)) CM_ReadDoodads(handle_t archive) {
    handle_t file;
    cmPlacementHeader_t header;

    if (!SFileOpenFileEx(archive, "war3map.doo", SFILE_OPEN_FROM_MPQ, &file)) {
        fprintf(stderr, "CM_ReadDoodads: missing war3map.doo\n");
        return;
    }
    if (!CM_ReadPlacementHeader(file, "war3map.doo", &header)) {
        SFileCloseFile(file);
        return;
    }

    FOR_LOOP(index, header.count) {
        uint32_t count;
        doodad_t *doodad = MemAlloc(sizeof(doodad_t));
        char context[128];

        snprintf(context, sizeof(context), "war3map.doo doodad %u", (unsigned)index);
        doodad->specialDoodad = false;
        doodad->player = PLAYER_NEUTRAL_PASSIVE;
        doodad->hitPoints = (uint32_t)-1;
        doodad->manaPoints = (uint32_t)-1;
        doodad->droppedItemSetPtr = (uint32_t)-1;
        doodad->goldAmount = 12500;
        doodad->targetAcquisition = -1.0f;
        SFileReadFile(file, &doodad->doodID, sizeof(uint32_t), NULL, NULL);
        SFileReadFile(file, &doodad->variation, sizeof(uint32_t), NULL, NULL);
        SFileReadFile(file, &doodad->position, sizeof(vec3_t), NULL, NULL);
        SFileReadFile(file, &doodad->angle, sizeof(float), NULL, NULL);
        SFileReadFile(file, &doodad->scale, sizeof(vec3_t), NULL, NULL);
        doodad->skinID = doodad->doodID;
        if (header.skins) SFileReadFile(file, &doodad->skinID, sizeof(uint32_t), NULL, NULL);
        if (header.version >= 12) SFileReadFile(file, &doodad->placementFlags, sizeof(uint32_t), NULL, NULL);
        SFileReadFile(file, &doodad->flags, sizeof(uint8_t), NULL, NULL);
        SFileReadFile(file, &doodad->treeLife, sizeof(uint8_t), NULL, NULL);
        if (header.tft) {
            SFileReadFile(file, &doodad->droppedItemSetPtr, sizeof(uint32_t), NULL, NULL);
            if (!CM_ReadDroppedItemSets(file, &count, &doodad->droppableItemSets, context)) {
                CM_FreeDoodadPlacementData(doodad);
                MemFree(doodad);
                break;
            }
            doodad->num_droppedItemSets = count;
        }
        SFileReadFile(file, &doodad->unitID, sizeof(uint32_t), NULL, NULL);
        if (!CM_ReadPlacementTail(file, doodad, &header)) {
            CM_FreeDoodadPlacementData(doodad); MemFree(doodad); break;
        }
        
        ADD_TO_LIST(doodad, world.doodads);
    }

    /* The tail of war3map.doo stores cliff/special doodads separately from
     * regular placements. Their X/Y are W3E vertex coordinates, not world
     * coordinates, and their model orientation/scale come from Doodads.slk. */
    if (world.map && world.map->vertices) {
        uint32_t special_version, special_count;
        if (SFileReadFile(file, &special_version, sizeof(special_version), NULL, NULL) &&
            SFileReadFile(file, &special_count, sizeof(special_count), NULL, NULL) &&
            special_version == 0 && special_count <= SFileBytesRemaining(file) / 16) {
            FOR_LOOP(index, special_count) {
                uint32_t z, x, y;
                doodad_t *doodad = MemAlloc(sizeof(*doodad));
                memset(doodad, 0, sizeof(*doodad));
                doodad->player = PLAYER_NEUTRAL_PASSIVE;
                doodad->hitPoints = (uint32_t)-1;
                doodad->manaPoints = (uint32_t)-1;
                doodad->droppedItemSetPtr = (uint32_t)-1;
                doodad->goldAmount = 12500;
                doodad->targetAcquisition = -1.0f;
                doodad->scale = (vec3_t){ 1.0f, 1.0f, 1.0f };
                doodad->flags = 2;
                doodad->treeLife = 100;
                doodad->specialDoodad = true;
                if (!SFileReadFile(file, &doodad->doodID, sizeof(doodad->doodID), NULL, NULL) ||
                    !SFileReadFile(file, &z, sizeof(z), NULL, NULL) ||
                    !SFileReadFile(file, &x, sizeof(x), NULL, NULL) ||
                    !SFileReadFile(file, &y, sizeof(y), NULL, NULL)) {
                    MemFree(doodad);
                    break;
                }
                doodad->position.x = world.map->center.x + x * TILE_SIZE;
                doodad->position.y = world.map->center.y + y * TILE_SIZE;
                /* The second special-record integer is reserved metadata, not
                 * an elevation. Terrain doodad models are positioned at Z=0. */
                (void)z;
                doodad->position.z = 0.0f;
                if (x >= world.map->width || y >= world.map->height) {
                    MemFree(doodad);
                    continue;
                }
                ADD_TO_LIST(doodad, world.doodads);
            }
        }
    }

    SFileCloseFile(file);
}

static bool CM_ReadInventoryItems(handle_t file, uint32_t *num, inventoryItem_t **data, cstring_t context) {
    uint32_t count;

    if (!num || !data) {
        return false;
    }
    *num = 0;
    *data = NULL;
    if (!CM_ReadCount(file, CM_INVENTORY_ITEM_DISK_SIZE, &count, context)) {
        return false;
    }
    *num = count;
    if (count > 0) {
        *data = MemAlloc(count * sizeof(**data));
    }
    FOR_LOOP(i, count) {
        SFileReadFile(file, &(*data)[i].slot, sizeof(uint32_t), NULL, NULL);
        SFileReadFile(file, &(*data)[i].itemID, sizeof(uint32_t), NULL, NULL);
    }
    return true;
}

static bool CM_ReadModifiedAbilities(handle_t file, uint32_t *num, modifiedAbility_t **data, cstring_t context) {
    uint32_t count;

    if (!num || !data) {
        return false;
    }
    *num = 0;
    *data = NULL;
    if (!CM_ReadCount(file, CM_MODIFIED_ABILITY_DISK_SIZE, &count, context)) {
        return false;
    }
    *num = count;
    if (count > 0) {
        *data = MemAlloc(count * sizeof(**data));
    }
    FOR_LOOP(i, count) {
        SFileReadFile(file, &(*data)[i].abilityID, sizeof(uint32_t), NULL, NULL);
        SFileReadFile(file, &(*data)[i].active, sizeof(uint32_t), NULL, NULL);
        SFileReadFile(file, &(*data)[i].level, sizeof(uint32_t), NULL, NULL);
    }
    return true;
}

static bool CM_ReadUnit(handle_t file, struct Doodad *unit, cmPlacementHeader_t const *header, uint32_t index) {
    char context[128];
    char array_context[256];

    snprintf(context, sizeof(context), "war3mapUnits.doo unit %u", (unsigned)index);

    SFileReadFile(file, &unit->doodID, sizeof(uint32_t), NULL, NULL);
    SFileReadFile(file, &unit->variation, sizeof(uint32_t), NULL, NULL);
    SFileReadFile(file, &unit->position, sizeof(vec3_t), NULL, NULL);
    SFileReadFile(file, &unit->angle, sizeof(float), NULL, NULL);
    SFileReadFile(file, &unit->scale, sizeof(vec3_t), NULL, NULL);
    unit->skinID = unit->doodID;
    if (header->skins) SFileReadFile(file, &unit->skinID, sizeof(uint32_t), NULL, NULL);
    if (header->version >= 12) SFileReadFile(file, &unit->placementFlags, sizeof(uint32_t), NULL, NULL);
    SFileReadFile(file, &unit->flags, sizeof(uint8_t), NULL, NULL);
    SFileReadFile(file, &unit->player, sizeof(uint32_t), NULL, NULL);
    SFileReadFile(file, &unit->unknown1, sizeof(uint8_t), NULL, NULL);
    SFileReadFile(file, &unit->unknown2, sizeof(uint8_t), NULL, NULL);
    SFileReadFile(file, &unit->hitPoints, sizeof(uint32_t), NULL, NULL); // (-1 = use default)
    SFileReadFile(file, &unit->manaPoints, sizeof(uint32_t), NULL, NULL); // (-1 = use default, 0 = unit doesn't have mana)
    if (header->tft) {
        SFileReadFile(file, &unit->droppedItemSetPtr, sizeof(uint32_t), NULL, NULL);
    } else {
        unit->droppedItemSetPtr = (uint32_t)-1;
    }
    if (!CM_ReadDroppedItemSets(file, &unit->num_droppedItemSets, &unit->droppableItemSets, context)) {
        return false;
    }

    SFileReadFile(file, &unit->goldAmount, sizeof(uint32_t), NULL, NULL); // (default = 12500)
    SFileReadFile(file, &unit->targetAcquisition, sizeof(float), NULL, NULL); // (-1 = normal, -2 = camp)

    // war3mapUnits.doo stores only the file-format hero fields here.  The
    // runtime doodadHero_t has extra state, so do not read sizeof(hero).
    SFileReadFile(file, &unit->hero.level, sizeof(uint32_t), NULL, NULL);
    if (header->tft) {
        SFileReadFile(file, &unit->hero.str, sizeof(uint32_t), NULL, NULL);
        SFileReadFile(file, &unit->hero.agi, sizeof(uint32_t), NULL, NULL);
        SFileReadFile(file, &unit->hero.intel, sizeof(uint32_t), NULL, NULL);
    }
    unit->hero.xp = 0;
    unit->hero.suspend_xp = false;

    snprintf(array_context, sizeof(array_context), "%s inventory", context);
    if (!CM_ReadInventoryItems(file, &unit->num_inventoryItems, &unit->inventoryItems, array_context)) {
        return false;
    }
    snprintf(array_context, sizeof(array_context), "%s abilities", context);
    if (!CM_ReadModifiedAbilities(file, &unit->num_modifiedAbilities, &unit->modifiedAbilities, array_context)) {
        return false;
    }

    SFileReadFile(file, &unit->randomUnitFlag, sizeof(uint32_t), NULL, NULL); // "r" (for uDNR units and iDNR items)

    switch ((int32_t)unit->randomUnitFlag) {
        case -1:
            break;
        case 0:
            SFileReadFile(file, &unit->levelOfRandomItem, sizeof(uint32_t), NULL, NULL);
            break;
        case 1:
            SFileReadFile(file, &unit->randomUnitGroupNumber, sizeof(uint32_t), NULL, NULL);
            SFileReadFile(file, &unit->randomUnitPositionNumber, sizeof(uint32_t), NULL, NULL);
            break;
        case 2:
            snprintf(array_context, sizeof(array_context), "%s random unit choices", context);
            if (!CM_ReadDroppableItems(file, &unit->num_diffAvailUnits, &unit->diffAvailUnits, array_context)) {
                return false;
            }
            break;
        default:
            fprintf(stderr,
                    "CM_ReadUnit: invalid random unit flag %d in %s\n",
                    (int)unit->randomUnitFlag,
                    context);
            return false;
    }
    SFileReadFile(file, &unit->color, sizeof(uint32_t), NULL, NULL);
    SFileReadFile(file, &unit->waygate, sizeof(uint32_t), NULL, NULL);
    SFileReadFile(file, &unit->unitID, sizeof(uint32_t), NULL, NULL);
    return CM_ReadPlacementTail(file, unit, header);
}

static void __attribute__((unused)) CM_ReadUnitDoodads(handle_t archive) {
    handle_t file;
    cmPlacementHeader_t header;

    if (!SFileOpenFileEx(archive, "war3mapUnits.doo", SFILE_OPEN_FROM_MPQ, &file)) {
        fprintf(stderr, "CM_ReadUnitDoodads: missing war3mapUnits.doo\n");
        return;
    }
    if (!CM_ReadPlacementHeader(file, "war3mapUnits.doo", &header)) {
        SFileCloseFile(file);
        return;
    }

    FOR_LOOP(index, header.count) {
        doodad_t *doodad = MemAlloc(sizeof(doodad_t));
        if (!CM_ReadUnit(file, doodad, &header, index)) {
            CM_FreeDoodadPlacementData(doodad);
            MemFree(doodad);
            break;
        }
        ADD_TO_LIST(doodad, world.doodads);
    }

    SFileCloseFile(file);
}

static void __attribute__((unused)) CM_ReadHeightmap(handle_t archive) {
    handle_t file;
    if (!SFileOpenFileEx(archive, "war3map.w3e", SFILE_OPEN_FROM_MPQ, &file)) {
        Com_Error(ERR_DROP, "CM_ReadHeightmap: missing war3map.w3e\n");
        return;
    }
    world.map = MemAlloc(sizeof(war3map_t));
    if (!W3E_ReadHeader(file, world.map, MemAlloc)) {
        SFileCloseFile(file);
        Com_Error(ERR_DROP, "CM_ReadHeightmap: invalid W3E header\n");
        return;
    }
    bool const valid_vertices = W3E_ReadVertexBlock(file, world.map, MemAlloc, MemFree);
    SFileCloseFile(file);
    if (!valid_vertices)
        Com_Error(ERR_DROP, "CM_ReadHeightmap: invalid or truncated W3E vertex block\n");
}

void CM_ReadModification(handle_t file, unitModification_t *mod) {
    SFileReadFile(file, &mod->modID, 4, NULL, NULL);
    SFileReadFile(file, &mod->type, 4, NULL, NULL);
    mod->level = 0;
    mod->dataPointer = 0;
    uint32_t strlength = 0;
    switch (mod->type) {
        case mod_int:
        case mod_real:
        case mod_unreal:
            mod->data = MemAlloc(4);
            SFileReadFile(file, mod->data, 4, NULL, NULL);
            break;
        case mod_bool:
        case mod_char:
            mod->data = MemAlloc(1);
            SFileReadFile(file, mod->data, 1, NULL, NULL);
            break;
        case mod_string:
        case mod_unitList:
        case mod_itemList:
        case mod_regenType:
        case mod_attackType:
        case mod_weaponType:
        case mod_targetType:
        case mod_moveType:
        case mod_defenseType:
        case mod_pathingTexture:
        case mod_upgradeList:
        case mod_stringList:
        case mod_abilityList:
        case mod_heroAbilityList:
        case mod_missileArt:
        case mod_attributeType:
        case mod_attackBits:
            strlength = SFileReadStringLength(file);
            mod->data = MemAlloc(strlength);
            SFileReadFile(file, mod->data, strlength, NULL, NULL);
            break;
        default:
            assert(false);
            break;
    }
}

typedef struct { uint32_t version; void (*read)(handle_t, unitModification_t *); } cmObjectRead_t;

/* Object data v3 inserts modification sets; v1/v2 retain one implicit set. */
static unitData_t *CM_ReadObjectOverrides(handle_t file, uint32_t *num, cmObjectRead_t const *fmt) {
    uint32_t count = 0;
    unitData_t *objects;
    *num = 0;
    if (fmt->version < 1 || fmt->version > 3 || !CM_ReadCount(file, 12, &count, "object table")) return NULL;
    if (!count) return NULL;
    objects = MemAlloc(count * sizeof(*objects));
    memset(objects, 0, count * sizeof(*objects));
    FOR_LOOP(i, count) {
        unitData_t *obj = objects + i;
        uint32_t sets = 1;
        if (!SFileReadFile(file, &obj->originalUnitID, 4, NULL, NULL) ||
            !SFileReadFile(file, &obj->newUnitID, 4, NULL, NULL)) goto invalid;
        if (fmt->version == 3 && !CM_ReadCount(file, 8, &sets, "object modification sets")) goto invalid;
        FOR_LOOP(k, sets) {
            uint32_t flags = 0, mods = 0, tag;
            if (fmt->version == 3 && !SFileReadFile(file, &flags, 4, NULL, NULL)) goto invalid;
            /* The classic campaign data uses flag 0; unknown semantic sets must not be silently flattened. */
            if (flags) { fprintf(stderr, "CM_ReadObjectOverrides: unsupported modification set flags %u\n", flags); goto invalid; }
            if (!CM_ReadCount(file, 12, &mods, "object modifications") || mods > UINT16_MAX - obj->numbeOfModifications)
                goto invalid;
            uint32_t total = obj->numbeOfModifications + mods;
            if (mods) {
                unitModification_t *next = MemAlloc(total * sizeof(*next));
                if (obj->numbeOfModifications) memcpy(next, obj->modifications, obj->numbeOfModifications * sizeof(*next));
                MemFree(obj->modifications); obj->modifications = next;
            }
            FOR_LOOP(j, mods) {
                fmt->read(file, obj->modifications + obj->numbeOfModifications++);
                if (!SFileReadFile(file, &tag, 4, NULL, NULL)) goto invalid;
            }
        }
    }
    *num = count;
    return objects;
invalid:
    fprintf(stderr, "CM_ReadObjectOverrides: invalid or truncated object data v%u\n", fmt->version);
    FOR_LOOP(i, count) {
        FOR_LOOP(j, objects[i].numbeOfModifications) MemFree(objects[i].modifications[j].data);
        MemFree(objects[i].modifications);
    }
    MemFree(objects);
    return NULL;
}

static void CM_ReadObjectData(handle_t archive, cstring_t filename,
                              uint32_t *num_original, unitData_t **original,
                              uint32_t *num_custom, unitData_t **custom) {
    uint32_t version;
    handle_t file;

    if (!SFileOpenFileEx(archive, filename, SFILE_OPEN_FROM_MPQ, &file)) {
        *num_original = *num_custom = 0;
        *original = *custom = NULL;
        return;
    }
    SFileReadFile(file, &version, 4, NULL, NULL);
    cmObjectRead_t fmt = { version, CM_ReadModification };
    *original = CM_ReadObjectOverrides(file, num_original, &fmt);
    *custom = CM_ReadObjectOverrides(file, num_custom, &fmt);
    SFileCloseFile(file);
}

/* Ability/doodad/upgrade object files insert level + data-pointer ints before the value. */
void CM_ReadAbilityModification(handle_t file, unitModification_t *mod) {
    SFileReadFile(file, &mod->modID, 4, NULL, NULL);
    SFileReadFile(file, &mod->type, 4, NULL, NULL);
    SFileReadFile(file, &mod->level, 4, NULL, NULL);
    SFileReadFile(file, &mod->dataPointer, 4, NULL, NULL);
    uint32_t strlength = 0;
    switch (mod->type) {
        case mod_int:
        case mod_real:
        case mod_unreal:
            mod->data = MemAlloc(4);
            SFileReadFile(file, mod->data, 4, NULL, NULL);
            break;
        case mod_bool:
        case mod_char:
            mod->data = MemAlloc(1);
            SFileReadFile(file, mod->data, 1, NULL, NULL);
            break;
        case mod_string:
        case mod_unitList:
        case mod_itemList:
        case mod_regenType:
        case mod_attackType:
        case mod_weaponType:
        case mod_targetType:
        case mod_moveType:
        case mod_defenseType:
        case mod_pathingTexture:
        case mod_upgradeList:
        case mod_stringList:
        case mod_abilityList:
        case mod_heroAbilityList:
        case mod_missileArt:
        case mod_attributeType:
        case mod_attackBits:
            strlength = SFileReadStringLength(file);
            mod->data = MemAlloc(strlength);
            SFileReadFile(file, mod->data, strlength, NULL, NULL);
            break;
        default:
            fprintf(stderr, "CM_ReadAbilityModification: unknown type %u for '%.4s'\n",
                    (unsigned)mod->type, (cstring_t)&mod->modID);
            mod->data = NULL;
            break;
    }
}

void CM_ReadUnits(handle_t archive) {
    CM_ReadObjectData(archive, "war3map.w3u",
                      &world.info.num_originalUnits, &world.info.originalUnits,
                      &world.info.num_userCreatedUnits, &world.info.userCreatedUnits);
}

void CM_ReadItems(handle_t archive) {
    CM_ReadObjectData(archive, "war3map.w3t",
                      &world.info.num_originalItems, &world.info.originalItems,
                      &world.info.num_userCreatedItems, &world.info.userCreatedItems);
}

void CM_ReadAbilities(handle_t archive) {
    uint32_t version;
    handle_t file;
    if (!SFileOpenFileEx(archive, "war3map.w3a", SFILE_OPEN_FROM_MPQ, &file)) {
        world.info.num_originalAbilities = 0;
        world.info.num_userCreatedAbilities = 0;
        world.info.originalAbilities = NULL;
        world.info.userCreatedAbilities = NULL;
        return;
    }
    SFileReadFile(file, &version, 4, NULL, NULL);
    cmObjectRead_t fmt = { version, CM_ReadAbilityModification };
    world.info.originalAbilities = CM_ReadObjectOverrides(file, &world.info.num_originalAbilities, &fmt);
    world.info.userCreatedAbilities = CM_ReadObjectOverrides(file, &world.info.num_userCreatedAbilities, &fmt);
    SFileCloseFile(file);
}

void CM_ReadBuffs(handle_t archive) {
    CM_ReadObjectData(archive, "war3map.w3h",
                      &world.info.num_originalBuffs, &world.info.originalBuffs,
                      &world.info.num_userCreatedBuffs, &world.info.userCreatedBuffs);
}

void CM_ReadDestructables(handle_t archive) {
    CM_ReadObjectData(archive, "war3map.w3b",
                      &world.info.num_originalDestructables, &world.info.originalDestructables,
                      &world.info.num_userCreatedDestructables, &world.info.userCreatedDestructables);
}

/* W3D version 1 uses the simple object-modification layout. Version 2 uses
 * the ability-style layout with level and data-pointer words before the value. */
void CM_ReadDoodadOverrides(handle_t archive) {
    handle_t file;
    uint32_t version;

    world.info.num_originalDoodads = world.info.num_userCreatedDoodads = 0;
    world.info.originalDoodads = world.info.userCreatedDoodads = NULL;
    if (!SFileOpenFileEx(archive, "war3map.w3d", SFILE_OPEN_FROM_MPQ, &file)) return;
    SFileReadFile(file, &version, sizeof(version), NULL, NULL);
    /* v1 has no level/data-pointer words; treating it as v2 shifts the value
     * and the following object count. v3 retains its counted-set handling. */
    cmObjectRead_t fmt = { version, version == 1 ? CM_ReadModification : CM_ReadAbilityModification };
    world.info.originalDoodads = CM_ReadObjectOverrides(file, &world.info.num_originalDoodads, &fmt);
    world.info.userCreatedDoodads = CM_ReadObjectOverrides(file, &world.info.num_userCreatedDoodads, &fmt);
    SFileCloseFile(file);
}

string_t FS_ReadArchiveFileIntoString(handle_t archive, cstring_t filename) {
    handle_t file;
    if (!SFileOpenFileEx(archive, filename, SFILE_OPEN_FROM_MPQ, &file)) {
        return NULL;
    }
    uint32_t fileSize = SFileGetFileSize(file, NULL);
    string_t buffer = MemAlloc(fileSize + 1);
    SFileReadFile(file, buffer, fileSize, NULL, NULL);
    SFileCloseFile(file);
    buffer[fileSize] = '\0';
    return buffer;
}

void removeTrailingWhitespace(char *s) {
    size_t len;

    if (!s) {
        return;
    }
    len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
}

static cstring_t CM_SkipSpace(cstring_t text) {
    while (text && (*text == ' ' || *text == '\t' || *text == '\r')) {
        text++;
    }
    return text;
}

static void CM_ReadLine(cstring_t *cursor, string_t out, uint32_t out_size) {
    uint32_t len = 0;
    cstring_t p;

    if (!cursor || !*cursor || !out || out_size == 0) {
        return;
    }
    p = *cursor;
    while (*p && *p != '\n' && *p != '\r') {
        if (len + 1 < out_size) {
            out[len++] = *p;
        }
        p++;
    }
    out[len] = '\0';
    while (*p == '\n' || *p == '\r') {
        p++;
    }
    *cursor = p;
}

static void CM_AppendTrigStringText(mapTrigStr_t *entry, cstring_t line) {
    uint32_t len;
    uint32_t add;

    if (!entry || !line) {
        return;
    }
    len = (uint32_t)strlen(entry->text);
    add = (uint32_t)strlen(line);
    if (len + add >= sizeof(entry->text)) {
        add = sizeof(entry->text) - len - 1;
    }
    if (add) {
        memcpy(entry->text + len, line, add);
        entry->text[len + add] = '\0';
    }
}

static void CM_ReadStringsInto(handle_t archive, mapInfo_t *info) {
    string_t buffer = FS_ReadArchiveFileIntoString(archive, "war3map.wts");
    cstring_t cursor;
    mapTrigStr_t *entry = NULL;
    bool reading_data = false;
    char line[MAX_TRIGSTR_LENGTH];

    if (!info) {
        SAFE_DELETE(buffer, MemFree);
        return;
    }
    if (!buffer) {
        return;
    }
    PF_TextRemoveBom(buffer);
    cursor = buffer;
    while (*cursor) {
        cstring_t trimmed;

        CM_ReadLine(&cursor, line, sizeof(line));
        trimmed = CM_SkipSpace(line);
        if (!reading_data) {
            if (!strncmp(trimmed, "STRING ", strlen("STRING "))) {
                SAFE_DELETE(entry, MemFree);
                entry = MemAlloc(sizeof(*entry));
                memset(entry, 0, sizeof(*entry));
                sscanf(trimmed, "STRING %d", &entry->id);
            } else if (entry && *trimmed == '{') {
                reading_data = true;
            }
            continue;
        }
        if (*trimmed == '}') {
            ADD_TO_LIST(entry, info->strings);
            entry = NULL;
            reading_data = false;
            continue;
        }
        removeTrailingWhitespace(line);
        CM_AppendTrigStringText(entry, line);
    }
    if (entry) {
        if (reading_data) {
            ADD_TO_LIST(entry, info->strings);
        } else {
            MemFree(entry);
        }
    }
    MemFree(buffer);
}

/* Loading presentation needs only map metadata and trigger strings, before terrain or entity parsing. */
bool CM_ReadMapInfo(cstring_t filename, mapInfo_t *info) {
    handle_t archive, data;
    uint32_t size = 0;
    bool valid;

    memset(info, 0, sizeof(*info));
    data = FS_ReadFile(filename, &size);
    if (!data || !SFileOpenArchiveFromMemory(data, size, 0, &archive)) {
        fprintf(stderr, "CM_ReadMapInfo: cannot open %s\n", filename);
        if (data) FS_FreeFile(data);
        return false;
    }
    valid = CM_ReadInfoInto(archive, info, true);
    if (valid) CM_ReadStringsInto(archive, info);
    else fprintf(stderr, "CM_ReadMapInfo: missing map info in %s\n", filename);
    SFileCloseArchive(archive);
    FS_FreeFile(data);
    if (!valid) CM_FreeMapInfo(info);
    return valid;
}

void CM_ReadStrings(handle_t archive) {
    CM_ReadStringsInto(archive, &world.info);
}

void CM_ReadMapScript(handle_t archive) {
    /* W3I selects the original script language. Root and scripts/ are both
     * retail locations; a missing selected script must not silently run another language. */
    cstring_t root_name = world.info.scriptType == 1 ? "war3map.lua" : "war3map.j";
    cstring_t nested_name = world.info.scriptType == 1 ? "scripts\\war3map.lua" : "scripts\\war3map.j";
    if (world.info.scriptType > 1) {
        fprintf(stderr, "CM_ReadMapScript: unknown script type %u\n", world.info.scriptType);
        return;
    }
    world.info.mapscript = FS_ReadArchiveFileIntoString(archive, root_name);
    if (!world.info.mapscript)
        world.info.mapscript = FS_ReadArchiveFileIntoString(archive, nested_name);
    if (!world.info.mapscript)
        fprintf(stderr, "CM_ReadMapScript: missing %s / %s in %s\n", root_name, nested_name,
                cm_loaded_map[0] ? cm_loaded_map : "(unknown)");
}

bool CM_LoadMap(cstring_t mapFilename, cmLoadYield_t yield) {
    handle_t data;
    uint32_t size = 0;
    bool loaded;

    cm_map_checksum = 0;
    snprintf(cm_loaded_map, sizeof(cm_loaded_map), "%s", mapFilename ? mapFilename : "");
    data = FS_ReadFile(mapFilename, &size);
    if (data && size) {
        cm_map_checksum = CM_CalcMapChecksum((uint8_t const *)data, size);
        FS_FreeFile(data);
    } else {
        fprintf(stderr, "CM_LoadMap: unable to read map bytes for checksum %s\n", mapFilename ? mapFilename : "");
        if (data) FS_FreeFile(data);
    }
    yield();
    loaded = CM_LoadMapFormat(mapFilename, yield);
    if (!loaded) {
        cm_loaded_map[0] = '\0';
        cm_map_checksum = 0;
    }
    return loaded;
}

uint32_t CM_GetMapChecksum(void) { return cm_map_checksum; }

bool CM_IsMapLoaded(cstring_t mapFilename) {
#ifdef WOW
    return cm_loaded_map[0] && mapFilename && !strcmp(cm_loaded_map, mapFilename);
#else
    return world.map && mapFilename && !strcmp(cm_loaded_map, mapFilename);
#endif
}

doodad_t *CM_GetDoodads(void) {
    return world.doodads;
}

uint32_t CM_GetLocalPlayerNumber(void) {
    FOR_LOOP(i, MAX_PLAYERS) {
        mapPlayer_t const *player = world.info.players + i;
        if (player->playerType == kPlayerTypeHuman)
            return i;
    }
    return 0;
}

mapInfo_t const *CM_GetMapInfo(void) {
    return &world.info;
}

void CM_ReleaseModel(void) {
    CM_FreeMapInfo(&world.info);
}
