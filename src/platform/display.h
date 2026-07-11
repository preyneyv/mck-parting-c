#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <u8g2.h>

void platform_display_init(void);
void platform_display_set_enabled(bool enabled);
void platform_display_set_contrast(uint8_t contrast);
u8g2_t *platform_display_get_u8g2(void);
