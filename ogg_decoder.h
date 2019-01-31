#ifndef OGGDECODER_H
#define OGGDECODER_H

#include "common.h"

struct _OGGDecoder;
typedef struct _OGGDecoder OGGDecoder;

#ifdef __cplusplus
extern "C" {
#endif

OGGDecoder* OGGDecoder_new(FramesOutputCallback dataCallback, void* userData);
void OGGDecoder_init(OGGDecoder* state);
void OGGDecoder_free(OGGDecoder* state);

int OGGDecoder_getSampleRate(OGGDecoder* state);
int OGGDecoder_getChannelsCount(OGGDecoder* state);

int OGGDecoder_writeData(OGGDecoder* state, const void* data, int length);
void OGGDecoder_finalize(OGGDecoder* state);

#ifdef __cplusplus
}
#endif

#endif
