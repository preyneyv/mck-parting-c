#pragma once

#include <stdint.h>

#define PRISM_STATE_LIFECYCLE_REPORT_MAGIC 0x53544154u

typedef struct
{
  uint32_t magic;
  uint32_t zero_value;
  uint32_t initialized_value;
  uint32_t first_number;
  uint32_t second_number;
  uint32_t pointer_to_number_value;
  uint32_t pointer_to_zero_matches;
  uint32_t large_bss_first;
  uint32_t large_bss_last;
  uint32_t writable_string_first;
} prism_state_lifecycle_report_t;
