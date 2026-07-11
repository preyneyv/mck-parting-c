#include <platform/sleep.h>

platform_wake_result_t platform_sleep_enter(uint32_t quick_wake_ms)
{
  (void)quick_wake_ms;
  return (platform_wake_result_t){0};
}
