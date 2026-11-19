#ifndef TEXTURE_PATH_H
#define TEXTURE_PATH_H

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif

/* One lookup order for rendering and data-side admission: exact first, then
 * legacy authored names in their BLP/DDS store representation. DDS/PCX are exact. */
static inline bool Texture_PathCandidate(char const *name, unsigned candidate,
                                         char *path, size_t capacity) {
    char const *ext;
    size_t length;
    char const *suffix;
    if (!name || !*name || !path || !capacity) return false;
    if (!candidate) return snprintf(path, capacity, "%s", name) < (int)capacity;
    ext = strrchr(name, '.');
    if (ext && (!strcasecmp(ext, ".dds") || !strcasecmp(ext, ".pcx"))) return false;
    if (ext && !strcasecmp(ext, ".blp")) {
        if (candidate != 1) return false;
        length = (size_t)(ext - name); suffix = ".dds";
    } else {
        if (candidate > 2) return false;
        length = ext && (!strcasecmp(ext, ".tga") || !strcasecmp(ext, ".tif"))
            ? (size_t)(ext - name) : strlen(name);
        suffix = candidate == 1 ? ".blp" : ".dds";
    }
    if (length + strlen(suffix) >= capacity) return false;
    return snprintf(path, capacity, "%.*s%s", (int)length, name, suffix) < (int)capacity;
}

#endif
