// Adapted from
// https://people.ece.cornell.edu/land/courses/ece4760/RP2040/C_SDK_fixed_pt/speed_test/Fixed_point_test.c

#pragma once

#include <limits.h>
#include <stdint.h>

/* Q1.15 fixed point: resolution 2^-15, range [-0.99997, +0.99997]. */
typedef int16_t q1x15;

#define Q1X15_ONE ((q1x15)INT16_MAX)
#define Q1X15_ZERO ((q1x15)0)
#define Q1X15_NEG_ONE ((q1x15) - INT16_MAX)

static inline q1x15 q1x15_clamp_s32(int32_t a)
{
  if (a > Q1X15_ONE)
    a = Q1X15_ONE;
  if (a < Q1X15_NEG_ONE)
    a = Q1X15_NEG_ONE;
  return (q1x15)a;
}

static inline q1x15 q1x15_mul(q1x15 a, q1x15 b)
{
  return (q1x15)(((int32_t)a * (int32_t)b) >> 15);
}

/* Add without saturating. */
static inline q1x15 q1x15_add_unchecked(q1x15 a, q1x15 b) { return a + b; }

/* Convert a float in [-1.0, +1.0]. */
static inline q1x15 q1x15_f(float a)
{
  int32_t v = (int32_t)(a * (float)Q1X15_ONE);
  return q1x15_clamp_s32(v);
}

/* Convert a double in [-1.0, +1.0]. */
static inline q1x15 q1x15_d(double a)
{
  int32_t v = (int32_t)(a * (double)Q1X15_ONE);
  return q1x15_clamp_s32(v);
}

/* Map [0, mag] onto [0, +1.0]. */
static inline q1x15 q1x15_mag(uint16_t a, uint16_t mag)
{
  return (q1x15)((int32_t)a * (int32_t)Q1X15_ONE / mag);
}
