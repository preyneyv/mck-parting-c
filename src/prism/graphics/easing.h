#pragma once

static inline float ease_out_cubic(float t)
{
  float inverse = 1.0f - t;
  return 1.0f - inverse * inverse * inverse;
}

