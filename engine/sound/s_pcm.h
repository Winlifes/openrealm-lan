#ifndef S_PCM_H
#define S_PCM_H

#include <stdint.h>
#include <stdbool.h>

/* Compressed input is borrowed until Close. Output is interleaved stereo S16
 * at 44100 Hz; decoding is incremental, including background music. */
typedef struct sPCM_s sPCM_t;
sPCM_t *S_PCM_Open(uint8_t const *data, uint32_t size, char const *path);
uint32_t S_PCM_Read(sPCM_t *decoder, int16_t *out, uint32_t frames);
bool S_PCM_Seek(sPCM_t *decoder, uint32_t milliseconds);
bool S_PCM_Finished(sPCM_t const *decoder);
void S_PCM_Close(sPCM_t *decoder);

#endif
