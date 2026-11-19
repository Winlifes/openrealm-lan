#include "s_local.h"

#define DR_FLAC_IMPLEMENTATION
#include "vendor/dr_flac/dr_flac.h"

#include <limits.h>
#include <stdint.h>

/* SD Reforged data ships sounds as .flac; mirror s_mp3_decode's contract:
 * convert to mono S16 at 44100 with the same integer sample-walk the MP3
 * path uses, so the mixer sees one representation. */
sfxcache_t *s_flac_decode(uint8_t const *data, uint32_t size) {
    drflac *flac;
    drflac_int16 frame[4096 * 2];
    sfxcache_t *cache = NULL;
    size_t capacity = 0;
    double sample_pos = 0.0;
    int length = 0;

    if (!data || size < 4) return NULL;
    flac = drflac_open_memory(data, size, NULL);
    if (!flac) return NULL;
    if ((flac->channels != 1 && flac->channels != 2) || flac->sampleRate == 0) {
        drflac_close(flac);
        return NULL;
    }

    for (;;) {
        drflac_uint64 frames = drflac_read_pcm_frames_s16(flac, 4096, frame);
        drflac_uint64 i;

        if (frames == 0) break;
        for (i = 0; i < frames; i++) {
            int mixed = flac->channels == 2
                ? ((int)frame[i * 2] + (int)frame[i * 2 + 1]) / 2
                : frame[i];
            while (sample_pos < 1.0) {
                if (length == INT_MAX || ((size_t)length == capacity)) {
                    size_t next = capacity ? capacity * 2 : 4096;
                    sfxcache_t *grown;

                    if (next < (size_t)length + 1) next = (size_t)length + 1;
                    if (next > (SIZE_MAX - sizeof(*cache)) / sizeof(short) + 1) {
                        drflac_close(flac);
                        free(cache);
                        return NULL;
                    }
                    grown = realloc(cache, sizeof(*cache) + (next - 1) * sizeof(short));
                    if (!grown) {
                        drflac_close(flac);
                        free(cache);
                        return NULL;
                    }
                    cache = grown;
                    capacity = next;
                }
                cache->data[length++] = (short)mixed;
                sample_pos += (double)flac->sampleRate / 44100.0;
            }
            sample_pos -= 1.0;
        }
    }
    drflac_close(flac);
    if (!length) { free(cache); return NULL; }
    cache->length = length;
    cache->loopstart = -1;
    return cache;
}
