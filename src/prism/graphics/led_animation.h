#pragma once

#include <stdbool.h>
#include <math.h>
#include <stdint.h>

#define LED_SINE_LUT_SIZE 256u

static inline void led_sine_lut_init(float lut[LED_SINE_LUT_SIZE])
{
    const float two_pi = 6.28318530718f;
    for (uint32_t i = 0; i < LED_SINE_LUT_SIZE; i++)
    {
        float phase = (float)i / (float)LED_SINE_LUT_SIZE;
        lut[i] = sinf(two_pi * phase);
    }
}

static inline float led_sine_lut_lerp(float x)
{
    static bool initialized = false;
    static float lut[LED_SINE_LUT_SIZE];

    if (!initialized)
    {
        led_sine_lut_init(lut);
        initialized = true;
    }

    const float inv_two_pi = 0.15915494309f;
    float cycles = x * inv_two_pi;
    cycles = cycles - floorf(cycles);

    float pos = cycles * (float)LED_SINE_LUT_SIZE;
    uint32_t idx = (uint32_t)pos;
    float frac = pos - (float)idx;

    uint32_t next = (idx + 1u) & (LED_SINE_LUT_SIZE - 1u);
    float y0 = lut[idx & (LED_SINE_LUT_SIZE - 1u)];
    float y1 = lut[next];
    return y0 + (y1 - y0) * frac;
}

static inline float led_sine_pulse(uint32_t tick, float frequency)
{
    float s = led_sine_lut_lerp((float)tick * frequency);
    return (1.0f + s) * 0.5f;
}

static inline float led_envelope_update(
    float current,
    float target,
    float attack_rate_per_sec,
    float release_rate_per_sec,
    float dt_s)
{
    float rate = (target > current) ? attack_rate_per_sec : release_rate_per_sec;
    float blend = rate * dt_s;
    if (blend < 0.0f)
        blend = 0.0f;
    if (blend > 1.0f)
        blend = 1.0f;
    return current + (target - current) * blend;
}
