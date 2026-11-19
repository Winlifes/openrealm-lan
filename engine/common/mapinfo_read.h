#ifndef BZ_MAPINFO_READ_H
#define BZ_MAPINFO_READ_H
#include "mapinfo.h"

/* One bounded metadata decoder for the world loader and menu browser. */
typedef struct {
    ARRAY(uint8_t const, bytes);
    uint32_t position;
    bool valid;
    handle_t (*alloc)(long);
} mapInfoReader_t;

static inline bool MapInfo_ReadBytes(mapInfoReader_t *r,void *out,uint32_t size) {
    if (!r->valid || size > ARRAY_COUNT(r->bytes)-r->position) {
        r->valid=false;memset(out,0,size);return false;
    }
    memcpy(out,r->bytes+r->position,size);r->position+=size;return true;
}
static inline void MapInfo_ReadString(mapInfoReader_t *r,string_t *out) {
    *out=NULL;if(!r->valid)return;
    uint8_t const *zero=memchr(r->bytes+r->position,0,ARRAY_COUNT(r->bytes)-r->position);
    if(!zero){r->valid=false;return;}
    uint32_t size=(uint32_t)(zero-(r->bytes+r->position))+1;
    *out=r->alloc(size);MapInfo_ReadBytes(r,*out,size);
}
static inline bool MapInfo_ParseSetup(void const *data,uint32_t size,mapInfo_t *info,
    handle_t (*alloc)(long),void (*free_data)(handle_t),uint32_t *consumed) {
    mapInfoReader_t reader={.bytes=data,.valid=true,.alloc=alloc};ARRAY_COUNT(reader.bytes)=size;
    MapInfo_ReadBytes(&reader, &info->fileFormat, 4);
    MapInfo_ReadBytes(&reader, &info->numberOfSaves, sizeof(uint32_t));
    MapInfo_ReadBytes(&reader, &info->editorVersion, sizeof(uint32_t));
    if (info->fileFormat >= 28) {
        MapInfo_ReadBytes(&reader, &info->gameVersionMajor, sizeof(uint32_t));
        MapInfo_ReadBytes(&reader, &info->gameVersionMinor, sizeof(uint32_t));
        MapInfo_ReadBytes(&reader, &info->gameVersionPatch, sizeof(uint32_t));
        MapInfo_ReadBytes(&reader, &info->gameVersionBuild, sizeof(uint32_t));
    }
    MapInfo_ReadString(&reader, &info->mapName);
    MapInfo_ReadString(&reader, &info->mapAuthor);
    MapInfo_ReadString(&reader, &info->mapDescription);
    MapInfo_ReadString(&reader, &info->playersRecommended);
    MapInfo_ReadBytes(&reader, &info->cameraBounds, sizeof(mapCameraBounds_t));
    MapInfo_ReadBytes(&reader, &info->playableArea, sizeof(size2_t));
    MapInfo_ReadBytes(&reader, &info->flags, sizeof(uint32_t));
    MapInfo_ReadBytes(&reader, &info->mainGroundType, sizeof(char));
    MapInfo_ReadBytes(&reader, &info->campaignBackgroundNumber, sizeof(uint32_t));
    /* v39 inserts the crest before the model string; treating it as text shifts every later field. */
    if (info->fileFormat >= 39) MapInfo_ReadBytes(&reader, &info->loadingScreenRace, sizeof(uint32_t));
    if (info->fileFormat >= 25) {
        MapInfo_ReadString(&reader, &info->loadingScreenModel);
    }
    MapInfo_ReadString(&reader, &info->loadingScreenText);
    MapInfo_ReadString(&reader, &info->loadingScreenTitle);
    MapInfo_ReadString(&reader, &info->loadingScreenSubtitle);
    if (info->fileFormat >= 25) {
        MapInfo_ReadBytes(&reader, &info->gameDataSet, sizeof(uint32_t));
    } else {
        MapInfo_ReadBytes(&reader, &info->loadingScreenNumber, sizeof(uint32_t));
    }
    if (info->fileFormat >= 25) {
        MapInfo_ReadString(&reader, &info->prologueScreenModel);
    }
    MapInfo_ReadString(&reader, &info->prologueScreenText);
    MapInfo_ReadString(&reader, &info->prologueScreenTitle);
    MapInfo_ReadString(&reader, &info->prologueScreenSubtitle);
    if (info->fileFormat >= 25) {
        MapInfo_ReadBytes(&reader, &info->fogStyle, sizeof(uint32_t));
        MapInfo_ReadBytes(&reader, &info->fogStartZ, sizeof(float));
        MapInfo_ReadBytes(&reader, &info->fogEndZ, sizeof(float));
        MapInfo_ReadBytes(&reader, &info->fogDensity, sizeof(float));
        MapInfo_ReadBytes(&reader, &info->fogColor, sizeof(color32_t));
        if (info->fileFormat >= 39) MapInfo_ReadBytes(&reader, &info->terrainFog, sizeof(info->terrainFog));
        MapInfo_ReadBytes(&reader, &info->weatherID, sizeof(uint32_t));
        MapInfo_ReadString(&reader, &info->soundEnvironment);
        MapInfo_ReadBytes(&reader, &info->lightEnvironmentTileset, sizeof(uint8_t));
        MapInfo_ReadBytes(&reader, &info->waterColor, sizeof(color32_t));
    }
    if (info->fileFormat >= 28) {
        MapInfo_ReadBytes(&reader, &info->scriptType, sizeof(uint32_t));
    }
    if (info->fileFormat >= 31) {
        MapInfo_ReadBytes(&reader, &info->supportedModes, sizeof(uint32_t));
        MapInfo_ReadBytes(&reader, &info->gameDataVersion, sizeof(uint32_t));
    }
    if (info->fileFormat >= 32) {
        MapInfo_ReadBytes(&reader, &info->defaultZoomOverride, sizeof(uint32_t));
        MapInfo_ReadBytes(&reader, &info->maximumZoomOverride, sizeof(uint32_t));
    }
    if (info->fileFormat >= 33) {
        MapInfo_ReadBytes(&reader, &info->minimumZoomOverride, sizeof(uint32_t));
    }
    if (info->fileFormat >= 39) MapInfo_ReadBytes(&reader, &info->waterOverrides, sizeof(info->waterOverrides));

    uint32_t num_players = 0;
    MapInfo_ReadBytes(&reader, &num_players, sizeof(uint32_t));
    if (num_players > 28 || num_players > (ARRAY_COUNT(reader.bytes) - reader.position) / 33) {
        fprintf(stderr, "MapInfo_ParseSetup: invalid player count %u\n", num_players);
        return false;
    }
    FOR_LOOP(i, num_players) {
        uint32_t playerNumber = 0;
        mapPlayer_t scratch = { 0 };
        mapPlayer_t *player;

        MapInfo_ReadBytes(&reader, &playerNumber, sizeof(uint32_t));
        player = playerNumber < MAX_PLAYERS ? info->players + playerNumber : &scratch;
        player->used = true;
        MapInfo_ReadBytes(&reader, &player->playerType, sizeof(playerType_t));
        MapInfo_ReadBytes(&reader, &player->playerRace, sizeof(playerRace_t));
        if (info->fileFormat >= 39 && info->editorVersion >= 6128) {
            uint32_t skin; MapInfo_ReadBytes(&reader, &skin, sizeof(skin));
        }
        MapInfo_ReadBytes(&reader, &player->flags, sizeof(uint32_t));
        MapInfo_ReadString(&reader, &player->playerName);
        MapInfo_ReadBytes(&reader, &player->startingPosition, sizeof(vec2_t));
        MapInfo_ReadBytes(&reader, &player->allyLowPrioritiesFlags, sizeof(uint32_t));
        MapInfo_ReadBytes(&reader, &player->allyHighPrioritiesFlags, sizeof(uint32_t));
        if (info->fileFormat >= 31) {
            MapInfo_ReadBytes(&reader, &player->enemyLowPrioritiesFlags, sizeof(uint32_t));
            MapInfo_ReadBytes(&reader, &player->enemyHighPrioritiesFlags, sizeof(uint32_t));
        }
        SAFE_DELETE(scratch.playerName, free_data);
    }

    MapInfo_ReadBytes(&reader, &info->num_teams, sizeof(uint32_t));
    if (info->num_teams > (ARRAY_COUNT(reader.bytes) - reader.position) / 9) {
        fprintf(stderr, "MapInfo_ParseSetup: invalid team count %u\n", info->num_teams);
        info->num_teams = 0; return false;
    }
    info->teams = info->num_teams ? alloc(sizeof(mapTeam_t) * info->num_teams) : NULL;
    FOR_LOOP(i, info->num_teams) {
        mapTeam_t *force = &info->teams[i];
        MapInfo_ReadBytes(&reader, &force->flags, sizeof(uint32_t));
        MapInfo_ReadBytes(&reader, &force->playerMasks, sizeof(uint32_t));
        MapInfo_ReadString(&reader, &force->name);
    }

    if(consumed)*consumed=reader.position;
    return reader.valid;
}
#endif
