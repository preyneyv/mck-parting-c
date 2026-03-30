#pragma once

#include <platform/leds.h>
#include <shared/engine.h>

static inline void led_set_both(color_t color)
{
    g_engine.led_colors[LED_L] = color;
    g_engine.led_colors[LED_R] = color;
}

static inline void led_set(uint8_t led, color_t color)
{
    if (led >= LED_COUNT)
        return;

    g_engine.led_colors[led] = color;
}

static inline void led_clear(void)
{
    led_set_both(rgba(0, 0, 0, 255));
}
