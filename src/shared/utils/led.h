#pragma once

#include <platform/leds.h>
#include <shared/engine.h>

static inline void led_set_both(color_t color)
{
    engine_led_set(LED_L, color);
    engine_led_set(LED_R, color);
}

static inline void led_set(uint8_t led, color_t color)
{
    if (led >= LED_COUNT)
        return;

    engine_led_set(led, color);
}

static inline void led_clear(void)
{
    led_set_both(rgba(0, 0, 0, 255));
}
