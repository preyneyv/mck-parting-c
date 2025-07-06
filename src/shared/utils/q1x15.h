// Adapted from
// https://people.ece.cornell.edu/land/courses/ece4760/RP2040/C_SDK_fixed_pt/speed_test/Fixed_point_test.c

#pragma once

#include <limits.h>
#include <stdint.h>

// ### fixed point 1.15
// res is 2^-15 = 3.0518e-5
// range is [-0.99997, +0.99997]
typedef int16_t q1x15;

static inline q1x15 q1x15_clamp_s32(int32_t a) {
  if (a > INT16_MAX)
    a = INT16_MAX;
  if (a < -INT16_MAX)
    a = -INT16_MAX;
  return (q1x15)a;
}

// convert float [-1.0, +1.0] to q1x15
static inline q1x15 q1x15_f(float a) {
  int32_t v = (int32_t)(a * (float)INT16_MAX);
  return q1x15_clamp_s32(v);
}

// convert q1x15 to float [-1.0, +1.0]
static inline float q1x15_to_float(q1x15 a) {
  return (float)(a) / (float)INT16_MAX;
}

static inline q1x15 q1x15_abs(q1x15 a) {
  if (a == INT16_MIN)
    return INT16_MAX;
  return (q1x15)(a < 0 ? -a : a);
}

static inline q1x15 q1x15_mul(q1x15 a, q1x15 b) {
  return (q1x15)(((int32_t)a * (int32_t)b) >> 15);
}

static inline q1x15 q1x15_div(q1x15 a, q1x15 b) {
  return (q1x15)((((signed int)(a) << 15) / (b)));
}

// add and clamp to prevent overflow
static inline q1x15 q1x15_add(q1x15 a, q1x15 b) {
  return q1x15_clamp_s32((int32_t)(a) + b);
}

// subtract and clamp to prevent overflow
static inline q1x15 q1x15_sub(q1x15 a, q1x15 b) {
  return q1x15_clamp_s32((int32_t)a - b);
}

// add without checking for overflow
static inline q1x15 q1x15_add_unchecked(q1x15 a, q1x15 b) { return a + b; }
// subtract without checking for overflow
static inline q1x15 q1x15_sub_unchecked(q1x15 a, q1x15 b) { return a - b; }
