/* Manual saves use the engine's per-user catalog and the original LoadSavedGameScreen FDF. */
#include "../menu_screen.h"
#include "../generated/load_saved_game.h"
#include "../generated/map_list_box.h"

#define BZ_SAVE_LIST_BYTES 32768 // bytes; bounds the double-NUL manual-save catalog and menu memory
#define BZ_SAVE_LIST_ROWS 14 // rows; same stock list-widget density as the neighboring map selector

static LoadSavedGame_t saved;
static MapListBox_t save_list;
static uiMapListState_t files;
static cstring_t const saved_left[] = { "GameSettingsPanel", NULL };

/* Names enter a quoted console command; accept spaces/UTF-8 while rejecting paths and command delimiters. */
static bool SavedGame_NameSafe(cstring_t name) {
    if (!name || !*name || strlen(name) >= sizeof(files.items[0].path)) return false;
    for (unsigned char const *p = (unsigned char const *)name; *p; p++)
        if (*p < 32 || *p == 127 || strchr("/\\\";", *p)) return false;
    return true;
}

static bool SavedGame_LoadFrames(void) {
    return LoadSavedGame_Load(&saved);
}

/* Rebind cached children after menu reload, then fill the engine-owned newest-first save list. */
static void SavedGame_Init(void) {
    MapListBox_t source;
    char names[BZ_SAVE_LIST_BYTES] = {0};
    if (!MapListBox_Load(&source)) {
        fprintf(stderr, "UI: saved-game list template is unavailable\n");
        return;
    }
    frameDef_t *root = UI_FindChildFrame(saved.FileListFrame, "MapListBox");
    if (!root) root = UI_CloneFrameTree(source.MapListBox, saved.FileListFrame);
    if (!MapListBox_Bind(&save_list, root)) return;
    UI_SetPoint(root, FRAMEPOINT_TOPLEFT, saved.FileListFrame, FRAMEPOINT_TOPLEFT, 0, 0);
    UI_SetPoint(root, FRAMEPOINT_BOTTOMRIGHT, saved.FileListFrame, FRAMEPOINT_BOTTOMRIGHT, 0, 0);
    memset(&files, 0, sizeof(files));
    mi.ListSaves(names, sizeof(names));
    for (cstring_t name = names; *name && files.count < UI_MAX_MAP_LIST_ITEMS; name += strlen(name) + 1) {
        if (!SavedGame_NameSafe(name)) {
            fprintf(stderr, "UI: unsafe saved-game basename omitted\n");
            continue;
        }
        uiMapListItem_t *item = &files.items[files.count++];
        snprintf(item->path, sizeof(item->path), "%s", name);
        snprintf(item->name, sizeof(item->name), "%s", name);
    }
    UI_BindMapList(root, &files, saved.LoadSavedGameTitle, BZ_SAVE_LIST_ROWS, "menu_saved_select %u");
    UI_SetOnClick(saved.OKButton, "menu_saved_load");
    UI_SetOnClick(saved.CancelButton, "menu_game");
    saved.OKButton->disabled = files.count == 0;
}

void SavedGame_Select(uint32_t index) {
    if (UI_GetCurrentScreen() == &loadSavedGameScreen && index < files.count) files.selected = index;
}

/* The ordinary load command validates the save header/version and restores its map and game state. */
void SavedGame_LoadSelected(void) {
    char command[CMDARG_LEN * 2];
    if (UI_GetCurrentScreen() != &loadSavedGameScreen || files.selected >= files.count) return;
    snprintf(command, sizeof(command), "load \"%s\"\n", files.items[files.selected].path);
    mi.Cmd_ExecuteText(command);
}

static void SavedGame_Draw(void) { UI_DrawFrame(saved.LoadSavedGameScreen); }
static void SavedGame_Key(int key, bool down) {
    if (!down) return;
    if (key == 27) M_ShowSinglePlayerMenu();
    else if (key == 13) SavedGame_LoadSelected();
}

uiScreen_t loadSavedGameScreen = {
    .name = "load-saved",
    .left = saved_left,
    .glue = { .panel = UI_GLUE_SINGLE_PLAYER },
    .load = SavedGame_LoadFrames,
    .init = SavedGame_Init,
    .draw = SavedGame_Draw,
    .key_event = SavedGame_Key,
};
