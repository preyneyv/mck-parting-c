#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <platform/input.h>

typedef struct
{
  bool confirmation_required;
  platform_input_mask_t wake_button;
} platform_wake_result_t;

platform_wake_result_t platform_sleep_enter(uint32_t quick_wake_ms);
