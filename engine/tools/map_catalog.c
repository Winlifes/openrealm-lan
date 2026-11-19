/* Inspect actual W3I metadata with the same bounded parser as menu/gameplay.
 * Usage: map_catalog <data root>; relative MPQ paths arrive on stdin. */
#include "common/mapinfo_read.h"
#include "common/mpq.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct allocation_s { struct allocation_s *next; void *data; } allocation_t;
static allocation_t *allocations;
static void *catalog_alloc(long size) {
    if (size < 0 || size > 16 * 1024 * 1024) exit(2);
    allocation_t *a = malloc(sizeof(*a)); if (!a) exit(2);
    a->data = calloc(1, size ? size : 1); if (!a->data) exit(2);
    a->next = allocations; allocations = a; return a->data;
}
static void catalog_free(void *p) { (void)p; }
static void quoted(char const *s) {
    putchar('"');
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { putchar('\\'); putchar(c); }
        else if (c < 32) printf("\\u%04x", c);
        else putchar(c);
    }
    putchar('"');
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    char path[4096], absolute[8192]; int failures = 0;
    while (fgets(path, sizeof(path), stdin)) {
        path[strcspn(path, "\r\n")] = 0;
        snprintf(absolute, sizeof(absolute), "%s/%s", argv[1], path);
        handle_t archive = NULL, file = NULL;
        bool ok = SFileOpenArchive(absolute, 0, 0, &archive) &&
                  SFileOpenFileEx(archive, "war3map.w3i", 0, &file);
        uint32_t size = ok ? SFileGetFileSize(file, NULL) : 0, read = 0;
        void *bytes = size && size < 4 * 1024 * 1024 ? malloc(size) : NULL;
        mapInfo_t info = {0};
        ok = bytes && SFileReadFile(file, bytes, size, &read, NULL) && read == size &&
             MapInfo_ParseSetup(bytes, size, &info, catalog_alloc, catalog_free, NULL);
        if (file) SFileCloseFile(file); if (archive) SFileCloseArchive(archive); free(bytes);
        if (!ok) { fprintf(stderr, "Invalid map metadata: %s\n", path); failures++; }
        else {
            printf("{\"path\":"); quoted(path); printf(",\"name\":"); quoted(info.mapName);
            printf(",\"flags\":%u,\"slots\":[", info.flags);
            unsigned count = 0;
            for (unsigned i = 0; i < MAX_PLAYERS; i++) {
                mapPlayer_t *p = &info.players[i];
                if (!p->used || (p->playerType != kPlayerTypeHuman && p->playerType != kPlayerTypeComputer)) continue;
                uint32_t team = count;
                if (!(info.flags & melee_map) && (info.flags & use_custom_forces))
                    for (unsigned j = 0; j < info.num_teams; j++)
                        if (info.teams[j].playerMasks & (1u << i)) { team = j; break; }
                printf("%s{\"player\":%u,\"race\":%u,\"team\":%u,\"color\":%u,\"computer\":%s}",
                    count++ ? "," : "", i, p->playerRace, team, i, p->playerType == kPlayerTypeComputer ? "true" : "false");
            }
            puts("]}");
        }
        while (allocations) { allocation_t *a = allocations; allocations = a->next; free(a->data); free(a); }
    }
    return failures ? 1 : 0;
}
