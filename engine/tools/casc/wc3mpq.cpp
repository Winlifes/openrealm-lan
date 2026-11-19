// wc3mpq — bulk MPQ packer (StormLib) for the WC3 classic set.
// usage: wc3mpq <archive.mpq> <manifest.tsv>
//   manifest lines: <src-path>\t<archive-name>   (archive names use backslashes)
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include "StormLib.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <archive.mpq> <manifest.tsv>\n", argv[0]);
        return 2;
    }
    FILE *m = fopen(argv[2], "r");
    if (!m) { perror("manifest"); return 2; }

    long long n = 0;
    char line[4096];
    while (fgets(line, sizeof(line), m)) if (line[0] && line[0] != '#') n++;
    rewind(m);
    if (n == 0) { fprintf(stderr, "empty manifest\n"); return 2; }

    HANDLE h = NULL;
    if (!SFileCreateArchive(argv[1], MPQ_CREATE_ARCHIVE_V1 | MPQ_CREATE_LISTFILE | MPQ_CREATE_ATTRIBUTES,
                            (DWORD)(n + 256), &h)) {
        fprintf(stderr, "create failed: %u\n", SErrGetLastError());
        return 1;
    }
    SFileSetMaxFileCount(h, (DWORD)(n + 8192));

    long long ok = 0, fail = 0, bytes = 0;
    while (fgets(line, sizeof(line), m)) {
        if (!line[0] || line[0] == '#') continue;
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = '\0';
        char *name = tab + 1;
        size_t ln = strlen(name);
        while (ln && (name[ln - 1] == '\n' || name[ln - 1] == '\r')) name[--ln] = '\0';
        if (!ln) continue;

        struct stat st;
        int r;
        if (stat(line, &st) == 0 && st.st_size > 0 && st.st_size < (32LL * 1024 * 1024)) {
            r = SFileAddFileEx(h, line, name, MPQ_FILE_COMPRESS, MPQ_COMPRESSION_ZLIB, MPQ_COMPRESSION_NEXT_SAME);
            bytes += st.st_size;
        } else if (st.st_size > 0) {
            r = SFileAddFileEx(h, line, name, 0, 0, 0);
            bytes += st.st_size;
        } else {
            r = SFileAddFileEx(h, line, name, 0, 0, 0); /* zero-byte ok */
        }
        if (!r) {
            fail++;
            if (fail <= 25) fprintf(stderr, "add fail (%u): %s -> %s\n", SErrGetLastError(), line, name);
        } else {
            ok++;
            if (ok % 2000 == 0) printf("  ... %lld files\n", ok);
        }
    }
    fclose(m);

    if (!SFileCloseArchive(h)) {
        fprintf(stderr, "close failed: %u\n", SErrGetLastError());
        return 1;
    }
    printf("packed ok=%lld fail=%lld src_bytes=%lld -> %s\n", ok, fail, bytes, argv[1]);
    return fail ? 1 : 0;
}
