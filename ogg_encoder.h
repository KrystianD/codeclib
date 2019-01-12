#ifndef OGGENCODER_H
#define OGGENCODER_H

#include "common.h"

struct _OGGEncoder;
typedef struct _OGGEncoder OGGEncoder;

#ifdef __cplusplus
extern "C" {
#endif

OGGEncoder* OGGEncoder_new(int sampleRate, int channels, int quality, BytesOutputCallback dataCallback, void* userData);
void OGGEncoder_init(OGGEncoder* state);
void OGGEncoder_free(OGGEncoder* state);

void OGGEncoder_writeFrame(OGGEncoder* state, int16_t frameSamples[]);
void OGGEncoder_writeFrames(OGGEncoder* state, int16_t framesSamples[], int framesCount);
void OGGEncoder_finalize(OGGEncoder* state);

#ifdef __cplusplus
}
#endif

#endif
