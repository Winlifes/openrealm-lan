#ifndef BZ_WC3_SOUND_ASSETS_H
#define BZ_WC3_SOUND_ASSETS_H

#include "common/stb_slk.h"

/* Reforged SoundInfo rows shared by game and renderer; parsed strings belong to their loading module. */
typedef struct { cstring_t name, Filepath; } wc3SoundAsset_t;
/* Borrowed catalog view; owners release rows with the shared schema on metadata replacement. */
typedef struct { cstring_t name, filename; ARRAY(wc3SoundAsset_t, rows); } wc3SoundAssetView_t;
/* One lookup accumulates matches across catalogs without owning the resulting string. */
typedef struct { cstring_t name, path; bool found; } wc3SoundAssetQuery_t;
/* Owners report these authored-data errors through their own bounded diagnostic path. */
typedef enum {
    WC3_SOUND_ASSET_OK,
    WC3_SOUND_ASSET_MISSING_PATH,
    WC3_SOUND_ASSET_CONFLICT,
} wc3SoundAssetError_t;

static slkField_t const wc3_sound_asset_schema[] = {
    { "",         offsetof(wc3SoundAsset_t, name),     STB_SLK_STR },
    { "Filepath", offsetof(wc3SoundAsset_t, Filepath), STB_SLK_STR },
    { NULL, 0, 0 }
};

#define BZ_WC3_SOUND_ASSET_TABLES(M) \
    M(DialogueHumanBase) \
    M(DialogueOrcBase) \
    M(DialogueUndeadBase) \
    M(DialogueNightElfBase) \
    M(DialogueCreepsBase) \
    M(DialogueDemonBase) \
    M(DialogueNagaBase) \
    M(SoundAssetCombat)
#define BZ_WC3_SOUND_ASSET_PATH(table) "UI\\SoundInfo\\" #table ".slk"

/* Pure lookup shared by game and renderer; each owns its parsed rows and diagnostics.
 * A query spans all catalogs so contradictory mappings cannot select an arbitrary file. */
static inline wc3SoundAssetError_t WC3_ResolveSoundAssetView(wc3SoundAssetView_t const *view,
                                                           wc3SoundAssetQuery_t *query) {
    FOR_LOOP(i, ARRAY_COUNT(view->rows)) {
        wc3SoundAsset_t const *row = view->rows + i;
        if (!row->name || strcmp(row->name, query->name)) continue;
        if (!row->Filepath || !*row->Filepath || !strcmp(row->Filepath, "_")) return WC3_SOUND_ASSET_MISSING_PATH;
        if (query->found && strcmp(query->path, row->Filepath)) return WC3_SOUND_ASSET_CONFLICT;
        query->path = row->Filepath; query->found = true;
    }
    return WC3_SOUND_ASSET_OK;
}

#endif
