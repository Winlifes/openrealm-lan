#ifndef AUDIO_PATH_H
#define AUDIO_PATH_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif

/* Keep exact-path priority. Metadata may name an older container while the
 * installation stores the same recording as OGG. Never change the basename or
 * retry a present but corrupt file. Duration and playback share this resolver. */
static inline void *Audio_ReadFile(char const *path, uint32_t *size,
                                  void *(*read_file)(char const *, uint32_t *),
                                  void (*free_file)(void *)) {
    *size = 0;
    if (!path || !*path) return NULL;
    void *data = read_file(path, size);
    if (data && *size) return data;
    if (data) free_file(data);
    char const *dot = path ? strrchr(path, '.') : NULL;
    /* Newer sound tables use asset ids without a directory/extension. The
     * deployment index contains only unambiguous actual asset basenames. */
    if (!dot && !strchr(path, '/') && !strchr(path, '\\')) {
        uint32_t index_size = 0;
        char *index = read_file("audio-assets.index", &index_size);
        if (index && index_size <= 4 * 1024 * 1024) {
            char const *cursor = index, *limit = index + index_size;
            size_t key_size = strlen(path);
            while (cursor < limit) {
                char const *line_end = memchr(cursor, '\n', (size_t)(limit - cursor));
                if (!line_end) line_end = limit;
                char const *tab = memchr(cursor, '\t', (size_t)(line_end - cursor));
                if (tab && (size_t)(tab - cursor) == key_size && !strncasecmp(cursor, path, key_size)) {
                    char resolved[1024]; size_t length = (size_t)(line_end - tab - 1);
                    if (length && length < sizeof(resolved)) {
                        memcpy(resolved, tab + 1, length); resolved[length] = 0;
                        *size = 0; data = read_file(resolved, size);
                        if (data && *size) { free_file(index); return data; }
                        if (data) free_file(data);
                    }
                    break;
                }
                cursor = line_end < limit ? line_end + 1 : limit;
            }
        }
        if (index) free_file(index);
    }
    char const *extensions[] = { ".ogg", ".wav", ".mp3", ".flac" };
    bool known = false;
    for (unsigned i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++)
        if (dot && !strcasecmp(dot, extensions[i])) known = true;
    if (!known) return NULL;
    char alternate[1024];
    size_t prefix = (size_t)(dot - path);
    if (prefix + 6 > sizeof(alternate)) return NULL;
    for (unsigned i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++) {
        if (!strcasecmp(dot, extensions[i])) continue;
        snprintf(alternate, sizeof(alternate), "%.*s%s", (int)prefix, path, extensions[i]);
        *size = 0;
        data = read_file(alternate, size);
        if (data && *size) return data;
        if (data) free_file(data);
    }
    *size = 0;
    return NULL;
}

#endif
