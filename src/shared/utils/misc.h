#pragma once

// Things that don't belong anywhere else

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

static inline float ease_out_cubic(float t)
{
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}
