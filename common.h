#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

typedef void (* BytesOutputCallback)(void* data, int length, void* userData);
typedef void (* FramesOutputCallback)(int16_t framesSamples[], int framesCount, void* userData);

#endif
