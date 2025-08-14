#pragma once

typedef union {
  struct {
    int16_t x;
    int16_t y;
  };
  struct {
    int16_t w;
    int16_t h;
  };
  int16_t v[2];
} vec2_t;

static inline vec2_t vec2(int16_t x, int16_t y) {
  return (vec2_t){.x = x, .y = y};
}
static inline vec2_t vec2_zero(void) { return (vec2_t){.x = 0, .y = 0}; }
static inline vec2_t vec2_add(vec2_t a, vec2_t b) {
  return (vec2_t){.x = a.x + b.x, .y = a.y + b.y};
}

typedef union {
  struct {
    float x;
    float y;
  };
  struct {
    float w;
    float h;
  };
  float v[2];
} vec2f_t;

static inline vec2f_t vec2f(float x, float y) {
  return (vec2f_t){.x = x, .y = y};
}
static inline vec2f_t vec2f_zero(void) {
  return (vec2f_t){.x = 0.0f, .y = 0.0f};
}
static inline vec2f_t vec2f_add(vec2f_t a, vec2f_t b) {
  return (vec2f_t){.x = a.x + b.x, .y = a.y + b.y};
}
