#include "campaign_progress.h"

#include <errno.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
/* The browser build keeps user files in the in-memory FS, which a page reload
 * discards. Mirror the progress file through localStorage so a reload resumes
 * the campaign instead of resetting to mission one. */
EM_JS(void, WC3_CampaignProgressPersistJS, (const char *path), {
    try {
        var p = UTF8ToString(Number(path));
        var data = Module.FS.readFile(p);
        var bin = String();
        for (var i = 0; i < data.length; i += 0x8000)
            bin += String.fromCharCode.apply(null, data.subarray(i, i + 0x8000));
        localStorage.setItem('wc3-campaign-progress', btoa(bin));
    } catch (e) {}
});
EM_JS(void, WC3_CampaignProgressRestoreJS, (const char *path), {
    try {
        var p = UTF8ToString(Number(path));
        var stored = localStorage.getItem('wc3-campaign-progress');
        if (!stored) return;
        var exists = true;
        try { Module.FS.stat(p); } catch (e) { exists = false; }
        if (exists) return;
        var bin = atob(stored);
        var bytes = new Uint8Array(bin.length);
        for (var i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
        var dir = p.split('/').slice(0, -1).join('/');
        if (dir) { try { Module.FS.mkdirTree(dir); } catch (e) {} }
        Module.FS.writeFile(p, bytes);
    } catch (e) {}
});
#endif
#include <ctype.h>
#ifndef _WIN32
#include <strings.h>
#endif

#define WC3_CAMPAIGN_PROGRESS_MAGIC MAKEFOURCC('O', 'R', 'C', 'P')
#define WC3_CAMPAIGN_PROGRESS_VERSION 2u // format version; appends mission completion and best-difficulty arrays

typedef struct {
    cstring_t name;
    int32_t campaign[WC3_CAMPAIGN_PROGRESS_EDITIONS];
} campaignProgressName_t;

/* Modern CampaignInfoClassic paths identify the mission's edition independently of loaded archives. */
typedef struct {
    cstring_t prefix;
    uint32_t edition;
} campaignProgressPath_t;

static campaignProgressName_t const campaign_progress_names[] = {
    { "Tutorial", { 0, -1 } },
    { "Human",    { 1,  1 } },
    { "Undead",   { 2,  2 } },
    { "Orc",      { 3,  3 } },
    { "NightElf", { 4,  0 } },
    { "NightElfEx", { -1, 0 } },
    { "HumanEx",    { -1, 1 } },
    { "UndeadEx",   { -1, 2 } },
    { "OrcEx",      { -1, 3 } },
};

static campaignProgressPath_t const campaign_progress_paths[] = {
    { .prefix = "Campaign/Classic/ROC/", .edition = WC3_CAMPAIGN_EDITION_ROC },
    { .prefix = "Campaign/Classic/TFT/", .edition = WC3_CAMPAIGN_EDITION_TFT },
};

/* Expansion archives can be enabled while a ROC mission is active. Only an
 * explicit authored edition prefix overrides the legacy archive selection. */
uint32_t wc3_campaign_progress_map_edition(uint32_t edition, cstring_t map_path) {
    char path[MAX_PATHLEN];
    if (!map_path || strlen(map_path) >= sizeof(path)) return edition;
    strlcpy(path, map_path, sizeof(path));
    for (string_t p = path; *p; p++)
        if (*p == '\\') *p = '/';
    FOR_LOOP(i, sizeof(campaign_progress_paths) / sizeof(campaign_progress_paths[0])) {
        campaignProgressPath_t const *row = &campaign_progress_paths[i];
        if (!strncasecmp(path, row->prefix, strlen(row->prefix))) return row->edition;
    }
    return edition;
}

static bool campaign_progress_key_valid(wc3CampaignProgressKey_t key) {
    return key.edition < WC3_CAMPAIGN_PROGRESS_EDITIONS &&
           key.campaign < WC3_CAMPAIGN_PROGRESS_CAMPAIGNS;
}

static bool campaign_progress_write_bytes(FILE *file, void const * data, size_t size) {
    return size == 0 || fwrite(data, 1, size, file) == size;
}

static bool campaign_progress_read_bytes(FILE *file, void *data, size_t size) {
    return size == 0 || fread(data, 1, size, file) == size;
}

static bool campaign_progress_write_u32(FILE *file, uint32_t value) {
    uint8_t const bytes[4] = {
        (uint8_t)(value & 0xffu),
        (uint8_t)((value >> 8) & 0xffu),
        (uint8_t)((value >> 16) & 0xffu),
        (uint8_t)((value >> 24) & 0xffu),
    };
    return campaign_progress_write_bytes(file, bytes, sizeof(bytes));
}

static bool campaign_progress_read_u32(FILE *file, uint32_t * value) {
    uint8_t bytes[4];

    if (!campaign_progress_read_bytes(file, bytes, sizeof(bytes))) return false;
    *value = (uint32_t)bytes[0] |
             ((uint32_t)bytes[1] << 8) |
             ((uint32_t)bytes[2] << 16) |
             ((uint32_t)bytes[3] << 24);
    return true;
}

void wc3_campaign_progress_init(wc3CampaignProgress_t *progress) {
    if (progress) memset(progress, 0, sizeof(*progress));
}

bool wc3_campaign_progress_set_tutorial(wc3CampaignProgress_t *progress,
                                        uint32_t edition, bool cleared) {
    if (!progress || edition >= WC3_CAMPAIGN_PROGRESS_EDITIONS) return false;
    progress->tutorial_known[edition] = 1;
    progress->tutorial_cleared[edition] = cleared ? 1 : 0;
    return true;
}

bool wc3_campaign_progress_set_campaign(wc3CampaignProgress_t *progress,
                                        wc3CampaignProgressKey_t key, bool available) {
    if (!progress || !campaign_progress_key_valid(key)) return false;
    progress->campaign_known[key.edition][key.campaign] = 1;
    progress->campaign_available[key.edition][key.campaign] = available ? 1 : 0;
    return true;
}

bool wc3_campaign_progress_set_mission(wc3CampaignProgress_t *progress,
                                       wc3CampaignProgressKey_t key, bool available) {
    if (!progress || !campaign_progress_key_valid(key) ||
        key.mission >= WC3_CAMPAIGN_PROGRESS_MISSIONS) {
        return false;
    }
    progress->mission_known[key.edition][key.campaign][key.mission] = 1;
    progress->mission_available[key.edition][key.campaign][key.mission] = available ? 1 : 0;
    return true;
}

bool wc3_campaign_progress_has_campaign(wc3CampaignProgress_t const *progress,
                                        wc3CampaignProgressKey_t key) {
    return progress && campaign_progress_key_valid(key) &&
           progress->campaign_known[key.edition][key.campaign];
}

bool wc3_campaign_progress_campaign_available(wc3CampaignProgress_t const *progress,
                                              wc3CampaignProgressKey_t key) {
    return progress && campaign_progress_key_valid(key) &&
           progress->campaign_available[key.edition][key.campaign];
}

bool wc3_campaign_progress_has_mission(wc3CampaignProgress_t const *progress,
                                       wc3CampaignProgressKey_t key) {
    return progress && campaign_progress_key_valid(key) &&
           key.mission < WC3_CAMPAIGN_PROGRESS_MISSIONS &&
           progress->mission_known[key.edition][key.campaign][key.mission];
}

bool wc3_campaign_progress_mission_available(wc3CampaignProgress_t const *progress,
                                             wc3CampaignProgressKey_t key) {
    return progress && campaign_progress_key_valid(key) &&
           key.mission < WC3_CAMPAIGN_PROGRESS_MISSIONS &&
           progress->mission_available[key.edition][key.campaign][key.mission];
}

bool wc3_campaign_progress_complete_mission(wc3CampaignProgress_t *progress,
                                            wc3CampaignProgressKey_t key, uint32_t difficulty) {
    uint8_t *completed, *best;
    if (!progress || !campaign_progress_key_valid(key) ||
        key.mission >= WC3_CAMPAIGN_PROGRESS_MISSIONS || difficulty > 3) return false;
    completed = &progress->mission_completed[key.edition][key.campaign][key.mission];
    best = &progress->mission_best_difficulty[key.edition][key.campaign][key.mission];
    if (!*completed || difficulty > *best) *best = (uint8_t)difficulty;
    *completed = 1;
    return true;
}

bool wc3_campaign_progress_best_difficulty(wc3CampaignProgress_t const *progress,
                                            wc3CampaignProgressKey_t key, uint32_t *difficulty) {
    if (!progress || !difficulty || !campaign_progress_key_valid(key) ||
        key.mission >= WC3_CAMPAIGN_PROGRESS_MISSIONS ||
        !progress->mission_completed[key.edition][key.campaign][key.mission]) return false;
    *difficulty = progress->mission_best_difficulty[key.edition][key.campaign][key.mission];
    return *difficulty <= 3;
}

static char *campaign_progress_trim(char *text) {
    char *end;
    while (*text && isspace((unsigned char)*text)) text++;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) *--end = '\0';
    return text;
}

static bool campaign_progress_indexed(cstring_t key, cstring_t prefix, uint32_t *index) {
    size_t const len = strlen(prefix);
    uint32_t value = 0;
    if (strncasecmp(key, prefix, len) || !isdigit((unsigned char)key[len])) return false;
    for (cstring_t cursor = key + len; *cursor; cursor++) {
        if (!isdigit((unsigned char)*cursor)) return false;
        value = value * 10 + (uint32_t)(*cursor - '0');
        if (value >= WC3_CAMPAIGN_PROGRESS_MISSIONS) return false;
    }
    *index = value;
    return true;
}

static bool campaign_progress_quoted(cstring_t value, uint32_t ordinal, string_t out, size_t cap) {
    if (!value || !cap) return false;
    for (uint32_t i = 0; i <= ordinal; i++) {
        cstring_t start = strchr(value, '"');
        cstring_t end;
        size_t len;
        if (!start || !(end = strchr(++start, '"'))) return false;
        if (i != ordinal) { value = end + 1; continue; }
        len = (size_t)(end - start);
        if (!len || len >= cap) return false;
        memcpy(out, start, len);
        out[len] = '\0';
        return true;
    }
    return false;
}

static bool campaign_progress_same_map(cstring_t a, cstring_t b) {
    if (!a || !b) return false;
    for (; *a && *b; a++, b++) {
        unsigned char x = (unsigned char)*a, y = (unsigned char)*b;
        if (x == '/') x = '\\';
        if (y == '/') y = '\\';
        if (tolower(x) != tolower(y)) return false;
    }
    return !*a && !*b;
}

/* Both retail CampaignStrings schemas are supported: newer MissionN has a third
 * quoted map path; 1.00-era MissionN only has a title and FileN supplies the
 * map basename. Cinematics never have either map-backed row. */
bool wc3_campaign_progress_resolve_map(uint32_t edition, cstring_t map_path,
                                        cstring_t campaign_strings, size_t size,
                                        wc3CampaignProgressKey_t *out) {
    size_t pos = 0;
    char section[128] = "";
    if (!out || !map_path || !campaign_strings ||
        edition >= WC3_CAMPAIGN_PROGRESS_EDITIONS) return false;
    while (pos < size) {
        char line[2048], path[MAX_PATHLEN], *key, *value, *eq;
        uint32_t mission;
        size_t count = 0;
        while (pos < size && campaign_strings[pos] != '\n' && campaign_strings[pos] != '\r') {
            if (count + 1 < sizeof(line)) line[count++] = campaign_strings[pos];
            pos++;
        }
        while (pos < size && (campaign_strings[pos] == '\n' || campaign_strings[pos] == '\r')) pos++;
        line[count] = '\0';
        key = campaign_progress_trim(line);
        if (key[0] == '/' && key[1] == '/') continue;
        if (*key == '[') {
            char *end = strchr(key + 1, ']');
            section[0] = '\0';
            if (end) {
                *end = '\0';
                cstring_t name = campaign_progress_trim(key + 1);
                if (strlen(name) < sizeof(section)) strlcpy(section, name, sizeof(section));
            }
            continue;
        }
        if (!section[0] || !(eq = strchr(key, '='))) continue;
        *eq = '\0';
        value = campaign_progress_trim(eq + 1);
        key = campaign_progress_trim(key);
        if (campaign_progress_indexed(key, "Mission", &mission)) {
            if (!campaign_progress_quoted(value, 2, path, sizeof(path))) continue;
        } else if (campaign_progress_indexed(key, "File", &mission)) {
            char file[MAX_PATHLEN];
            if (!campaign_progress_quoted(value, 0, file, sizeof(file))) continue;
            if (strchr(file, '/') || strchr(file, '\\')) {
                strlcpy(path, file, sizeof(path));
            } else if (snprintf(path, sizeof(path), "Maps\\Campaign\\%s.w3m", file) >= (int)sizeof(path)) {
                continue;
            }
        } else continue;
        if (campaign_progress_same_map(path, map_path)) {
            /* Resolve after reading the row's path: one modern catalog holds
             * Tutorial/Human/etc. and the TFT *Ex sections at the same time. */
            uint32_t const map_edition = wc3_campaign_progress_map_edition(edition, path);
            int32_t const campaign = wc3_campaign_progress_campaign_index(map_edition, section);
            if (campaign < 0) continue;
            *out = MAKE(wc3CampaignProgressKey_t,
                        .edition = map_edition, .campaign = (uint32_t)campaign, .mission = mission);
            return true;
        }
    }
    return false;
}

int32_t wc3_campaign_progress_campaign_index(uint32_t edition, cstring_t key) {
    if (!key || !*key || edition >= WC3_CAMPAIGN_PROGRESS_EDITIONS) return -1;
    FOR_LOOP(i, sizeof(campaign_progress_names) / sizeof(campaign_progress_names[0])) {
        if (!strcasecmp(key, campaign_progress_names[i].name)) {
            return campaign_progress_names[i].campaign[edition];
        }
    }
    return -1;
}

bool wc3_campaign_progress_load(cstring_t path, wc3CampaignProgress_t *progress) {
    FILE *file;
    uint32_t magic, version, editions, campaigns, missions;
    wc3CampaignProgress_t loaded;

    if (!progress) return false;
    wc3_campaign_progress_init(progress);
    if (!path || !*path) return false;

    errno = 0;
    file = fopen(path, "rb");
#ifdef __EMSCRIPTEN__
    if (!file) {
        WC3_CampaignProgressRestoreJS(path);
        errno = 0;
        file = fopen(path, "rb");
    }
#endif
    if (!file) {
        if (errno != ENOENT) {
            fprintf(stderr, "Campaign progress: cannot open '%s' for reading: %s\n",
                    path, strerror(errno));
        }
        return false;
    }

    wc3_campaign_progress_init(&loaded);
    if (!campaign_progress_read_u32(file, &magic) || magic != WC3_CAMPAIGN_PROGRESS_MAGIC ||
        !campaign_progress_read_u32(file, &version) || (version != 1u && version != WC3_CAMPAIGN_PROGRESS_VERSION) ||
        !campaign_progress_read_u32(file, &editions) || editions != WC3_CAMPAIGN_PROGRESS_EDITIONS ||
        !campaign_progress_read_u32(file, &campaigns) || campaigns != WC3_CAMPAIGN_PROGRESS_CAMPAIGNS ||
        !campaign_progress_read_u32(file, &missions) || missions != WC3_CAMPAIGN_PROGRESS_MISSIONS ||
        !campaign_progress_read_bytes(file, loaded.tutorial_known, sizeof(loaded.tutorial_known)) ||
        !campaign_progress_read_bytes(file, loaded.tutorial_cleared, sizeof(loaded.tutorial_cleared)) ||
        !campaign_progress_read_bytes(file, loaded.campaign_known, sizeof(loaded.campaign_known)) ||
        !campaign_progress_read_bytes(file, loaded.campaign_available, sizeof(loaded.campaign_available)) ||
        !campaign_progress_read_bytes(file, loaded.mission_known, sizeof(loaded.mission_known)) ||
        !campaign_progress_read_bytes(file, loaded.mission_available, sizeof(loaded.mission_available)) ||
        (version >= 2 &&
         (!campaign_progress_read_bytes(file, loaded.mission_completed, sizeof(loaded.mission_completed)) ||
          !campaign_progress_read_bytes(file, loaded.mission_best_difficulty,
                                        sizeof(loaded.mission_best_difficulty))))) {
        fprintf(stderr, "Campaign progress: invalid or unsupported file '%s'\n", path);
        fclose(file);
        return false;
    }

    /* Reject trailing bytes and corrupt completion values instead of promoting
     * an invalid record to permanent player progress. */
    if (fgetc(file) != EOF || ferror(file)) {
        fclose(file);
        return false;
    }
    FOR_LOOP(e, WC3_CAMPAIGN_PROGRESS_EDITIONS) {
        FOR_LOOP(c, WC3_CAMPAIGN_PROGRESS_CAMPAIGNS) {
            FOR_LOOP(m, WC3_CAMPAIGN_PROGRESS_MISSIONS) {
                uint8_t const completed = loaded.mission_completed[e][c][m];
                uint8_t const best = loaded.mission_best_difficulty[e][c][m];
                if (completed > 1 || best > 3 || (!completed && best)) {
                    fclose(file);
                    return false;
                }
            }
        }
    }
    if (fclose(file) != 0) return false;
    *progress = loaded;
    return true;
}

bool wc3_campaign_progress_save(cstring_t path, wc3CampaignProgress_t const *progress) {
    PATHSTR tmp, backup;
    FILE *file;
    bool ok;
    bool had_backup = false;

    if (!path || !*path || !progress) return false;
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp) ||
        snprintf(backup, sizeof(backup), "%s.bak", path) >= (int)sizeof(backup)) {
        fprintf(stderr, "Campaign progress: path is too long\n");
        return false;
    }

    file = fopen(tmp, "wb");
    if (!file) {
        fprintf(stderr, "Campaign progress: cannot open '%s' for writing: %s\n",
                tmp, strerror(errno));
        return false;
    }

    ok = campaign_progress_write_u32(file, WC3_CAMPAIGN_PROGRESS_MAGIC) &&
         campaign_progress_write_u32(file, WC3_CAMPAIGN_PROGRESS_VERSION) &&
         campaign_progress_write_u32(file, WC3_CAMPAIGN_PROGRESS_EDITIONS) &&
         campaign_progress_write_u32(file, WC3_CAMPAIGN_PROGRESS_CAMPAIGNS) &&
         campaign_progress_write_u32(file, WC3_CAMPAIGN_PROGRESS_MISSIONS) &&
         campaign_progress_write_bytes(file, progress->tutorial_known,
                                       sizeof(progress->tutorial_known)) &&
         campaign_progress_write_bytes(file, progress->tutorial_cleared,
                                       sizeof(progress->tutorial_cleared)) &&
         campaign_progress_write_bytes(file, progress->campaign_known,
                                       sizeof(progress->campaign_known)) &&
         campaign_progress_write_bytes(file, progress->campaign_available,
                                       sizeof(progress->campaign_available)) &&
         campaign_progress_write_bytes(file, progress->mission_known,
                                       sizeof(progress->mission_known)) &&
         campaign_progress_write_bytes(file, progress->mission_available,
                                       sizeof(progress->mission_available)) &&
         campaign_progress_write_bytes(file, progress->mission_completed,
                                       sizeof(progress->mission_completed)) &&
         campaign_progress_write_bytes(file, progress->mission_best_difficulty,
                                       sizeof(progress->mission_best_difficulty));
    if (fclose(file) != 0) ok = false;
    if (!ok) {
        fprintf(stderr, "Campaign progress: failed while writing '%s'\n", tmp);
        remove(tmp);
        return false;
    }

    remove(backup);
    errno = 0;
    if (rename(path, backup) == 0) {
        had_backup = true;
    } else if (errno != ENOENT) {
        fprintf(stderr, "Campaign progress: cannot preserve previous '%s': %s\n",
                path, strerror(errno));
        remove(tmp);
        return false;
    }

    if (rename(tmp, path) != 0) {
        int const install_errno = errno;
        if (had_backup && rename(backup, path) != 0) {
            fprintf(stderr, "Campaign progress: failed to restore previous '%s': %s\n",
                    path, strerror(errno));
        }
        fprintf(stderr, "Campaign progress: cannot install '%s': %s\n",
                path, strerror(install_errno));
        remove(tmp);
        return false;
    }

    if (had_backup) remove(backup);
#ifdef __EMSCRIPTEN__
    WC3_CampaignProgressPersistJS(path);
#endif
    return true;
}
