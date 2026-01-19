#pragma once

// Things that don't belong anywhere else

static inline float ease_out_cubic(float t)
{
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}
