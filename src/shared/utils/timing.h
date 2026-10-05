#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <platform/time.h>

/* Accumulates the duration of repeated start/stop intervals, in microseconds. */
typedef struct
{
  platform_time_t start_time;
  platform_time_t end_time;
  uint64_t aggregate_time;
  uint32_t count;
} timing_stats_t;

static inline void ti_init(timing_stats_t *ti)
{
  ti->start_time = 0;
  ti->end_time = 0;
  ti->aggregate_time = 0;
  ti->count = 0;
}

static inline void ti_start(timing_stats_t *ti)
{
  ti->start_time = platform_now_us();
}

static inline uint64_t ti_stop(timing_stats_t *ti)
{
  ti->end_time = platform_now_us();
  uint64_t elapsed =
      (uint64_t)platform_time_diff_us(ti->start_time, ti->end_time);
  ti->aggregate_time += elapsed;
  ti->count++;

  return elapsed;
}

/* Average interval in milliseconds, optionally clearing the tally. */
static inline double ti_get_average_ms(timing_stats_t *ti, bool reset)
{
  if (ti->count == 0)
    return 0.0;
  double avg_us = (double)ti->aggregate_time / (double)ti->count;
  if (reset)
  {
    ti->aggregate_time = 0;
    ti->count = 0;
  }
  return avg_us * 1e-3;
}
