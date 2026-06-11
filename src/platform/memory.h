#pragma once

#include <pico.h>

#define PLATFORM_TIME_CRITICAL_FUNC(name) __no_inline_not_in_flash_func(name)
#define PLATFORM_SCRATCH_X(group) __scratch_x(group)
#define PLATFORM_SCRATCH_Y(group) __scratch_y(group)
