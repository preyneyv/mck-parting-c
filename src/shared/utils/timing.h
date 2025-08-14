#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <pico/time.h>

// Everything stored in microseconds
typedef struct {
  absolute_time_t start_time;
  absolute_time_t end_time;
  uint64_t aggregate_time;
  uint32_t count;
} TimingInstrumenter;

// Initialize/reset all counters
static inline void ti_init(TimingInstrumenter *ti) {
  ti->start_time = 0;
  ti->end_time = 0;
  ti->aggregate_time = 0;
  ti->count = 0;
}

// Record current timestamp
static inline void ti_start(TimingInstrumenter *ti) {
  ti->start_time = get_absolute_time();
}

// Stop and accumulate
static inline void ti_stop(TimingInstrumenter *ti) {
  ti->end_time = get_absolute_time();
  ti->aggregate_time += absolute_time_diff_us(ti->start_time, ti->end_time);
  ti->count++;
}

// Last measured interval (µs)
static inline uint64_t ti_get_elapsed_us(const TimingInstrumenter *ti) {
  return absolute_time_diff_us(ti->start_time, ti->end_time);
}

// Average time in milliseconds.
// If reset==true, clear the tally after computing.
static inline double ti_get_average_ms(TimingInstrumenter *ti, bool reset) {
  if (ti->count == 0)
    return 0.0;
  double avg_us = (double)ti->aggregate_time / (double)ti->count;
  if (reset) {
    ti->aggregate_time = 0;
    ti->count = 0;
  }
  return avg_us * 1e-3;
}
