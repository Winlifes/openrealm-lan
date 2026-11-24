/*
 * cl_main.c — Main client loop and initialization.
 *
 * The client is responsible for three things:
 *   1. Capturing user input and forwarding commands to the server.
 *   2. Receiving game state snapshots from the server and applying them.
 *   3. Preparing and rendering the scene each frame.
 *
 * CL_Frame() is the entry point called from the platform event loop.
 * CL_Init() sets up the renderer and input bindings at startup.
 */
#ifdef _WIN32
#include <winsock2.h>
/* These SDK aliases would rename renderer/menu API members in the unity build. */
#undef DrawText
#undef PlaySound
#else
#include <arpa/inet.h>
#endif
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "client.h"
#include "cl_control_groups.h"
#include "cl_input_local.h"
#include "../common/video_modes.h"
#include "tr_public.h"
#include "ui_layout.h"
#include "sound/s_local.h"
#include "common/server_api.h"
#include "common/online.h"
#ifdef BZ_TESTS
#include "shared/test.h"
#endif

refExport_t re;
menuExport_t menu;

struct client_static cls;
struct client_state cl;

#define CL_TIMEOUT_MSEC 10000
#define CL_TIMEOUT_CHECK_FRAMES 6 // consecutive expired frames; platform receive callbacks may follow the first resumed frame
#define CL_LOADING_PUMP_MSEC 16 // milliseconds; one 60 Hz platform pump; bounds checkpoint overhead during loading
#define CL_SIGNON_RETRY_MSEC 1000 // milliseconds; repeat the current paged request until its continuation arrives
#define CL_PING_MAX_AGE_MSEC 5000

typedef struct { cstring_t name; xcommand_t call; } clMenuCommand_t;
static clMenuCommand_t cl_menu_commands[128];
typedef enum { CL_MENU_UNLOADED, CL_MENU_READY, CL_MENU_SUSPENDED } clMenuLife_t;
static clMenuLife_t cl_menu_life;
typedef enum { CL_SIGNON_NONE, CL_SIGNON_NEW, CL_SIGNON_CONFIGSTRINGS, CL_SIGNON_BASELINES, CL_SIGNON_COMPLETE } clSignonStage_t;
typedef struct {
    clSignonStage_t stage;
    uint32_t index, sent_time;
    bool sent;
    char command[32];
} clSignonRequest_t;
static clSignonRequest_t cl_signon;

static uint32_t cl_last_packet_time = 0;
static uint32_t cl_realtime = 0;
static uint32_t cl_timeout_checks;
static uint32_t cl_connect_started, cl_connect_retry;

uint32_t CL_RealTime(void) { return cl_realtime; }

static void CL_RecordPacketSend(uint32_t sequence, uint32_t now) {
    size_t slot = sequence % (sizeof(cls.net_ping.sent) / sizeof(*cls.net_ping.sent));
    cls.net_ping.sent[slot].sequence = sequence;
    cls.net_ping.sent[slot].sent_at = now;
}

static void CL_RecordPacketAck(uint32_t sequence, uint32_t now) {
    size_t slot = sequence % (sizeof(cls.net_ping.sent) / sizeof(*cls.net_ping.sent));
    if (!sequence || cls.net_ping.sent[slot].sequence != sequence) return;
    unsigned elapsed = now - cls.net_ping.sent[slot].sent_at;
    cls.net_ping.sent[slot].sequence = 0;
    if (elapsed > CL_PING_MAX_AGE_MSEC) return;
    if (cls.net_ping.valid && now - cls.net_ping.updated_at <= CL_PING_MAX_AGE_MSEC)
        elapsed = (3 * cls.net_ping.msec + elapsed + 2) / 4;
    cls.net_ping.msec = elapsed;
    cls.net_ping.updated_at = now;
    cls.net_ping.valid = true;
}

int CL_NetworkPing(void) {
    if (!cls.net_ping.valid || SDL_GetTicks() - cls.net_ping.updated_at > CL_PING_MAX_AGE_MSEC)
        return -1;
    return (int)cls.net_ping.msec;
}

static void CL_ResetNetchan(void) {
    Netchan_Reset(&cls.netchan);
    memset(&cls.net_ping, 0, sizeof(cls.net_ping));
}

typedef enum {
    CL_MENU_ACTION_NONE,
    CL_MENU_ACTION_MAP,
    CL_MENU_ACTION_LEVEL,
    CL_MENU_ACTION_MENU,
    CL_MENU_ACTION_LOAD,
    CL_MENU_ACTION_SCRIPT_LOAD,
    CL_MENU_ACTION_QUIT,
} clMenuActionType_t;

typedef struct {
    clMenuActionType_t type;
    PATHSTR arg;
} clPendingMenuAction_t;

static clPendingMenuAction_t cl_pending_menu_action;
static clPendingMenuAction_t cl_movie_deferred_action;
/* Queued in authored order: scripts can request a movie followed by a model
 * scene (or vice versa) before the same deferred map transition. */
#define CL_MAX_QUEUED_CINEMATICS 8 // scenes; bounds deferred model and movie presentation until transition completion
static struct {
    bool model;
    PATHSTR path;
} cl_cinematic_queue[CL_MAX_QUEUED_CINEMATICS];
static uint32_t cl_cinematic_queue_count;
static bool cl_cinematic_paused;
static bool cl_cinematic_previous_pause;

void Cmd_ForwardToServer(cstring_t text) {
    if (cls.state <= ca_connected || *text == '-' || *text == '+') {
        fprintf(stderr, "Unknown command \"%s\"\n", text);
        CON_printf("Unknown command \"%s\"", text);
        return;
    }
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "%s", text);
}

cstring_t CL_GetConfigString(uint32_t index) {
    return cl.configstrings[index];
}

void CL_ClientCommand(cstring_t cmd) {
    memset(cls.netchan.message.data, 0, cls.netchan.message.maxsize);
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "%s", cmd);
}

void CL_ClearState(void) {
    S_StopAllSounds();
    S_ClearSoundEvents();
    CL_MusicReset();
    CL_ClearTEnts ();
    CL_WindowClear();
    CL_ClearMinimap();

    SAFE_DELETE(cl.loading.data, MemFree);
    SAFE_DELETE(cl.fow.visible, MemFree);
    SAFE_DELETE(cl.fow.explored, MemFree);
    SAFE_DELETE(cl.fow.texture, MemFree);
    SAFE_DELETE(cl.terrain_mask.cells, MemFree);
    SAFE_DELETE(cl.minimap_model, re.ReleaseModel);
    SAFE_DELETE(cl.moveConfirmation, re.ReleaseModel);
    FOR_LOOP(model, MAX_MODELS) {
        SAFE_DELETE(cl.models[model], re.ReleaseModel);
        SAFE_DELETE(cl.portraits[model], re.ReleaseModel);
    }
    FOR_LOOP(image, MAX_IMAGES) {
        if (cl.pics[image]) {
            re.ReleaseTexture((texture_t *)cl.pics[image]);
            cl.pics[image] = NULL;
        }
    }
    FOR_LOOP(image, MAX_DYNAMIC_IMAGES) {
        SAFE_DELETE(cl.dynamicPics[image], re.ReleaseTexture);
    }
    SCR_ClearLayoutResources();

    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        SCR_ClearLayoutLayer(layer);
    }

    /* Release per-map model ownership before advancing the renderer's
     * registration sequence, and clear any map-archive asset scope so
     * menus cannot inherit the previous level's imported overrides. */
    re.RegisterMap(NULL);

    memset(&cl, 0, sizeof(struct client_state));
    CL_ControlGroupsReset();
    CL_ResetFrameHistory();

    SZ_Clear (&cls.netchan.message);
    /* A cleared session starts a new netchan session too. On client_connect the server has just restarted the slot's
     * netchan (SV_Map rebuilds lobby slots, SV_DirectConnect a same-port reconnect); keeping the old sequence numbers
     * made each side drop the other's packets as stale and deadlocked the UDP lobby-to-game start. */
    CL_ResetNetchan();
    cl_timeout_checks = 0;
    memset(&cl_signon, 0, sizeof(cl_signon));
}

/* Forward declarations for UI callbacks */
static void CL_UIServerCommand(cstring_t text);
static void CL_LANRefreshServers(void);
static uint32_t CL_LANNumServers(void);
static bool CL_LANServer(uint32_t index, menuLanGame_t *out);
static void CL_LANConnectServer(uint32_t index);
static refExport_t *CL_UIGetRenderer(void);
/* This uses the same deferred, map-aware path as an in-game named load. */
static void CL_UI_LoadSavedGame(cstring_t name) {
    MenuAction("load", name);
}

static void CL_SuspendMenu(void) {
    if (cl_menu_life != CL_MENU_READY) return;
    menu.Shutdown();
    cl_menu_life = CL_MENU_SUSPENDED;
}

static void CL_ResumeMenu(void) {
    if (cl_menu_life != CL_MENU_SUSPENDED) return;
    menu.Init();
    cl_menu_life = CL_MENU_READY;
}

static void CL_RunMenuCommand(void) {
    if (!CL_MenuActive()) {
        fprintf(stderr, "Menu command %s rejected while a world owns presentation\n", Cmd_Argv(0));
        return;
    }
    FOR_LOOP(i, sizeof(cl_menu_commands) / sizeof(cl_menu_commands[0])) {
        if (cl_menu_commands[i].name && !strcmp(cl_menu_commands[i].name, Cmd_Argv(0))) {
            cl_menu_commands[i].call();
            return;
        }
    }
}

static void CL_AddMenuCommand(cstring_t name, xcommand_t call) {
    FOR_LOOP(i, sizeof(cl_menu_commands) / sizeof(cl_menu_commands[0])) {
        clMenuCommand_t *cmd = &cl_menu_commands[i];
        if (cmd->name && strcmp(cmd->name, name)) continue;
        if (!cmd->name) Cmd_AddCommand(name, CL_RunMenuCommand);
        *cmd = (clMenuCommand_t){ .name = name, .call = call };
        return;
    }
    Com_Error(ERR_FATAL, "Too many main-menu commands");
}

static void CL_MenuCommand(cstring_t command) {
    if (!command || !*command) {
        return;
    }
    Cbuf_AddText(command);
    Cbuf_AddText("\n");
}

/* Q2 CL_Disconnect: always tell the server, even a local one about to shut down; the server
 * discards a dead session's loopback datagrams when it next initializes. */
static void CL_SendDisconnect(void) {
    if (cls.state < ca_connected) return;
    SZ_Clear(&cls.netchan.message);
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    MSG_WriteString(&cls.netchan.message, "disconnect");
    Netchan_Transmit(NS_CLIENT, &cls.netchan);
}

static void CL_CancelCinematicSession(void);

static void CL_DisconnectInternal(cstring_t reason, bool notify, bool queue_menu) {
    Sys_UpdateLoadingPresentation(false, 1.0f, true);
    CL_CancelCinematicSession();
    Cbuf_ClearDefer();
    if (cls.state == ca_disconnected) {
        return;
    }

    fprintf(stderr, "CL_Disconnect: %s\n", reason && *reason ? reason : "disconnected");

    CL_SendDisconnect();
    /* Flush the disconnect packet before closing its transport. Browser
     * WebSocket close drains pending writes and lets the room bridge drop the
     * remote endpoint even when the page is immediately navigated away. */
    NET_ConfigSource(NS_CLIENT, false);

    if (cls.netchan.remote_address.type == NA_EOS) Online_Leave();
    CL_ClearState();
    cls.state = ca_disconnected;
    cl_last_packet_time = 0;
    CL_SetMenuBindings();

    if (!queue_menu) {
        return;
    }
    Cvar_Set("map", "");
    CL_ResumeMenu();
    if (notify) {
        CL_MenuCommand("menu_disconnected");
    } else {
        CL_MenuCommand("menu_main");
    }
}

void CL_UseLoopback(void) {
    if (cls.netchan.remote_address.type == NA_LOOPBACK) return;
    CL_DisconnectInternal("Starting a local game.", false, false);
    cls.netchan.remote_address = (netadr_t){ .type = NA_LOOPBACK };
}

static void CL_SessionEnded(void) {
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (typeof Module.onSessionEnded === 'function') Module.onSessionEnded(); });
#endif
}

void CL_Disconnect(cstring_t reason, bool notify) {
    CL_DisconnectInternal(reason, notify, true);
    CL_SessionEnded();
}

/* UI library FS_ReadFile wrapper — tries engine filesystem first,
 * then falls back to a raw CWD fopen so share/ files written by
 * CL_UI_WriteFile (which writes relative to CWD) are readable. */
static int CL_UI_ReadFile(cstring_t fileName, void **buf) {
    int size = FS_ReadFileQ3(fileName, buf);
    if (size > 0 || !buf) return size;
    FILE *f = fopen(fileName, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return -1; }
    *buf = MemAlloc((size_t)len + 1);
    if (!*buf) { fclose(f); return -1; }
    if (fread(*buf, 1, (size_t)len, f) != (size_t)len) {
        MemFree(*buf);
        *buf = NULL;
        fclose(f);
        return -1;
    }
    ((char *)*buf)[len] = '\0';
    fclose(f);
    return (int)len;
}

/* Write a local file by path (relative to CWD, same as share/ configs). */
static void CL_UI_WriteFile(cstring_t path, void const *data, int size) {
    FILE *f;
    if (!path || !data || size <= 0) return;
    f = fopen(path, "wb");
    if (!f) return;
    fwrite(data, 1, (size_t)size, f);
    fclose(f);
}

static bool CL_UI_HasExtension(cstring_t name, cstring_t extension) {
    cstring_t dot;

    if (!extension || !*extension) {
        return true;
    }
    dot = strrchr(name, '.');
    return dot && !strcasecmp(dot, extension);
}

static int CL_UI_CompareFileNames(void const *a, void const *b) {
    return strcasecmp((cstring_t)a, (cstring_t)b);
}

static bool CL_UI_ListHasFile(PATHSTR *files, int count, cstring_t name) {
    for (int i = 0; i < count; i++) {
        if (!strcasecmp(files[i], name)) {
            return true;
        }
    }
    return false;
}

typedef struct {
    cstring_t extension;
    char *listbuf;
    int bufsize;
    int used;
    int count;
} clUiFileList_t;

static void CL_UI_AddMapFile(cstring_t path, void *userData) {
    clUiFileList_t *list = userData;
    int len;

    if (!list || !CL_UI_HasExtension(path, list->extension)) {
        return;
    }
    len = (int)strlen(path) + 1;
    if (list->used + len < list->bufsize) {
        memcpy(list->listbuf + list->used, path, len);
        list->used += len;
    }
    list->count++;
}

static int CL_UI_GetFileList(cstring_t path, cstring_t extension, char *listbuf, int bufsize) {
    enum { MAX_UI_FILELIST = 1024 };
    PATHSTR files[MAX_UI_FILELIST];
    char mask[MAX_PATHLEN * 2];
    sfileFindData_t find;
    handle_t handle;
    int count = 0;
    int used = 0;

    if (!path || !*path || !listbuf || bufsize <= 0) {
        return 0;
    }
    listbuf[0] = '\0';
    if (!strcasecmp(path, "Maps")) {
        clUiFileList_t list = { extension, listbuf, bufsize, 0, 0 };

        FS_ListMaps(CL_UI_AddMapFile, &list);
        if (list.used < list.bufsize) {
            list.listbuf[list.used] = '\0';
        }
        return list.count;
    }
    snprintf(mask, sizeof(mask), "%s\\*", path);

    handle = FS_FindFirstFile(mask, &find);
    while (handle && count < MAX_UI_FILELIST) {
        if (CL_UI_HasExtension(find.cFileName, extension) &&
            !CL_UI_ListHasFile(files, count, find.cFileName)) {
            strlcpy(files[count], find.cFileName, sizeof(files[count]));
            for (char *p = files[count]; *p; p++) {
                if (*p == '/') {
                    *p = '\\';
                }
            }
            count++;
        }
        if (!FS_FindNextFile(handle, &find)) {
            break;
        }
    }
    if (handle) {
        FS_FindClose(handle);
    }

    qsort(files, count, sizeof(files[0]), CL_UI_CompareFileNames);
    for (int i = 0; i < count; i++) {
        int len = (int)strlen(files[i]) + 1;
        if (used + len >= bufsize) {
            break;
        }
        memcpy(listbuf + used, files[i], len);
        used += len;
    }
    if (used < bufsize) {
        listbuf[used] = '\0';
    }
    return count;
}

static void CL_UIServerCommand(cstring_t text) {
    if (!text || !*text || *text == '-' || *text == '+') {
        return;
    }
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "%s", text);
}

/* Renderer access callback for UI rendering */
static refExport_t *CL_UIGetRenderer(void) {
    return &re;
}

#define CL_MAX_LAN_SERVERS 64

static menuLanGame_t cl_lan_servers[CL_MAX_LAN_SERVERS];
static uint32_t cl_num_lan_servers;

static void CL_InfoValue(cstring_t info, cstring_t key, string_t out, uint32_t out_size) {
    char needle[64];
    cstring_t cursor;
    cstring_t value;
    cstring_t end;
    size_t len;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!info || !key || !*key) {
        return;
    }

    snprintf(needle, sizeof(needle), "\\%s\\", key);
    cursor = strstr(info, needle);
    if (!cursor) {
        return;
    }
    value = cursor + strlen(needle);
    end = strchr(value, '\\');
    len = end ? (size_t)(end - value) : strlen(value);
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, value, len);
    out[len] = '\0';
}

static void CL_LANRefreshServers(void) {
    netadr_t adr;
    unsigned short port = (unsigned short)Cvar_Integer("game_port", PORT_SERVER);
    bool const open_client_socket = !NET_IsConfigured(NS_CLIENT);

    memset(cl_lan_servers, 0, sizeof(cl_lan_servers));
    cl_num_lan_servers = 0;
    if (open_client_socket) {
        fprintf(stderr, "CL_LANRefreshServers: opening client UDP socket for LAN queries\n");
    }
    NET_ConfigSource(NS_CLIENT, true);
    if (!NET_IsConfigured(NS_CLIENT)) {
        fprintf(stderr, "CL_LANRefreshServers: client UDP socket is closed, cannot query LAN servers\n");
        return;
    }

    memset(&adr, 0, sizeof(adr));
    adr.type = NA_BROADCAST;
    adr.port = htons(port);
    Netchan_OutOfBandPrint(NS_CLIENT, adr, "info");
}

static uint32_t CL_LANNumServers(void) {
    return cl_num_lan_servers;
}

static bool CL_LANServer(uint32_t index, menuLanGame_t *out) {
    if (!out || index >= cl_num_lan_servers) {
        return false;
    }
    *out = cl_lan_servers[index];
    return true;
}

static void CL_LANConnectServer(uint32_t index) {
    menuLanGame_t *game;
    unsigned short port = (unsigned short)Cvar_Integer("game_port", PORT_SERVER);

    if (index >= cl_num_lan_servers) {
        return;
    }
    game = &cl_lan_servers[index];
    Cvar_Set("connect", game->address);
    CL_Connect(game->address, port);
}

static bool CL_OnlineGame(uint32_t index, menuLanGame_t *out) {
    onlineGame_t game;
    if (!out || !Online_Game(index, &game)) return false;
    /* The menu API owns its public structure; copy fields across that boundary. */
    snprintf(out->address, sizeof(out->address), "%s", game.address);
    snprintf(out->hostname, sizeof(out->hostname), "%s", game.hostname);
    snprintf(out->mapname, sizeof(out->mapname), "%s", game.mapname);
    out->players = game.players; out->maxPlayers = game.maxPlayers;
    out->speed = game.speed; out->slots = game.slots;
    return true;
}

static void CL_OnlineLeave(void) {
    bool const host = Online_IsHost();
    bool const connected = cls.state != ca_disconnected;
    /* Both network browsers use this session-boundary import. UDP and browser
     * bridge peers must release their endpoint just like Internet peers. */
    if (connected) CL_DisconnectInternal("Left game room.", false, false);
    if (host) SV_Shutdown();
    Online_Leave();
    if (connected) CL_SessionEnded();
}

static void CL_AddLANServer(netadr_t const *from, cstring_t info) {
    menuLanGame_t *game;
    char value[128];
    cstring_t address;

    if (!from || !info) {
        return;
    }
    address = NET_AdrToString(from);
    FOR_LOOP(i, cl_num_lan_servers) {
        if (!strcmp(cl_lan_servers[i].address, address)) {
            game = &cl_lan_servers[i];
            goto update;
        }
    }
    if (cl_num_lan_servers >= CL_MAX_LAN_SERVERS) {
        fprintf(stderr, "CL_AddLANServer: ignoring %s, LAN server list is full\n", address);
        return;
    }
    game = &cl_lan_servers[cl_num_lan_servers++];
    memset(game, 0, sizeof(*game));
    snprintf(game->address, sizeof(game->address), "%s", address);

update:
    CL_InfoValue(info, "hostname", game->hostname, sizeof(game->hostname));
    CL_InfoValue(info, "mapname", game->mapname, sizeof(game->mapname));
    CL_InfoValue(info, "players", value, sizeof(value));
    game->players = (uint32_t)atoi(value);
    CL_InfoValue(info, "maxplayers", value, sizeof(value));
    game->maxPlayers = (uint32_t)atoi(value);
    CL_InfoValue(info, "speed", value, sizeof(value));
    game->speed = (uint32_t)atoi(value);
    CL_InfoValue(info, "slots", value, sizeof(value));
    game->slots = (uint32_t)atoi(value);
    if (!game->hostname[0]) {
        snprintf(game->hostname, sizeof(game->hostname), "%s", "OpenWarcraft3");
    }
}

static void CL_UICvarSet(cstring_t name, cstring_t value) {
    Cvar_Set(name, value);
}

static void CL_UICvarRegister(cstring_t name, cstring_t default_value, uint32_t flags) {
    Cvar_Get(name, default_value, flags);
}

void CL_SetLoadingProgress(float progress) {
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    if (progress <= cl.loading_progress) return;
    cl.loading_progress = progress;
    SCR_UpdateLoadingPlaque();
}

void CL_BeginLoadingMap(cstring_t mapName) {
    cl.loading_progress = 0.0f;
    Sys_UpdateLoadingPresentation(true, cl.loading_progress, false);
    /* Release glue-owned caches before the renderer registers a different world. */
    CL_SuspendMenu();
    /* Per-map input conveniences must never retain entity numbers into the
     * next world, where those numbers may refer to unrelated entities. */
    CL_ResetInput();
    CL_ControlGroupsReset();
    /* Publish the resolved map before freezing the plaque; menu launches have no startup map cvar. */
    Cvar_Set("map", mapName);
    /* Register the world with the renderer so the map's terrain and assets are built.
     * Without this call the ground layers stay empty and the world renders black. */
    if (mapName && *mapName) re.RegisterMap(mapName);
    /* Same-map load keeps CS_WORLD unchanged; forget the previous world's begin
     * so PrepRefresh sends it again and CL_ParseFrame can end the plaque. */
    CL_RestartRefresh();
    CL_MapLoadDebugBegin();
    cl.precache_ready = false;
    cl.playerstate.client_ui_state = CLIENT_UI_LOADING;
    cls.state = ca_connected;
    CL_SetGameplayInput();
    SCR_BeginLoadingPlaque();
    /* New map baselines repopulate the compact active-entity list; drop any
     * stale entries from the previous map before they arrive. */
    cl.num_active = cl.num_modelless = 0;
    CL_ResetFrameHistory();
}

int CL_ModelIndex(cstring_t modelName) {
    /* Find or register model */
    for (uint32_t i = 1; i < MAX_MODELS; i++) {
        if (cl.models[i] == NULL) {
            cl.models[i] = re.LoadModel(modelName);
            return (int)i;
        }
        /* Check if already loaded - compare configstring */
        if (*cl.configstrings[CS_MODELS + i] && !strcmp(cl.configstrings[CS_MODELS + i], modelName)) {
            return (int)i;
        }
    }
    return 0;
}

int CL_ImageIndex(cstring_t imageName) {
    /* Find or register image */
    for (uint32_t i = 1; i < MAX_IMAGES; i++) {
        if (cl.pics[i] == NULL) {
            cl.pics[i] = re.LoadTexture(imageName);
            return (int)i;
        }
        /* Check if already loaded */
        if (*cl.configstrings[CS_IMAGES + i] && !strcmp(cl.configstrings[CS_IMAGES + i], imageName)) {
            return (int)i;
        }
    }
    return 0;
}

int CL_FontIndex(cstring_t fontName, uint32_t fontSize) {
    /* Create font spec string */
    char fontspec[256];
    snprintf(fontspec, sizeof(fontspec), "%s,%u", fontName, fontSize);
    
    /* Find or register font */
    for (uint32_t i = 1; i < MAX_FONTSTYLES; i++) {
        if (cl.fonts[i] == NULL) {
            cl.fonts[i] = re.LoadFont(fontName, fontSize);
            return (int)i;
        }
        /* Check if already loaded */
        if (*cl.configstrings[CS_FONTS + i] && !strcmp(cl.configstrings[CS_FONTS + i], fontspec)) {
            return (int)i;
        }
    }
    return 0;
}

void CL_UIMenuCommand(cstring_t command) {
    CON_printf("CL_UIMenuCommand: %s\\n", command);
    CL_MenuCommand(command);
}

static videoMode_t CL_VideoMode(void) { return *video_mode_get(Cvar_Integer("vid_mode", BZ_VIDEO_MODE_DEFAULT)); }

static void CL_VideoApply_f(void) {
    videoMode_t mode = CL_VideoMode();

    if (re.SetWindowSize) {
        re.SetWindowSize(mode.width, mode.height);
    }
    /* Mode changes resize the window without waiting for the SDL event to be polled. */
    CL_CanvasWindowChanged();
}

static cstring_t CL_RebuildMenuTarget(cstring_t target) {
    return target && *target ? target : "menu_main";
}

static void CL_RebuildMenu(cstring_t target) {
    Cvar_Set("map", "");
    CL_SuspendMenu();
    re.RegisterMap(NULL);
    /* An edition switch changes which archives are visible, and with them the authored widescreen chrome. */
    CL_CanvasResolvePolicy();
    CL_ResumeMenu();

    /* M_Init deliberately installs no disconnected glue screen.  Every
     * rebuild therefore has to select its destination explicitly, including
     * menu_main.  Skipping the main-menu command leaves a valid but empty UI
     * after menu_restart (for example when switching RoC/TFT editions). */
    CL_MenuCommand(CL_RebuildMenuTarget(target));
}

static void CL_MenuRestart_f(void) {
    if (cls.state != ca_disconnected) {
        return;
    }
    CL_RebuildMenu("menu_main");
}

static void CL_Quit_f(void) {
    /* Com_Quit owns client teardown; repeating it destroys the WebGL context before the second GL cleanup. */
    Com_Quit();
}

/* Game modules request session-boundary actions through gi.MenuAction while
 * their simulation/JASS call stack may still be active.  Never tear down or
 * replace the current world inline from that callback.  Copy the request and
 * execute it from the following client frame, after SV_Frame has returned. */
void MenuAction(cstring_t action, cstring_t arg) {
    clPendingMenuAction_t pending = { 0 };

    if (!action || !*action) return;
    if (!strcmp(action, "map")) {
        if (!arg || !*arg) return;
        if (!Com_ResolveMapArgument(arg, pending.arg, sizeof(pending.arg))) return;
        pending.type = CL_MENU_ACTION_MAP;
    } else if (!strcmp(action, "level")) {
        if (!arg || !*arg) return;
        if (!Com_ResolveMapArgument(arg, pending.arg, sizeof(pending.arg))) return;
        pending.type = CL_MENU_ACTION_LEVEL;
    } else if (!strcmp(action, "menu")) {
        pending.type = CL_MENU_ACTION_MENU;
        if (arg && *arg) strlcpy(pending.arg, arg, sizeof(pending.arg));
    } else if (!strcmp(action, "load") || !strcmp(action, "scriptload")) {
        if (!arg || !*arg || strlen(arg) >= sizeof(pending.arg)) return;
        pending.type = !strcmp(action, "scriptload") ? CL_MENU_ACTION_SCRIPT_LOAD : CL_MENU_ACTION_LOAD;
        strlcpy(pending.arg, arg, sizeof(pending.arg));
    } else if (!strcmp(action, "quit")) {
        pending.type = CL_MENU_ACTION_QUIT;
    } else {
        return;
    }

    if (cl_pending_menu_action.type != CL_MENU_ACTION_NONE) {
        fprintf(stderr, "MenuAction: replacing pending session action %u with %u\n",
                (unsigned)cl_pending_menu_action.type, (unsigned)pending.type);
    }
    cl_pending_menu_action = pending;
}

static void CL_QueueCinematic(cstring_t path, bool model) {
    if (!path || !*path) return;
    if (cl_cinematic_queue_count >= CL_MAX_QUEUED_CINEMATICS) {
        fprintf(stderr, "CL_QueueCinematic: queue full, rejecting %s\n", path);
        return;
    }
    cl_cinematic_queue[cl_cinematic_queue_count].model = model;
    snprintf(cl_cinematic_queue[cl_cinematic_queue_count].path,
             sizeof(cl_cinematic_queue[cl_cinematic_queue_count].path), "%s", path);
    cl_cinematic_queue_count++;
}

void CL_QueueMovie(cstring_t path) { CL_QueueCinematic(path, false); }
void CL_QueueModelCinematic(cstring_t path) { CL_QueueCinematic(path, true); }

/* The client owns the session pause and deferred transition. A presentation
 * renderer may only start/stop its own asset, never change the game session. */
static void CL_CancelCinematicSession(void) {
    CL_MovieCancel();
    cl_cinematic_queue_count = 0;
    memset(&cl_movie_deferred_action, 0, sizeof(cl_movie_deferred_action));
    if (cl_cinematic_paused && SV_IsActive())
        SV_SetPaused(cl_cinematic_previous_pause);
    cl_cinematic_paused = false;
    cl_cinematic_previous_pause = false;
}

static bool CL_HasModelCinematic(void) {
    FOR_LOOP(i, cl_cinematic_queue_count)
        if (cl_cinematic_queue[i].model) return true;
    return false;
}

static bool CL_CanStartCinematic(bool transition_pending) {
    /* Model scenes may run standalone. Movie files are transition interposers
     * and must not interrupt the map's remaining scripted ending. */
    return transition_pending || CL_HasModelCinematic();
}

/* A model ending scene precedes an unlock movie queued earlier by
 * SetCinematicAvailableBJ. Preserve FIFO order within each presentation type. */
static bool CL_PopCinematic(PATHSTR path, bool *model, bool allow_movie) {
    uint32_t index = cl_cinematic_queue_count;
    FOR_LOOP(i, cl_cinematic_queue_count)
        if (cl_cinematic_queue[i].model) { index = i; break; }
    if (index == cl_cinematic_queue_count) {
        if (!allow_movie || !cl_cinematic_queue_count) return false;
        index = 0;
    }
    *model = cl_cinematic_queue[index].model;
    snprintf(path, sizeof(PATHSTR), "%s", cl_cinematic_queue[index].path);
    cl_cinematic_queue_count--;
    memmove(cl_cinematic_queue + index, cl_cinematic_queue + index + 1,
            (cl_cinematic_queue_count - index) * sizeof(cl_cinematic_queue[0]));
    return true;
}

/* Missing files are skipped, not allowed to hold a queued victory indefinitely. */
static bool CL_StartNextCinematic(bool allow_movie) {
    PATHSTR path;
    bool model;
    while (CL_PopCinematic(path, &model, allow_movie)) {
        if (model ? CL_PlayModelCinematic(path) : CL_PlayMovie(path)) return true;
    }
    return false;
}

#ifdef BZ_TESTS
TEST(client_cinematic, ending_model_precedes_unlock_movies_and_movies_wait_for_transition) {
    PATHSTR path;
    bool model;
    cl_cinematic_queue_count = 0;
    CL_QueueMovie("Movies\\OutroX.mpq");
    T_ASSERT(!CL_CanStartCinematic(false));
    CL_QueueModelCinematic("Doodads\\Cinematic\\ArthasIllidanFight\\ArthasIllidanFight.mdl");
    T_ASSERT(CL_CanStartCinematic(false));
    CL_QueueMovie("Movies\\later.mpq");
    T_ASSERT(CL_PopCinematic(path, &model, false));
    T_ASSERT(model);
    T_STREQ(path, "Doodads\\Cinematic\\ArthasIllidanFight\\ArthasIllidanFight.mdl");
    T_ASSERT(!CL_PopCinematic(path, &model, false));
    T_EQ(cl_cinematic_queue_count, 2u);
    T_ASSERT(CL_CanStartCinematic(true));
    T_ASSERT(CL_PopCinematic(path, &model, true));
    T_ASSERT(!model);
    T_STREQ(path, "Movies\\OutroX.mpq");
    T_ASSERT(CL_PopCinematic(path, &model, true));
    T_STREQ(path, "Movies\\later.mpq");
    T_ASSERT(!CL_PopCinematic(path, &model, true));
}

TEST(client_cinematic, queue_capacity_never_discards_existing_scenes) {
    PATHSTR path;
    bool model;
    cl_cinematic_queue_count = 0;
    FOR_LOOP(i, CL_MAX_QUEUED_CINEMATICS) CL_QueueMovie("movie.mpq");
    CL_QueueModelCinematic("overflow.mdl");
    T_EQ(cl_cinematic_queue_count, (uint32_t)CL_MAX_QUEUED_CINEMATICS);
    FOR_LOOP(i, CL_MAX_QUEUED_CINEMATICS) {
        T_ASSERT(CL_PopCinematic(path, &model, true));
        T_STREQ(path, "movie.mpq");
        T_ASSERT(!model);
    }
    T_ASSERT(!CL_PopCinematic(path, &model, true));
}
#endif

static void CL_ProcessPendingMenuAction(void) {
    clPendingMenuAction_t pending;

    if (CL_MovieActive()) return;
    if (cl_cinematic_paused) {
        /* The previous scene completed/skipped. Keep the simulation frozen
         * while the next queued scene starts; execute transition only at EOF. */
        bool transition_pending = cl_movie_deferred_action.type != CL_MENU_ACTION_NONE;
        if (CL_CanStartCinematic(transition_pending) &&
            CL_StartNextCinematic(transition_pending)) return;
        pending = cl_movie_deferred_action;
        memset(&cl_movie_deferred_action, 0, sizeof(cl_movie_deferred_action));
        cl_cinematic_paused = false;
        if (SV_IsActive()) SV_SetPaused(cl_cinematic_previous_pause);
        cl_cinematic_previous_pause = false;
        if (pending.type != CL_MENU_ACTION_NONE) goto execute;
    }
    if (!CL_CanStartCinematic(cl_pending_menu_action.type != CL_MENU_ACTION_NONE)) return;

    pending = cl_pending_menu_action;
    memset(&cl_pending_menu_action, 0, sizeof(cl_pending_menu_action));
    if (cl_cinematic_queue_count) {
        cl_movie_deferred_action = pending;
        cl_cinematic_paused = true;
        cl_cinematic_previous_pause = SV_IsActive() && Cvar_Integer("paused", 0) != 0;
        if (SV_IsActive()) SV_SetPaused(true);
        if (CL_StartNextCinematic(pending.type != CL_MENU_ACTION_NONE)) return;
        /* Every queued asset failed. Restore the original scheduler state and
         * perform the authored transition rather than freezing the campaign. */
        if (SV_IsActive()) SV_SetPaused(cl_cinematic_previous_pause);
        cl_cinematic_previous_pause = false;
        cl_cinematic_paused = false;
        pending = cl_movie_deferred_action;
        memset(&cl_movie_deferred_action, 0, sizeof(cl_movie_deferred_action));
    }
execute:
    switch (pending.type) {
    case CL_MENU_ACTION_MAP:
        /* A map selected by the user starts a fresh untrusted-map save scope.
         * JASS ChangeLevel uses "level" to retain its current campaign scope. */
        FS_ResetScriptSaveScope();
        /* fallthrough */
    case CL_MENU_ACTION_LEVEL:
        CL_SetGameplayBindings();
        CL_BeginLoadingMap(pending.arg);
        SV_Map(pending.arg);
        if (SV_IsActive()) CL_SetLoadingProgress(0.05f);
        break;
    case CL_MENU_ACTION_MENU:
        /* Returning from a world is a session boundary, not an in-place menu
         * navigation.  Shut the client/server world down first, then rebuild
         * the menu after clearing the map asset scope so FDF/model state is
         * valid again even after a runtime menu restart. */
        CL_DisconnectInternal("Game ended.", false, false);
        SV_Shutdown();
        /* Workspace cleanup now removes files: finish game shutdown first. */
        FS_ResetScriptSaveScope();
        CL_RebuildMenu(pending.arg[0] ? pending.arg : "menu_main");
        CL_SessionEnded();
        break;
    case CL_MENU_ACTION_LOAD: {
        PATHSTR map;
        /* Do not delete the running campaign workspace when the named save
         * is missing or has an unreadable map identity. Restoration itself
         * starts a fresh scope after this validation succeeds. */
        if (!SV_GetSaveMap(pending.arg, map, sizeof(map))) break;
        /* Only a player-selected manual save may reauthorize its original
         * private campaign storage. Missing/stale bindings leave a new scope. */
        FS_RestoreScriptSaveScope(pending.arg);
        CL_SetGameplayBindings();
        CL_BeginLoadingMap(map);
        if (SV_LoadGame(pending.arg, map)) CL_ControlGroupsLoad(pending.arg);
        break;
    }
    case CL_MENU_ACTION_SCRIPT_LOAD: {
        PATHSTR map;
        /* Unlike a player-selected load, retain the active campaign's private
         * script storage. Never forward these names to the manual-save path. */
        if (!SV_GetScriptSaveMap(pending.arg, map, sizeof(map))) break;
        CL_SetGameplayBindings();
        CL_BeginLoadingMap(map);
        if (!SV_LoadScriptGame(pending.arg, map))
            fprintf(stderr, "script load: failed to restore zone %s\n", pending.arg);
        break;
    }
    case CL_MENU_ACTION_QUIT:
        CL_Quit_f();
        break;
    case CL_MENU_ACTION_NONE:
        break;
    }
}

#ifdef BZ_TESTS
TEST(client_loading, progress_is_clamped_and_monotonic) {
    float saved = cl.loading_progress;
    uint32_t saved_disable_screen = cls.disable_screen;

    cls.disable_screen = 0;
    cl.loading_progress = 0.0f;
    CL_SetLoadingProgress(0.25f);
    T_FEQ(cl.loading_progress, 0.25f, 0.0001f);
    CL_SetLoadingProgress(0.10f);
    T_FEQ(cl.loading_progress, 0.25f, 0.0001f);
    CL_SetLoadingProgress(2.0f);
    T_FEQ(cl.loading_progress, 1.0f, 0.0001f);

    cl.loading_progress = saved;
    cls.disable_screen = saved_disable_screen;
}

static uint32_t cl_test_menu_calls;
static void CL_TestMenuCommand(void) { cl_test_menu_calls++; }
TEST(client_session, menu_commands_cannot_enter_loading_or_active_world) {
    connstate_t state = cls.state;
    uint32_t ui = cl.playerstate.client_ui_state;
    CL_AddMenuCommand("test_menu_boundary", CL_TestMenuCommand); cl_test_menu_calls = 0;
    cls.state = ca_active; cl.playerstate.client_ui_state = CLIENT_UI_GAME;
    Cmd_ExecuteString("test_menu_boundary"); T_EQ(cl_test_menu_calls, 0);
    cls.state = ca_connected; cl.playerstate.client_ui_state = CLIENT_UI_LOADING;
    Cmd_ExecuteString("test_menu_boundary"); T_EQ(cl_test_menu_calls, 0);
    cls.state = ca_disconnected; cl.playerstate.client_ui_state = CLIENT_UI_GAME;
    Cmd_ExecuteString("test_menu_boundary"); T_EQ(cl_test_menu_calls, 1);
    Cmd_RemoveCommand("test_menu_boundary");
    FOR_LOOP(i, sizeof(cl_menu_commands) / sizeof(cl_menu_commands[0]))
        if (cl_menu_commands[i].name && !strcmp(cl_menu_commands[i].name, "test_menu_boundary"))
            cl_menu_commands[i] = (clMenuCommand_t){0};
    cls.state = state; cl.playerstate.client_ui_state = ui;
}

TEST(client_session, menu_action_map_is_deferred_until_client_frame) {
    memset(&cl_pending_menu_action, 0, sizeof(cl_pending_menu_action));

    MenuAction("map", "Maps\\Campaign\\Human02.w3m");

    T_EQ(cl_pending_menu_action.type, CL_MENU_ACTION_MAP);
    T_STREQ(cl_pending_menu_action.arg, "Maps\\Campaign\\Human02.w3m");

    /* Do not call CL_ProcessPendingMenuAction here: the regression contract is
     * specifically that MenuAction itself cannot enter SV_Map re-entrantly. */
    memset(&cl_pending_menu_action, 0, sizeof(cl_pending_menu_action));
}

TEST(client_session, script_level_change_preserves_session_action_type) {
    memset(&cl_pending_menu_action, 0, sizeof(cl_pending_menu_action));
    MenuAction("level", "Maps\\Campaign\\Human02.w3m");
    T_EQ(cl_pending_menu_action.type, CL_MENU_ACTION_LEVEL);
    T_STREQ(cl_pending_menu_action.arg, "Maps\\Campaign\\Human02.w3m");
    memset(&cl_pending_menu_action, 0, sizeof(cl_pending_menu_action));
}

TEST(client_session, script_load_uses_distinct_deferred_action) {
    memset(&cl_pending_menu_action, 0, sizeof(cl_pending_menu_action));
    MenuAction("scriptload", "Blizzard/Zones/OrcX01_02.w3z");
    T_EQ(cl_pending_menu_action.type, CL_MENU_ACTION_SCRIPT_LOAD);
    T_STREQ(cl_pending_menu_action.arg, "Blizzard/Zones/OrcX01_02.w3z");
    /* The scriptload execution path must not call FS_ResetScriptSaveScope.
     * Do not invoke a real map load from the unit test. */
    memset(&cl_pending_menu_action, 0, sizeof(cl_pending_menu_action));
}

TEST(client_session, menu_action_named_load_is_deferred_until_client_frame) {
    memset(&cl_pending_menu_action, 0, sizeof(cl_pending_menu_action));

    MenuAction("load", "chapter-01");

    T_EQ(cl_pending_menu_action.type, CL_MENU_ACTION_LOAD);
    T_STREQ(cl_pending_menu_action.arg, "chapter-01");

    /* Loading rebuilds the server/map, so MenuAction must only capture the
     * selected save while the gameplay-window callback is still active. */
    memset(&cl_pending_menu_action, 0, sizeof(cl_pending_menu_action));
}

TEST(client_session, menu_action_menu_is_deferred_until_client_frame) {
    memset(&cl_pending_menu_action, 0, sizeof(cl_pending_menu_action));

    MenuAction("menu", "menu_main");

    T_EQ(cl_pending_menu_action.type, CL_MENU_ACTION_MENU);
    T_STREQ(cl_pending_menu_action.arg, "menu_main");

    /* Client-owned leave buttons use this path so they cannot rebuild FDF/menu
     * state while the gameplay window input callback is still on the stack. */
    memset(&cl_pending_menu_action, 0, sizeof(cl_pending_menu_action));
}

TEST(client_session, disconnect_always_tells_the_local_server_it_left) {
    struct client_static old_cls = cls;
    uint8_t packet_data[MAX_MSGLEN];
    sizeBuf_t packet = { .data = packet_data, .maxsize = sizeof(packet_data) };
    netadr_t from;

    memset(&cls, 0, sizeof(cls));
    while (NET_GetLoopPacket(NS_SERVER, &from, &packet)) {}
    SZ_Init(&cls.netchan.message, cls.netchan.message_buf, MAX_MSGLEN);
    cls.netchan.remote_address.type = NA_LOOPBACK;
    cls.state = ca_connected;

    /* Even when SV_Shutdown follows (mission exit), the stale packet is the server's to discard
     * (server_net.stale_loopback_disconnect_does_not_drop_next_local_session). */
    CL_SendDisconnect();
    T_ASSERT(NET_GetLoopPacket(NS_SERVER, &from, &packet) > 0);
    T_EQ(MSG_ReadByte(&packet), clc_stringcmd);
    T_STREQ(MSG_ReadString2(&packet), "disconnect");

    cls = old_cls;
}

static uint32_t cl_test_menu_shutdown_count;
static uint32_t cl_test_menu_init_count;
static uint32_t cl_test_register_map_count;
static bool cl_test_register_map_was_null;

static void CL_TestMenuShutdown(void) {
    cl_test_menu_shutdown_count++;
}

static void CL_TestMenuInit(void) {
    cl_test_menu_init_count++;
}

static void CL_TestRegisterMap(cstring_t map) {
    cl_test_register_map_count++;
    cl_test_register_map_was_null = map == NULL;
}

/* Dedicated test runs have no renderer; the menu rebuild re-resolves the canvas, which pushes its scene. */
static void CL_TestSetUIScene(rect_t const *scene) { (void)scene; }

TEST(client_session, leaving_a_lan_lobby_sends_disconnect_and_closes_transport) {
    struct client_state *old_cl = MemAlloc(sizeof(cl));
    struct client_static old_cls = cls;
    refExport_t old_renderer = re;
    uint32_t old_packet = cl_last_packet_time;
    bool const old_socket = NET_IsConfigured(NS_CLIENT);
    uint8_t bytes[MAX_MSGLEN];
    sizeBuf_t packet = { .data = bytes, .maxsize = sizeof(bytes) };
    netadr_t from;

    memcpy(old_cl, &cl, sizeof(cl)); memset(&cl, 0, sizeof(cl));
    memset(&cls, 0, sizeof(cls));
    re.RegisterMap = CL_TestRegisterMap;
    NET_ClearLoopPackets(NS_CLIENT); NET_ClearLoopPackets(NS_SERVER);
    NET_ConfigSource(NS_CLIENT, true);
    T_ASSERT(NET_IsConfigured(NS_CLIENT));
    SZ_Init(&cls.netchan.message, cls.netchan.message_buf, sizeof(cls.netchan.message_buf));
    cls.netchan.remote_address.type = NA_LOOPBACK;
    cls.state = ca_connected;

    /* The shared menu leave import is also used by the LAN browser. A LAN
     * peer has neither an EOS address nor an Internet host reservation. */
    CL_OnlineLeave();
    T_EQ(cls.state, ca_disconnected);
    T_ASSERT(!NET_IsConfigured(NS_CLIENT));
    T_ASSERT(NET_GetLoopPacket(NS_SERVER, &from, &packet) > 0);
    T_EQ(MSG_ReadByte(&packet), clc_stringcmd);
    T_STREQ(MSG_ReadString2(&packet), "disconnect");
    CL_OnlineLeave();
    T_EQ(NET_GetLoopPacket(NS_SERVER, &from, &packet), 0);

    NET_ClearLoopPackets(NS_CLIENT); NET_ClearLoopPackets(NS_SERVER);
    NET_ConfigSource(NS_CLIENT, old_socket);
    memcpy(&cl, old_cl, sizeof(cl)); MemFree(old_cl); cls = old_cls; re = old_renderer;
    cl_last_packet_time = old_packet;
}

TEST(client_session, menu_resources_suspend_once_and_resume_once) {
    clMenuLife_t old_life = cl_menu_life;
    struct client_state *old_cl = MemAlloc(sizeof(cl));
    struct client_static old_cls = cls;
    PATHSTR old_map;
    memcpy(old_cl, &cl, sizeof(cl)); memset(&cl, 0, sizeof(cl));
    snprintf(old_map, sizeof(old_map), "%s", Cvar_String("map", ""));
    cls.state = ca_disconnected; cls.disable_screen = 1;
    menuExport_t old_menu = menu;
    void (*old_register_map)(cstring_t)=re.RegisterMap;
    re.RegisterMap=CL_TestRegisterMap;
    menu.Shutdown = CL_TestMenuShutdown; menu.Init = CL_TestMenuInit;
    cl_test_menu_shutdown_count = cl_test_menu_init_count = 0;
    cl_menu_life = CL_MENU_UNLOADED;
    CL_SuspendMenu(); CL_ResumeMenu();
    T_EQ(cl_test_menu_shutdown_count, 0); T_EQ(cl_test_menu_init_count, 0);
    cl_menu_life = CL_MENU_READY;
    CL_BeginLoadingMap("test.map"); CL_BeginLoadingMap("test.map");
    T_EQ(cl_test_menu_shutdown_count, 1); T_EQ(cl_menu_life, CL_MENU_SUSPENDED);
    CL_ResumeMenu(); CL_ResumeMenu();
    T_EQ(cl_test_menu_init_count, 1); T_EQ(cl_menu_life, CL_MENU_READY);
    re.RegisterMap=old_register_map;
    menu = old_menu; cl_menu_life = old_life;
    memcpy(&cl, old_cl, sizeof(cl)); MemFree(old_cl); cls = old_cls; Cvar_Set("map", old_map);
}

TEST(client_session, menu_rebuild_defaults_to_main_menu_target) {
    T_STREQ(CL_RebuildMenuTarget(NULL), "menu_main");
    T_STREQ(CL_RebuildMenuTarget(""), "menu_main");
    T_STREQ(CL_RebuildMenuTarget("menu_main"), "menu_main");
    T_STREQ(CL_RebuildMenuTarget("menu_single_player_campaign"),
            "menu_single_player_campaign");
}

TEST(client_session, menu_rebuild_clears_world_scope_before_returning_to_menu) {
    clMenuLife_t old_life = cl_menu_life;
    cl_menu_life = CL_MENU_READY;
    void (*old_shutdown)(void) = menu.Shutdown;
    void (*old_init)(void) = menu.Init;
    void (*old_register_map)(cstring_t) = re.RegisterMap;
    void (*old_set_scene)(rect_t const *) = re.SetUIScene;

    cl_test_menu_shutdown_count = 0;
    cl_test_menu_init_count = 0;
    cl_test_register_map_count = 0;
    cl_test_register_map_was_null = false;
    menu.Shutdown = CL_TestMenuShutdown;
    menu.Init = CL_TestMenuInit;
    re.RegisterMap = CL_TestRegisterMap;
    re.SetUIScene = CL_TestSetUIScene;
    Cvar_Set("map", "maps/test.map");

    CL_RebuildMenu("menu_main");

    T_STREQ(Cvar_String("map", NULL), "");
    T_EQ(cl_test_menu_shutdown_count, 1);
    T_EQ(cl_test_register_map_count, 1);
    T_ASSERT(cl_test_register_map_was_null);
    T_EQ(cl_test_menu_init_count, 1);

    cl_menu_life = old_life;
    menu.Shutdown = old_shutdown;
    menu.Init = old_init;
    re.RegisterMap = old_register_map;
    re.SetUIScene = old_set_scene;
}

#endif


/* A NULL origin is a screen-space (UI) presentation sound and plays without distance attenuation. */
static void CL_RendererPlaySoundAt(cstring_t path, vec3_t const *origin, float volume) {
    S_PlaySoundPacket(path, origin, origin != NULL, CHAN_AUTO, volume, 1.0f, 0.0f);
}

static void CL_NetStatus_f(void) {
    fprintf(stderr, "Client network: state=%d peer=%s sequence=%u incoming=%u ack=%u message=%u reliable=%u inflight=%u time=%u last_packet=%u precache=%d loading=%d refresh=%d; %s\n",
        cls.state, NET_AdrToString(&cls.netchan.remote_address), cls.netchan.outgoing_sequence,
        cls.netchan.incoming_sequence, cls.netchan.incoming_acknowledged, cls.netchan.message.cursize,
        Netchan_Reliable(&cls.netchan)->cursize, cls.netchan.inflight_length, cl_realtime, cl_last_packet_time,
        cl.precache_ready, cl.layout[LAYER_LOADING] != NULL, cl.refresh_prepped, NET_TransportStatus());
}

void CL_Init(void) {
    videoMode_t mode;

    CON_printf("OpenWarcraft3 v0.1");
    fprintf(stderr, "Console initialized.\n");

    re = R_GetAPI((refImport_t) {
        .MemAlloc = MemAlloc,
        .MemFree = MemFree,
        .FS_ReadFile = FS_ReadFileQ3,
        .FS_FreeFile = FS_FreeFile,
        .FS_MmapFile = FS_MmapFile,
        .FS_MunmapFile = FS_MunmapFile,
        .FileExtract = FS_ExtractFile,
        .LoadSlk = Stb_SlkLoad,
        .CvarString = Cvar_String,
        .PlaySoundAt = CL_RendererPlaySoundAt,
        .error = CON_printf,
    });

    Cmd_AddCommand("vid_apply", CL_VideoApply_f);
    
    mode = CL_VideoMode();
    re.Init(mode.width, mode.height);
    CL_CanvasInit();
    
    S_Init();
    CL_MusicInit();
    CL_MovieInit();

    /* Initialize UI library */
    menu = M_GetAPI((menuImport_t) {
        .FS_ReadFile = CL_UI_ReadFile,
        .FS_FreeFile = FS_FreeFile,
        .FS_IsArchiveMounted = FS_IsArchiveMounted,
        .FS_GetFileList = CL_UI_GetFileList,
        .FS_RegisterArchiveMapSource = FS_RegisterArchiveMapSource,
        .FS_WriteFile = CL_UI_WriteFile,
        .UserPath = FS_UserPath,
        .ListSaves = FS_ListSaves,
        .GetSaveMap = SV_GetSaveMap,
        .DeleteSave = FS_DeleteSave,
        .LoadSavedGame = CL_UI_LoadSavedGame,
        .MemAlloc = MemAlloc,
        .MemFree = MemFree,
        .ImageIndex = CL_ImageIndex,
        .ModelIndex = CL_ModelIndex,
        .FontIndex = CL_FontIndex,
        .Cmd_AddCommand = CL_AddMenuCommand,
        .Cmd_Argc = Cmd_Argc,
        .Cmd_Argv = Cmd_Argv,
        .Cmd_ArgsFrom = Cmd_ArgsFrom,
        .Cmd_ExecuteText = Cbuf_AddText,
        .ServerCommand = CL_UIServerCommand,
        .Cvar_String = Cvar_String,
        .Cvar_Set = CL_UICvarSet,
        .Cvar_Register = CL_UICvarRegister,
        .GetConfigString = CL_GetConfigString,
        .LAN_RefreshServers = CL_LANRefreshServers,
        .LAN_NumServers = CL_LANNumServers,
        .LAN_Server = CL_LANServer,
        .LAN_ConnectServer = CL_LANConnectServer,
        .Online_Begin = Online_Begin,
        .Online_Status = Online_Status,
        .Online_Ready = Online_Ready,
        .Online_HostReady = Online_HostReady,
        .Online_Refresh = Online_Refresh,
        .Online_NumGames = Online_NumGames,
        .Online_Game = CL_OnlineGame,
        .Online_Join = Online_Join,
        .Online_Leave = CL_OnlineLeave,
        .GetRenderer = CL_UIGetRenderer,
        .Printf = CON_printf,
        .PlaySound = S_PlaySound,
        .PlaySoundByName = S_PlaySoundByName,
        .PlayMusic = CL_MusicPlayMenu,
        .StopMusic = CL_MusicStopMenu,
        .PlayMovie = CL_PlayMovie,
    });
    
    cl_menu_life = CL_MENU_SUSPENDED;
    CL_ResumeMenu();

    SZ_Init(&cls.netchan.message, cls.netchan.message_buf, MAX_MSGLEN);
    
    CL_ClearState();

    Cmd_AddCommand("quit", CL_Quit_f);
    Cmd_AddCommand("screenshot", CL_Screenshot_f);
    Cmd_AddCommand("menu_restart", CL_MenuRestart_f);
    Cmd_AddCommand("cl_net_status", CL_NetStatus_f);

    CON_Init();
    Cvar_Get("ui_skip_transitions", "0", CVAR_ARCHIVE);
    CL_InitInput();

    CL_SetMenuBindings();
    cls.state = ca_disconnected;
    scr_initialized = true;
    CL_MenuCommand(Cvar_String("cl_start_menu", "menu_main"));
}

#ifdef __EMSCRIPTEN__
/* Test/automation hook: run a console/menu command from the page (the menu is
 * driven by ordinary console commands). Used by headless UI tests; harmless in
 * normal play. */
EMSCRIPTEN_KEEPALIVE void WC3_DebugMenuCommand(const char *text) {
    if (text && *text) Cmd_ExecuteString(text);
}
#endif

static clSignonStage_t CL_SignonStage(cstring_t command, uint32_t *index) {
    cstring_t const names[] = { "configstrings", "baselines" };
    *index = 0;
    if (!strcmp(command, "new")) return CL_SIGNON_NEW;
    FOR_LOOP(i, 2) {
        size_t length = strlen(names[i]);
        if (strncmp(command, names[i], length)) continue;
        if (!command[length]) return CL_SIGNON_CONFIGSTRINGS + i;
        if (command[length] != ' ' || !command[length + 1] || command[length + 1] == '-') return CL_SIGNON_NONE;
        char *end;
        long value = strtol(command + length + 1, &end, 10);
        uint32_t limit = i ? MAX_GAME_ENTITIES : MAX_CONFIGSTRINGS;
        if (*end || value < 0 || value > (long)limit || strlen(command) >= sizeof(cl_signon.command)) return CL_SIGNON_NONE;
        *index = (uint32_t)value;
        return CL_SIGNON_CONFIGSTRINGS + i;
    }
    return CL_SIGNON_NONE;
}

static bool CL_WriteMirrorCommand(cstring_t command) {
    size_t size = 2 + strlen(command);
    if (cls.netchan.message.cursize > cls.netchan.message.maxsize || size > cls.netchan.message.maxsize - cls.netchan.message.cursize) return false;
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    MSG_WriteString(&cls.netchan.message, command);
    return true;
}

static void CL_SendSignonRequest(void) {
    if (!CL_WriteMirrorCommand(cl_signon.command)) return;
    cl_signon.sent = true;
    cl_signon.sent_time = cl_realtime;
}

/* A lost request or reply cannot leave a connected client on keepalives forever.
 * Requests advance monotonically; delayed replies must not rewind later pages. */
void CL_RequestSignon(cstring_t command) {
    uint32_t index;
    clSignonStage_t stage;
    if (!command) {
        memset(&cl_signon, 0, sizeof(cl_signon));
        cl_signon.stage = CL_SIGNON_COMPLETE;
        return;
    }
    stage = CL_SignonStage(command, &index);
    if (stage == CL_SIGNON_NONE) { CL_WriteMirrorCommand(command); return; }
    if (cl_signon.stage > stage || (cl_signon.stage == stage && cl_signon.index > index)) return;
    cl_signon.stage = stage; cl_signon.index = index; cl_signon.sent = false;
    strlcpy(cl_signon.command, command, sizeof(cl_signon.command));
    CL_SendSignonRequest();
}

void CL_RestartSignon(void) {
    memset(&cl_signon, 0, sizeof(cl_signon));
    CL_RequestSignon("new");
}

static void CL_SignonFrame(void) {
    if (cls.state != ca_connected || !cl_signon.command[0]) return;
    if (!cl_signon.sent || cl_realtime - cl_signon.sent_time >= CL_SIGNON_RETRY_MSEC) CL_SendSignonRequest();
}

void CL_ConnectionlessPacket(netadr_t const *from, sizeBuf_t *msg) {
    char payload[1024] = { 0 };
    char command[256] = { 0 };
    char *info;
    uint32_t length;
    int protocol = 0;

    if (msg->cursize <= 4) {
        return;
    }
    length = msg->cursize - 4;
    if (length >= sizeof(payload)) {
        length = sizeof(payload) - 1;
    }
    memcpy(payload, msg->data + 4, length);
    sscanf(payload, "%255s %d", command, &protocol);
    if (!strcmp(command, "info")) {
        info = strchr(payload, '\n');
        if (from->type == NA_IP) CL_AddLANServer(from, info ? info + 1 : "");
#ifdef __EMSCRIPTEN__
        /* The WS bridge echoes the destination of our own broadcast "info"
         * query back as the packet's from-address, so replies surface as
         * NA_BROADCAST here instead of NA_IP. */
        else if (from->type == NA_BROADCAST) CL_AddLANServer(from, info ? info + 1 : "");
#endif
        return;
    }
    if (strcmp(command, "client_connect")) {
        return;
    }
    if (protocol != BZ_PROTOCOL_VERSION) {
        fprintf(stderr, "CL_ConnectionlessPacket: server protocol %d does not match %d\n", protocol, BZ_PROTOCOL_VERSION);
        return;
    }

    /* Q2 resets the client at the new connection boundary. The server sends
     * the complete configstring/baseline stream again, so old media indices
     * must not be retained or rebound after a same-map save/load. */
    CL_ClearState();
    CL_RestartRefresh();
    CL_RestartSignon();
    if (cls.state < ca_connected) {
        cls.state = ca_connected;
    }
}

#ifdef BZ_TESTS
TEST(client_session, connection_reply_requires_matching_protocol) {
    struct client_state *old_cl = MemAlloc(sizeof(cl));
    struct client_static old_cls = cls;
    void (*old_register_map)(cstring_t) = re.RegisterMap;
    netadr_t loopback = { .type = NA_LOOPBACK };
    cstring_t replies[] = { "client_connect", "client_connect 14",
        "client_connect " BZ_XSTR(BZ_PROTOCOL_VERSION) };
    memcpy(old_cl, &cl, sizeof(cl)); memset(&cl, 0, sizeof(cl));
    re.RegisterMap = CL_TestRegisterMap;
    SZ_Init(&cls.netchan.message, cls.netchan.message_buf, MAX_MSGLEN);
    FOR_LOOP(i, 3) {
        uint8_t bytes[128]; sizeBuf_t msg = { .data = bytes, .maxsize = sizeof(bytes) };
        cls.state = ca_connecting; SZ_Clear(&cls.netchan.message);
        MSG_WriteLong(&msg, -1); MSG_WriteString(&msg, replies[i]);
        CL_ConnectionlessPacket(&loopback, &msg);
        T_EQ(cls.state, i == 2 ? ca_connected : ca_connecting);
        if (i < 2) T_EQ(cls.netchan.message.cursize, 0);
        else {
            cls.netchan.message.readcount = 0;
            T_EQ(MSG_ReadByte(&cls.netchan.message), clc_stringcmd);
            T_STREQ(MSG_ReadString2(&cls.netchan.message), "new");
        }
    }
    re.RegisterMap = old_register_map;
    memcpy(&cl, old_cl, sizeof(cl)); MemFree(old_cl); cls = old_cls;
}

/* A UDP lobby that starts its game, or a same-port reconnect, rebuilds the server's slot and netchan at sequence 0 and
 * announces it with client_connect. The client must restart its own netchan there too: otherwise it drops every server
 * packet as stale and the server drops its "new" as acknowledging packets never sent. */
TEST(client_session, connection_reply_restarts_sequenced_netchan) {
    static struct netchan server;
    static uint8_t packet[MAX_MSGLEN];
    struct client_state *old_cl = MemAlloc(sizeof(cl));
    struct client_static old_cls = cls;
    void (*old_register_map)(cstring_t) = re.RegisterMap;
    netadr_t const host = { .type = NA_IP, .ip = { 127, 0, 0, 1 }, .port = 0x1234 };
    uint8_t bytes[128];
    sizeBuf_t oob = { .data = bytes, .maxsize = sizeof(bytes) }, msg = { .data = packet, .maxsize = sizeof(packet) };
    memcpy(old_cl, &cl, sizeof(cl)); memset(&cl, 0, sizeof(cl));
    re.RegisterMap = CL_TestRegisterMap;
    cls.netchan.remote_address = host;
    SZ_Init(&cls.netchan.message, cls.netchan.message_buf, MAX_MSGLEN);
    Netchan_Reset(&cls.netchan);
    memset(&server, 0, sizeof(server));
    server.remote_address = host;
    SZ_Init(&server.message, server.message_buf, MAX_MSGLEN);
    cls.state = ca_connected;
    FOR_LOOP(i, 4) { /* the lobby: both ends advance their sequences */
        MSG_WriteByte(&server.message, svc_nop);
        msg.cursize = Netchan_BuildPacket(&server, packet, sizeof(packet));
        T_ASSERT(Netchan_Process(&cls.netchan, &msg));
        CL_ClientCommand("lobby_say hi");
        msg.cursize = Netchan_BuildPacket(&cls.netchan, packet, sizeof(packet));
        T_ASSERT(Netchan_Process(&server, &msg));
    }
    Netchan_Reset(&server); /* SV_Map / SV_DirectConnect rebuild the slot */
    MSG_WriteLong(&oob, -1); MSG_WriteString(&oob, "client_connect " BZ_XSTR(BZ_PROTOCOL_VERSION));
    CL_ConnectionlessPacket(&host, &oob);
    msg.cursize = Netchan_BuildPacket(&cls.netchan, packet, sizeof(packet));
    T_ASSERT(Netchan_Process(&server, &msg));
    T_EQ(MSG_ReadByte(&msg), clc_stringcmd);
    T_STREQ(MSG_ReadString2(&msg), "new");
    MSG_WriteByte(&server.message, svc_nop);
    msg.cursize = Netchan_BuildPacket(&server, packet, sizeof(packet));
    T_ASSERT(Netchan_Process(&cls.netchan, &msg));
    re.RegisterMap = old_register_map;
    memcpy(&cl, old_cl, sizeof(cl)); MemFree(old_cl); cls = old_cls;
}
#endif

static void CL_ReadPacketMessage(netadr_t const *from, sizeBuf_t *msg, int length) {
    if ((from->type == NA_EOS || (cls.state != ca_disconnected && cls.netchan.remote_address.type == NA_EOS)) &&
        !NET_CompareAdr(from, &cls.netchan.remote_address)) {
        fprintf(stderr, "CL_ReadPackets: rejected packet outside the connected Internet host\n");
        return;
    }
    cl_last_packet_time = cl_realtime;
    if (length >= 4) {
        int hdr;

        memcpy(&hdr, msg->data, sizeof(hdr));
        if (hdr == -1) {
            CL_ConnectionlessPacket(from, msg);
            return;
        }
    }
    if (Netchan_IsSequenced(&cls.netchan)) {
        /* Q2 netchan: only the connected server's sequenced packets count, and stale or duplicated ones are dropped. */
        if (!NET_CompareAdr(from, &cls.netchan.remote_address) || !Netchan_Process(&cls.netchan, msg)) return;
        CL_RecordPacketAck(cls.netchan.incoming_acknowledged, SDL_GetTicks());
    }
    CL_ParseServerMessage(msg);
}

#ifdef BZ_TESTS
TEST(client_session, latency_uses_fresh_acknowledged_packets) {
    struct client_static old_cls = cls;
    CL_ResetNetchan();
    uint32_t now = SDL_GetTicks();
    CL_RecordPacketSend(10, now - 80);
    CL_RecordPacketSend(11, now - 20);
    CL_RecordPacketAck(9, now);
    T_EQ(CL_NetworkPing(), -1);
    CL_RecordPacketAck(10, now);
    T_EQ(CL_NetworkPing(), 80);
    CL_RecordPacketAck(10, now + 400);
    T_EQ(CL_NetworkPing(), 80);
    CL_RecordPacketAck(11, now);
    T_EQ(CL_NetworkPing(), 65);
    cls.net_ping.updated_at = now - CL_PING_MAX_AGE_MSEC - 1;
    T_EQ(CL_NetworkPing(), -1);
    CL_RecordPacketSend(42, now - 20);
    CL_RecordPacketAck(42, now);
    T_EQ(CL_NetworkPing(), 20);
    CL_ResetNetchan();
    T_EQ(CL_NetworkPing(), -1);
    cls = old_cls;
}

TEST(client_session, lan_discovery_survives_leaving_online_but_foreign_host_packets_are_rejected) {
    struct client_static old_cls = cls;
    uint32_t old_count = cl_num_lan_servers, old_time = cl_last_packet_time;
    menuLanGame_t *saved = malloc(sizeof(cl_lan_servers));
    T_NOT_NULL(saved); if (!saved) return;
    memcpy(saved, cl_lan_servers, sizeof(cl_lan_servers));
    cl_num_lan_servers = 0;
    netadr_t udp = { .type = NA_IP, .ip = {127, 0, 0, 1}, .port = htons(28000) };
    uint8_t bytes[128]; sizeBuf_t message;
    SZ_Init(&message, bytes, sizeof(bytes));
    MSG_WriteLong(&message, -1);
    MSG_WriteString(&message, "info\n\\hostname\\LAN Test\\mapname\\Test.w3m");
    cls.state = ca_disconnected;
    T_ASSERT(NET_StringToAdr("eos:0123456789abcdef0123456789abcdef", 0, &cls.netchan.remote_address));
    CL_ReadPacketMessage(&udp, &message, message.cursize);
    T_EQ(cl_num_lan_servers, 1); T_STREQ(cl_lan_servers[0].hostname, "LAN Test");
    cls.state = ca_connecting; udp.port = htons(28001); cl_last_packet_time = 37;
    CL_ReadPacketMessage(&udp, &message, message.cursize);
    T_EQ(cl_num_lan_servers, 1); T_EQ(cl_last_packet_time, 37);
    CL_ReadPacketMessage(&cls.netchan.remote_address, &message, message.cursize);
    T_EQ(cl_num_lan_servers, 1); /* EOS info cannot enter LAN discovery. */
    cls = old_cls; cl_last_packet_time = old_time; cl_num_lan_servers = old_count;
    memcpy(cl_lan_servers, saved, sizeof(cl_lan_servers)); free(saved);
}
#endif

/* Read all available server packets from the network buffer and dispatch each
 * message type to the appropriate CL_Parse* handler in cl_parse.c. */
void CL_ReadPackets(void) {
    static uint8_t net_message_buffer[MAX_MSGLEN];
    static sizeBuf_t net_message = {
        .data = net_message_buffer,
        .maxsize = MAX_MSGLEN,
        .cursize = 0,
        .readcount = 0,
    };
    netadr_t from;
    int r;

    while ((r = NET_GetLoopPacket(NS_CLIENT, &from, &net_message)) != 0) {
        CL_ReadPacketMessage(&from, &net_message, r);
    }
    while ((r = NET_GetPacket(NS_CLIENT, &from, &net_message)) != 0) {
        CL_ReadPacketMessage(&from, &net_message, r);
    }
}

/* A synchronous listen-server load may pump presentation packets and native
 * window events, but never commands or game frames.  SDL_PumpEvents services
 * the platform window manager without consuming queued gameplay input. */
void CL_LoadingFrame(void) {
    static uint32_t last_pump;
    uint32_t now;

    if (!scr_initialized || Cvar_Integer("dedicated", 0)) return;
    now = SDL_GetTicks();
    if (!last_pump || now - last_pump >= CL_LOADING_PUMP_MSEC) {
        SDL_PumpEvents();
        last_pump = now;
    }
    CL_ReadPackets();
}

void CL_SendCmd(void) {
    if (cls.state == ca_disconnected || cls.state == ca_connecting) {
        return;
    }
    uint32_t previous = cls.netchan.outgoing_sequence;
    if (Netchan_Transmit(NS_CLIENT, &cls.netchan) && Netchan_IsSequenced(&cls.netchan)) {
        uint32_t now = SDL_GetTicks();
        while (previous != cls.netchan.outgoing_sequence) CL_RecordPacketSend(++previous, now);
    }
}

static void CL_CheckTimeout(void) {
    uint32_t timeout_msec;
    int32_t elapsed;

    if (cls.state < ca_connected || cl_last_packet_time == 0) {
        cl_timeout_checks = 0;
        return;
    }
    /* Server-side world loading is synchronous and can silence the link for a
     * minute on cold real-data loads; a fixed 10s window disconnected clients
     * mid-start. cl_timeout (seconds) bounds the silence instead. */
    timeout_msec = (uint32_t)MAX(1, Cvar_Integer("cl_timeout", 120)) * 1000u;
    /* A receive timestamp may be newer than the sampled frame clock. Keep
     * that negative age recent, while retaining elapsed time across wrap. */
    elapsed = (int32_t)(cl_realtime - cl_last_packet_time);
    if (elapsed <= 0 || (uint32_t)elapsed <= timeout_msec) {
        cl_timeout_checks = 0;
        return;
    }
    /* A suspended browser can resume its frame before dispatching queued
     * WebSocket callbacks. Confirm silence on later frames, as for a debugger
     * stop, rather than disconnecting before those callbacks can run. */
    if (++cl_timeout_checks < CL_TIMEOUT_CHECK_FRAMES) return;
    CL_NetStatus_f();
    CL_Disconnect("Connection to host timed out.", true);
}

/* Set up the netchan to point at a remote server and send an initial
 * connection request.  The server will respond with an out-of-band
 * "client_connect" packet which triggers CL_ConnectionlessPacket(). */
static void CL_SanitizeUserinfoValue(cstring_t in, string_t out, uint32_t out_size) {
    uint32_t write = 0;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!in) {
        return;
    }
    for (; *in && write + 1 < out_size; in++) {
        unsigned char c = (unsigned char)*in;

        if (c == '\\' || c == '\n' || c == '\r') {
            c = ' ';
        }
        if (c < 32) {
            continue;
        }
        out[write++] = (char)c;
    }
    out[write] = '\0';
}

static void CL_SendConnectRequest(netadr_t address) {
    UINAME name;
    CL_SanitizeUserinfoValue(Cvar_String("name", "Player"), name, sizeof(name));
    Netchan_OutOfBandPrint(NS_CLIENT, address, "connect %d\n\\name\\%s", BZ_PROTOCOL_VERSION, name[0] ? name : "Player");
}

void CL_Connect(cstring_t host, unsigned short port) {
    netadr_t adr;

    if (!NET_StringToAdr(host, port, &adr)) {
        fprintf(stderr, "CL_Connect: bad server address \"%s\"\n", host);
        return;
    }
    // Loopback (localhost) needs no UDP socket — packets go through the
    // in-memory ring buffer.  Only open the socket for remote addresses.
    if (adr.type == NA_EOS && (!Online_Ready() || !Online_InLobby())) {
        fprintf(stderr, "CL_Connect: join an Internet room before connecting to its host\n");
        return;
    }
    if (adr.type == NA_IP || adr.type == NA_BROADCAST) {
        NET_ConfigSource(NS_CLIENT, true);
        if (!NET_IsConfigured(NS_CLIENT)) {
            fprintf(stderr, "CL_Connect: client UDP socket is closed, cannot connect\n");
            return;
        }
    }
    cls.netchan.remote_address = adr;
    SZ_Init(&cls.netchan.message, cls.netchan.message_buf, MAX_MSGLEN);
    CL_ResetNetchan();
    CL_ResetFrameHistory();
    Cbuf_CopyToDefer();
    cls.state = ca_connecting;
    // Send an out-of-band "connect" request; the server will register this
    // client slot and reply with "client_connect".
    cl_connect_started = cl_connect_retry = cl_realtime;
    CL_SendConnectRequest(adr);
    if (adr.type == NA_LOOPBACK)
        fprintf(stderr, "CL_Connect: connecting to local server via loopback\n");
    else
        fprintf(stderr, "CL_Connect: connecting to %s\n", NET_AdrToString(&adr));
}

void CL_Shutdown(void) {
    Sys_UpdateLoadingPresentation(false, 1.0f, true);
    CL_CancelCinematicSession();
    CL_SuspendMenu();
    cl_menu_life = CL_MENU_UNLOADED;
    FOR_LOOP(modelIndex, MAX_MODELS) {
        SAFE_DELETE(cl.models[modelIndex], re.ReleaseModel);
        SAFE_DELETE(cl.portraits[modelIndex], re.ReleaseModel);
    }
    SAFE_DELETE(cl.minimap_model, re.ReleaseModel);
    FOR_LOOP(imageIndex, MAX_DYNAMIC_IMAGES) {
        SAFE_DELETE(cl.dynamicPics[imageIndex], re.ReleaseTexture);
    }
    CL_MovieShutdown();
    CL_MusicShutdown();
    V_Shutdown();
    re.Shutdown();
    S_Shutdown();
}

void CL_SendCommand(void) {
    CL_SendCmd();

    Cbuf_Execute();
}

/* Main client tick called from the platform event loop.
 * Advances the client clock, applies incoming server state, samples input,
 * sends commands, and renders the current frame. */
static void CL_SendSoundEvents(void) {
    soundEvent_t event;
    if (cls.state == ca_connected) return; /* retain loading-time events until active */
    while (cls.state != ca_active || cls.netchan.message.cursize + 64 < cls.netchan.message.maxsize) {
        if (!S_PollSoundEvent(&event)) break;
        if (cls.state != ca_active) continue;
        MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
        SZ_Printf(&cls.netchan.message, "sound_event %u %u %u", event.user, event.request, event.event);
    }
}

void CL_Frame(uint32_t msec) {
    cl_realtime += msec;
    cl.time += msec;

    netadr_t online_host;
    if (Online_TakeConnection(&online_host)) {
        cstring_t address = NET_AdrToString(&online_host);
        Cvar_Set("connect", address);
        CL_Connect(address, 0);
    }
    if (cls.netchan.remote_address.type == NA_EOS && cls.state != ca_disconnected && !Online_InLobby())
        CL_Disconnect("The Internet room has closed.", true);
    CL_ProcessPendingMenuAction();
    CL_Input();
    CL_CanvasFrame(cl_realtime);
    CL_MovieUpdate();
    CL_ReadPackets();
    CL_SignonFrame();
    if (cls.state == ca_connecting) {
        if (cl_realtime - cl_connect_started > CL_TIMEOUT_MSEC) {
            CL_Disconnect("Host connection timed out.", true);
        } else if (cl_realtime - cl_connect_retry >= 1000) {
            cl_connect_retry = cl_realtime;
            CL_SendConnectRequest(cls.netchan.remote_address);
        }
    }
    CL_MusicUpdate();
    S_SetUserVolume(Cvar_Integer("s_sound", 1)
        ? Cvar_Value("s_volume", 1.0f) : 0.0f);
    CL_SendSoundEvents();
    CL_CheckTimeout();
    CL_SendCommand();
    if (cls.state == ca_connected && !cl.refresh_prepped) {
        CL_PrepRefresh();
    } else if (cls.state == ca_active) {
        CL_PrepRefresh();
    }
    SCR_UpdateScreen(msec);
}

#if defined(BZ_TESTS) && defined(BZ_CLIENT_WORLD)
#include "shared/test.h"
static size2_t CL_TestNoWindow(void) { return (size2_t){ 0 }; }

static void CL_TestSignonRequest(cstring_t expected) {
    uint8_t bytes[128];
    sizeBuf_t packet = { .data = bytes, .maxsize = sizeof(bytes) };
    netadr_t from;
    int length = NET_GetLoopPacket(NS_SERVER, &from, &packet);
    if (!expected) { T_EQ(length, 0); return; }
    T_ASSERT(length > 0);
    if (length <= 0) return;
    T_EQ(MSG_ReadByte(&packet), clc_stringcmd); T_STREQ(MSG_ReadString2(&packet), expected);
}

static void CL_TestConnectRequest(void) {
    uint8_t bytes[256];
    sizeBuf_t packet = { .data = bytes, .maxsize = sizeof(bytes) };
    netadr_t from;
    int length = NET_GetLoopPacket(NS_SERVER, &from, &packet);
    T_ASSERT(length > 4);
    if (length <= 4) return;
    T_EQ(MSG_ReadLong(&packet), -1);
    T_ASSERT(!strncmp(MSG_ReadString2(&packet), "connect " BZ_XSTR(BZ_PROTOCOL_VERSION) "\n", strlen("connect " BZ_XSTR(BZ_PROTOCOL_VERSION) "\n")));
}

TEST(client_session, lost_connect_request_retries_until_reply_or_deadline) {
    struct client_state *old_cl = MemAlloc(sizeof(cl));
    struct client_static old_cls = cls;
    refExport_t old_renderer = re;
    uint32_t old_time = cl_realtime, old_packet = cl_last_packet_time;
    uint32_t old_started = cl_connect_started, old_retry = cl_connect_retry;
    bool old_screen = scr_initialized;
    clMenuLife_t old_life = cl_menu_life;
    uint8_t bytes[128];
    sizeBuf_t packet = { .data = bytes, .maxsize = sizeof(bytes) };
    netadr_t loopback = { .type = NA_LOOPBACK };

    memcpy(old_cl, &cl, sizeof(cl)); memset(&cl, 0, sizeof(cl)); memset(&cls, 0, sizeof(cls));
    re.GetWindowSize = CL_TestNoWindow; re.RegisterMap = CL_TestRegisterMap;
    scr_initialized = false; cl_menu_life = CL_MENU_UNLOADED; CL_ClearState();
    SZ_Init(&cls.netchan.message, cls.netchan.message_buf, sizeof(cls.netchan.message_buf));
    cls.netchan.remote_address = loopback; cls.state = ca_connecting;
    cl_realtime = cl_last_packet_time = cl_connect_started = cl_connect_retry = 100;
    NET_ClearLoopPackets(NS_CLIENT); NET_ClearLoopPackets(NS_SERVER);
    CL_SendConnectRequest(loopback); CL_TestConnectRequest(); /* drop the first connect */
    CL_Frame(500); CL_TestSignonRequest(NULL);
    CL_Frame(501); CL_TestConnectRequest();
    MSG_WriteLong(&packet, -1); MSG_WriteString(&packet, "client_connect " BZ_XSTR(BZ_PROTOCOL_VERSION));
    NET_SendPacket(NS_SERVER, packet.cursize, packet.data, loopback);
    CL_Frame(16); T_EQ(cls.state, ca_connected); CL_TestSignonRequest("new");
    CL_Frame(1001); CL_TestSignonRequest("new"); /* dialing has stopped */

    CL_ClearState(); cls.state = ca_connecting;
    cl_connect_started = cl_connect_retry = cl_realtime;
    CL_Frame(10001); T_EQ(cls.state, ca_disconnected); CL_TestSignonRequest(NULL);
    NET_ClearLoopPackets(NS_CLIENT); NET_ClearLoopPackets(NS_SERVER);
    memcpy(&cl, old_cl, sizeof(cl)); MemFree(old_cl); cls = old_cls; re = old_renderer;
    cl_realtime = old_time; cl_last_packet_time = old_packet;
    cl_connect_started = old_started; cl_connect_retry = old_retry;
    scr_initialized = old_screen; cl_menu_life = old_life;
}

TEST(client_session, lost_signon_pages_retry_without_rewinding_or_restarting_completed_loads) {
    struct client_state *old_cl = MemAlloc(sizeof(cl));
    struct client_static old_cls = cls;
    refExport_t old_renderer = re;
    uint32_t old_time = cl_realtime, old_packet = cl_last_packet_time;
    bool old_screen = scr_initialized;
    clMenuLife_t old_life = cl_menu_life;
    uint8_t bytes[128];
    sizeBuf_t packet = { .data = bytes, .maxsize = sizeof(bytes) };
    netadr_t loopback = { .type = NA_LOOPBACK };
    cstring_t commands[] = { "configstrings 52", "baselines 7", "configstrings 2", "precache" };

    memcpy(old_cl, &cl, sizeof(cl)); memset(&cl, 0, sizeof(cl)); memset(&cls, 0, sizeof(cls));
    re.GetWindowSize = CL_TestNoWindow; re.RegisterMap = CL_TestRegisterMap;
    scr_initialized = false; cl_menu_life = CL_MENU_UNLOADED;
    SZ_Init(&cls.netchan.message, cls.netchan.message_buf, sizeof(cls.netchan.message_buf));
    cls.netchan.remote_address = loopback; cls.state = ca_connecting;
    cl_realtime = cl_last_packet_time = 100;
    NET_ClearLoopPackets(NS_CLIENT); NET_ClearLoopPackets(NS_SERVER);
    MSG_WriteLong(&packet, -1); MSG_WriteString(&packet, "client_connect " BZ_XSTR(BZ_PROTOCOL_VERSION));
    NET_SendPacket(NS_SERVER, packet.cursize, packet.data, loopback);
    CL_Frame(16); CL_TestSignonRequest("new"); /* drop the first request */
    CL_Frame(500); CL_TestSignonRequest(NULL);
    CL_Frame(501); CL_TestSignonRequest("new");

    FOR_LOOP(i, 4) {
        SZ_Clear(&packet); packet.readcount = 0;
        MSG_WriteByte(&packet, svc_mirror); MSG_WriteString(&packet, commands[i]);
        NET_SendPacket(NS_SERVER, packet.cursize, packet.data, loopback);
        CL_Frame(16);
        CL_TestSignonRequest(i < 2 ? commands[i] : i == 3 ? "new" : NULL);
        if (i == 0) { CL_Frame(1001); CL_TestSignonRequest(commands[i]); }
        if (i == 2) { CL_Frame(1001); CL_TestSignonRequest("baselines 7"); }
    }
    T_ASSERT(!cl.precache_ready); /* the loading presentation's final packet was lost */
    SZ_Clear(&packet); packet.readcount = 0;
    uiFrame_t empty = { 0 }, frame = { .number = 1, .flags.type = FT_SIMPLEFRAME };
    MSG_WriteByte(&packet, svc_layout); MSG_WriteByte(&packet, LAYER_LOADING);
    MSG_WriteDeltaUIFrame(&packet, &empty, &frame, true); MSG_WriteByte(&packet, 0);
    MSG_WriteLong(&packet, 0); MSG_WriteShort(&packet, 0);
    MSG_WriteByte(&packet, svc_mirror); MSG_WriteString(&packet, "precache");
    NET_SendPacket(NS_SERVER, packet.cursize, packet.data, loopback);
    CL_Frame(16); T_NOT_NULL(cl.layout[LAYER_LOADING]); T_ASSERT(cl.precache_ready);
    CL_TestSignonRequest(NULL);
    CL_Frame(1001); CL_TestSignonRequest(NULL);
    CL_ClearState();
    cls.netchan.message.maxsize = 4;
    CL_RequestSignon("configstrings 52");
    T_EQ(cls.netchan.message.cursize, 0); T_ASSERT(!cls.netchan.message.overflowed);
    CL_Frame(16); CL_TestSignonRequest(NULL);
    cls.netchan.message.maxsize = sizeof(cls.netchan.message_buf);
    CL_Frame(16); CL_TestSignonRequest("configstrings 52");
    CL_RequestSignon("configstrings 2"); CL_Frame(16); CL_TestSignonRequest(NULL);
    CL_Frame(1001); CL_TestSignonRequest("configstrings 52");
    CL_RequestSignon("status"); CL_Frame(16); CL_TestSignonRequest("status");
    CL_RequestSignon(NULL); CL_Frame(1001); CL_TestSignonRequest(NULL);
    CL_ClearState();
    CL_Frame(1001); CL_TestSignonRequest(NULL);

    NET_ClearLoopPackets(NS_CLIENT); NET_ClearLoopPackets(NS_SERVER);
    memcpy(&cl, old_cl, sizeof(cl)); MemFree(old_cl); cls = old_cls; re = old_renderer;
    cl_realtime = old_time; cl_last_packet_time = old_packet;
    scr_initialized = old_screen; cl_menu_life = old_life;
}

TEST(client_session, resumed_frame_allows_receive_callback_before_confirming_timeout) {
    struct client_state *old_cl = MemAlloc(sizeof(cl));
    struct client_static old_cls = cls;
    refExport_t old_renderer = re;
    uint32_t old_time = cl_realtime, old_packet = cl_last_packet_time;
    bool old_screen = scr_initialized;
    clMenuLife_t old_life = cl_menu_life;
    char old_timeout[64];
    uint8_t bytes[128], nop = svc_nop;
    sizeBuf_t packet = { .data = bytes, .maxsize = sizeof(bytes) };
    netadr_t loopback = { .type = NA_LOOPBACK }, from;

    strlcpy(old_timeout, Cvar_String("cl_timeout", "120"), sizeof(old_timeout));
    memcpy(old_cl, &cl, sizeof(cl)); memset(&cl, 0, sizeof(cl));
    memset(&cls, 0, sizeof(cls));
    re.GetWindowSize = CL_TestNoWindow; re.RegisterMap = CL_TestRegisterMap;
    scr_initialized = false; cl_menu_life = CL_MENU_UNLOADED;
    CL_ClearState();
    SZ_Init(&cls.netchan.message, cls.netchan.message_buf, sizeof(cls.netchan.message_buf));
    cls.netchan.remote_address = loopback; cls.state = ca_connected;
    Cvar_Set("cl_timeout", "1"); cl_realtime = cl_last_packet_time = 100;
    NET_ClearLoopPackets(NS_CLIENT); NET_ClearLoopPackets(NS_SERVER);

    /* A platform resumes its frame callback before dispatching the queued receive callback. */
    CL_Frame(1501);
    T_EQ(cls.state, ca_connected);
    NET_SendPacket(NS_SERVER, 1, &nop, loopback);
    CL_Frame(16);
    T_EQ(cls.state, ca_connected); T_EQ(cl_last_packet_time, cl_realtime);
    FOR_LOOP(i, 6) { CL_Frame(16); T_EQ(cls.state, ca_connected); }

    /* A vanished peer still disconnects after consecutive expired client frames. */
    CL_Frame(1001); T_EQ(cls.state, ca_connected);
    FOR_LOOP(i, 4) { CL_Frame(16); T_EQ(cls.state, ca_connected); }
    CL_Frame(16); T_EQ(cls.state, ca_disconnected);
    T_ASSERT(NET_GetLoopPacket(NS_SERVER, &from, &packet) > 0);
    T_EQ(MSG_ReadByte(&packet), clc_stringcmd); T_STREQ(MSG_ReadString2(&packet), "disconnect");

    NET_ClearLoopPackets(NS_CLIENT); NET_ClearLoopPackets(NS_SERVER);
    Cvar_Set("cl_timeout", old_timeout);
    memcpy(&cl, old_cl, sizeof(cl)); MemFree(old_cl); cls = old_cls; re = old_renderer;
    cl_realtime = old_time; cl_last_packet_time = old_packet;
    scr_initialized = old_screen; cl_menu_life = old_life;
}

TEST(client_session, recent_packet_ahead_of_the_sampled_clock_is_not_a_timeout) {
    struct client_state *old_cl = MemAlloc(sizeof(cl));
    struct client_static old_cls = cls;
    refExport_t old_renderer = re;
    uint32_t old_time = cl_realtime, old_packet = cl_last_packet_time, old_checks = cl_timeout_checks;
    clMenuLife_t old_life = cl_menu_life;
    char old_timeout[64];
    uint8_t bytes[128];
    sizeBuf_t packet = { .data = bytes, .maxsize = sizeof(bytes) };
    netadr_t from;

    strlcpy(old_timeout, Cvar_String("cl_timeout", "120"), sizeof(old_timeout));
    memcpy(old_cl, &cl, sizeof(cl)); memset(&cl, 0, sizeof(cl));
    memset(&cls, 0, sizeof(cls));
    re.RegisterMap = CL_TestRegisterMap;
    cl_menu_life = CL_MENU_UNLOADED;
    SZ_Init(&cls.netchan.message, cls.netchan.message_buf, sizeof(cls.netchan.message_buf));
    cls.netchan.remote_address.type = NA_LOOPBACK; cls.state = ca_connected;
    NET_ClearLoopPackets(NS_CLIENT); NET_ClearLoopPackets(NS_SERVER);
    Cvar_Set("cl_timeout", "1"); cl_timeout_checks = 0;

    /* Published Edge receipts showed time=904125, last_packet=904146 with
     * an open transport. A negative packet age is recent, not 49 days old. */
    FOR_LOOP(i, CL_TIMEOUT_CHECK_FRAMES + 1) {
        cl_realtime = 904125 + i * 8; cl_last_packet_time = cl_realtime + 21;
        CL_CheckTimeout();
        T_EQ(cls.state, ca_connected); T_EQ(cl_timeout_checks, 0);
    }
    /* Ordinary 32-bit clock wrap still has a small positive elapsed time. */
    cl_realtime = 32; cl_last_packet_time = UINT32_MAX - 15;
    CL_CheckTimeout(); T_EQ(cls.state, ca_connected); T_EQ(cl_timeout_checks, 0);
    /* Actual silence across the wrap must still disconnect after confirmation. */
    cl_realtime = 2500; cl_last_packet_time = UINT32_MAX - 500;
    FOR_LOOP(i, CL_TIMEOUT_CHECK_FRAMES - 1) { CL_CheckTimeout(); T_EQ(cls.state, ca_connected); }
    CL_CheckTimeout(); T_EQ(cls.state, ca_disconnected);
    T_ASSERT(NET_GetLoopPacket(NS_SERVER, &from, &packet) > 0);
    T_EQ(MSG_ReadByte(&packet), clc_stringcmd); T_STREQ(MSG_ReadString2(&packet), "disconnect");

    NET_ClearLoopPackets(NS_CLIENT); NET_ClearLoopPackets(NS_SERVER);
    Cvar_Set("cl_timeout", old_timeout);
    memcpy(&cl, old_cl, sizeof(cl)); MemFree(old_cl); cls = old_cls; re = old_renderer;
    cl_realtime = old_time; cl_last_packet_time = old_packet; cl_timeout_checks = old_checks;
    cl_menu_life = old_life;
}

TEST(client_sound, playback_receipts_are_reliable_ordered_and_wait_for_buffer_space) {
    sState_t *saved = malloc(sizeof(s));
    sizeBuf_t old_message = cls.netchan.message;
    int old_state = cls.state;
    uint8_t bytes[256]; int16_t out[2];
    sfxcache_t sample = { .length = 1, .loopstart = -1, .data = {1000} };
    sfx_t entry = { .path = "receipt-test.wav", .cache = &sample };
    soundPolicy_t policy = { .user = 24, .request = 731, .max_channel = 3, .max_total = 24, .max_duplicates = 4 };
    T_NOT_NULL(saved); if (!saved) return;
    *saved = s; memset(&s, 0, sizeof(s)); S_ClearSoundEvents();
    s.initialized = true; s.known_sfx = &entry; s.sfxcap = 1; ARRAY_COUNT(s.known_sfx) = 1;
    SZ_Init(&cls.netchan.message, bytes, 1); cls.state = ca_active;
    T_ASSERT(S_PlaySoundPolicy("receipt-test.wav", NULL, false, 0, 1, 0, 0, &policy));
    S_TestMix(out, 1);
    CL_SendSoundEvents(); T_EQ(cls.netchan.message.cursize, 0); /* full buffer retains events */
    SZ_Init(&cls.netchan.message, bytes, sizeof(bytes));
    cls.state = ca_connected; CL_SendSoundEvents(); T_EQ(cls.netchan.message.cursize, 0);
    cls.state = ca_active; CL_SendSoundEvents();
    FOR_LOOP(i, 3) {
        char expected[64]; snprintf(expected, sizeof(expected), "sound_event 24 731 %u", i + 1);
        T_EQ(MSG_ReadByte(&cls.netchan.message), clc_stringcmd);
        char actual[64]; MSG_ReadString(&cls.netchan.message, actual);
        T_STREQ(actual, expected);
    }
    T_EQ(cls.netchan.message.readcount, cls.netchan.message.cursize);
    S_ClearSoundEvents(); s = *saved; free(saved); cls.netchan.message = old_message; cls.state = old_state;
}
#endif
