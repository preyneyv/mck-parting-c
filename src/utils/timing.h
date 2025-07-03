#ifndef _UTILS_TIMING_H
#define _UTILS_TIMING_H

#include <stdint.h>
#include <stdbool.h>

#include <pico/time.h>

// Everything stored in microseconds
typedef struct
{
    uint64_t start_time;
    uint64_t end_time;
    uint64_t aggregate_time;
    uint32_t count;
} TimingInstrumenter;

// Initialize/reset all counters
static inline void ti_init(TimingInstrumenter *ti)
{
    ti->start_time = 0;
    ti->end_time = 0;
    ti->aggregate_time = 0;
    ti->count = 0;
}

// Record current timestamp
static inline void ti_start(TimingInstrumenter *ti)
{
    ti->start_time = time_us_64();
}

// Stop and accumulate
static inline void ti_stop(TimingInstrumenter *ti)
{
    ti->end_time = time_us_64();
    ti->aggregate_time += (ti->end_time - ti->start_time);
    ti->count++;
}

// Last measured interval (µs)
static inline uint64_t ti_get_elapsed_us(const TimingInstrumenter *ti)
{
    return ti->end_time - ti->start_time;
}

// Average time in milliseconds.
// If reset==true, clear the tally after computing.
static inline double ti_get_average_ms(TimingInstrumenter *ti, bool reset)
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

#endif // _UTILS_TIMING_H
