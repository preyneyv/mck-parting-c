#pragma once

#include <limits.h>
#include <stdint.h>

/* Q1.31 fixed point: resolution 2^-31, range [-1.0, +1.0]. Multiplying two
 * Q1.31 values is slow on RP2040, so use it sparingly. */
typedef int32_t q1x31;

#define Q1X31_ONE ((q1x31)INT32_MAX)
#define Q1X31_ZERO ((q1x31)0)
#define Q1X31_NEG_ONE ((q1x31)INT32_MIN)

static inline q1x31 q1x31_clamp_s64(int64_t a)
{
  if (a > Q1X31_ONE)
    a = Q1X31_ONE;
  if (a < Q1X31_NEG_ONE)
    a = Q1X31_NEG_ONE;
  return (q1x31)a;
}

static inline q1x31 q1x31_f(float a)
{
  int64_t v = (int64_t)(a * (float)Q1X31_ONE);
  return q1x31_clamp_s64(v);
}
