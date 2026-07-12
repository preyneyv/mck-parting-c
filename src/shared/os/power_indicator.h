#pragma once

#include <stdint.h>

#include <platform/peripheral.h>
#include <u8g2.h>

void power_indicator_draw(u8g2_t *u8g2, int16_t right_x, int16_t top_y,
                          platform_power_state_t power);
