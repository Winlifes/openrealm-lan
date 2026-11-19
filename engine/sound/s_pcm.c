#include "s_local.h"
#include "s_pcm.h"
#include <limits.h>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#include "vendor/minimp3/minimp3.h"
#include "vendor/dr_flac/dr_flac.h"

struct sPCM_s {
    enum { PCM_OGG, PCM_MP3, PCM_FLAC } format;
    stb_vorbis *vorbis;
    drflac *flac;
    mp3dec_t mp3;
    ARRAY(uint8_t const, data); /* borrowed compressed input */
    uint32_t offset;
    short block[4096 * 2];
    int frames, channels, rate;
    double position;
    bool eof;
};

sPCM_t *S_PCM_Open(uint8_t const *data, uint32_t size, char const *path) {
    if (!data || size < 4 || size > INT_MAX) return NULL;
    sPCM_t *d = calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->data = data; ARRAY_COUNT(d->data) = size;
    if (!memcmp(data, "OggS", 4)) {
        int error = 0;
        d->format = PCM_OGG;
        d->vorbis = stb_vorbis_open_memory(data, (int)size, &error, NULL);
        if (!d->vorbis) { S_PCM_Close(d); return NULL; }
        stb_vorbis_info info = stb_vorbis_get_info(d->vorbis);
        d->channels = info.channels; d->rate = info.sample_rate;
    } else if (!memcmp(data, "fLaC", 4)) {
        d->format = PCM_FLAC;
        d->flac = drflac_open_memory(data, size, NULL);
        if (!d->flac) { S_PCM_Close(d); return NULL; }
        d->channels = d->flac->channels; d->rate = d->flac->sampleRate;
    } else {
        char const *extension = path ? strrchr(path, '.') : NULL;
        bool signature = (size >= 3 && !memcmp(data, "ID3", 3)) ||
                         (data[0] == 0xff && (data[1] & 0xe0) == 0xe0);
        if (!signature && (!extension || strcasecmp(extension, ".mp3"))) { S_PCM_Close(d); return NULL; }
        d->format = PCM_MP3;
        mp3dec_init(&d->mp3);
        /* MP3 headers are validated by the decoder, not the filename alone. */
        d->channels = 2; d->rate = 44100;
    }
    if ((d->channels != 1 && d->channels != 2) || d->rate <= 0) {
        S_PCM_Close(d); return NULL;
    }
    return d;
}

static bool S_PCM_NextBlock(sPCM_t *d) {
    d->position -= d->frames;
    d->frames = 0;
    if (d->format == PCM_OGG) {
        d->frames = stb_vorbis_get_samples_short_interleaved(d->vorbis, d->channels,
                                                            d->block, 4096 * d->channels);
    } else if (d->format == PCM_FLAC) {
        d->frames = (int)drflac_read_pcm_frames_s16(d->flac, 4096, d->block);
    } else {
        while (d->offset < ARRAY_COUNT(d->data)) {
            mp3dec_frame_info_t info;
            int remaining = (int)(ARRAY_COUNT(d->data) - d->offset);
            int frames = mp3dec_decode_frame(&d->mp3, d->data + d->offset, remaining, d->block, &info);
            if (info.frame_bytes <= 0 || info.frame_bytes > remaining) break;
            d->offset += info.frame_bytes;
            if (!frames) continue;
            if ((info.channels != 1 && info.channels != 2) || info.hz <= 0) break;
            d->frames = frames; d->channels = info.channels; d->rate = info.hz;
            break;
        }
    }
    d->eof = d->frames == 0;
    return !d->eof;
}

uint32_t S_PCM_Read(sPCM_t *d, int16_t *out, uint32_t frames) {
    if (!d || !out || d->eof) return 0;
    uint32_t written = 0;
    while (written < frames) {
        while (d->position >= d->frames)
            if (!S_PCM_NextBlock(d)) return written;
        int i = (int)d->position;
        out[written * 2] = d->block[i * d->channels];
        out[written * 2 + 1] = d->block[i * d->channels + (d->channels == 2)];
        d->position += (double)d->rate / 44100.0;
        written++;
    }
    return written;
}

bool S_PCM_Seek(sPCM_t *d, uint32_t milliseconds) {
    if (!d) return false;
    if (d->format == PCM_OGG && !stb_vorbis_seek_start(d->vorbis)) return false;
    if (d->format == PCM_FLAC && !drflac_seek_to_pcm_frame(d->flac, 0)) return false;
    if (d->format == PCM_MP3) mp3dec_init(&d->mp3);
    d->offset = 0; d->frames = 0; d->position = 0; d->eof = false;
    uint64_t remaining = (uint64_t)milliseconds * 44100 / 1000;
    short discard[4096 * 2];
    while (remaining) {
        uint32_t count = S_PCM_Read(d, discard, (uint32_t)MIN(remaining, 4096));
        if (!count) break;
        remaining -= count;
    }
    return true;
}

bool S_PCM_Finished(sPCM_t const *d) { return !d || d->eof; }

void S_PCM_Close(sPCM_t *d) {
    if (!d) return;
    if (d->vorbis) stb_vorbis_close(d->vorbis);
    if (d->flac) drflac_close(d->flac);
    free(d);
}

sfxcache_t *s_ogg_decode(uint8_t const *data, uint32_t size) {
    sPCM_t *d = S_PCM_Open(data, size, ".ogg");
    sfxcache_t *cache = NULL;
    size_t capacity = 0;
    uint32_t length = 0, count;
    short block[4096 * 2];
    if (!d) return NULL;
    while ((count = S_PCM_Read(d, block, 4096))) {
        if (length > INT_MAX - count) goto fail;
        size_t needed = length + count;
        if (needed > capacity) {
            size_t next = MAX(needed, capacity ? capacity * 2 : 4096);
            if (next > (SIZE_MAX - sizeof(*cache)) / sizeof(short) + 1) goto fail;
            void *grown = realloc(cache, sizeof(*cache) + (next - 1) * sizeof(short));
            if (!grown) goto fail;
            cache = grown; capacity = next;
        }
        for (uint32_t i = 0; i < count; i++)
            cache->data[length++] = (short)(((int)block[i * 2] + block[i * 2 + 1]) / 2);
    }
    S_PCM_Close(d);
    if (!length) { free(cache); return NULL; }
    cache->length = (int)length; cache->loopstart = -1;
    return cache;
fail:
    S_PCM_Close(d); free(cache); return NULL;
}
