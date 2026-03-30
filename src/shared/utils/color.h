#pragma once

#include <platform/leds.h>

static inline uint8_t u8_lerp(uint8_t a, uint8_t b, float t)
{
    if (t <= 0.0f)
        return a;
    if (t >= 1.0f)
        return b;
    return (uint8_t)(a + (int16_t)((b - a) * t));
}

static inline color_t color_lerp(color_t a, color_t b, float t)
{
    return rgba(
        u8_lerp(a.r, b.r, t),
        u8_lerp(a.g, b.g, t),
        u8_lerp(a.b, b.b, t),
        255);
}

static inline color_t color_scale(color_t c, float scale)
{
    if (scale < 0.0f)
        scale = 0.0f;
    if (scale > 1.0f)
        scale = 1.0f;

    uint8_t r = (uint8_t)((float)c.r * scale);
    uint8_t g = (uint8_t)((float)c.g * scale);
    uint8_t b = (uint8_t)((float)c.b * scale);
    return rgba(r, g, b, 255);
}

static inline color_t color_cap(color_t c, uint8_t cap)
{
    if (cap >= 255)
        return c;

    float scale = (float)cap / 255.0f;
    return color_scale(c, scale);
}
