#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <platform/input.h>
#include <prism/graphics/color.h>

bool wake_confirmation_active(void);
bool wake_confirmation_expired(void);
uint8_t wake_confirmation_brightness_scale(uint8_t configured);
void wake_confirmation_start(platform_input_mask_t button);
void wake_confirmation_cancel(void);
void wake_confirmation_tick(void);
void wake_confirmation_frame(const uint8_t *app_framebuffer);
color_t wake_confirmation_blend_led(uint8_t led, color_t app_color);
void wake_confirmation_release_tick(void);
