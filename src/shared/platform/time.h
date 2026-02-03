#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef int64_t platform_time_t;

platform_time_t platform_time_now();
int64_t platform_time_diff_us(platform_time_t start, platform_time_t end);
platform_time_t platform_time_delayed_by_ms(platform_time_t start, int32_t ms);
bool platform_time_reached(platform_time_t target);
platform_time_t platform_time_nil();
platform_time_t platform_time_at_end();
