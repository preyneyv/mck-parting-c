#pragma once

#include <stddef.h>
#include <stdint.h>

#ifndef ANIM_MAX
#define ANIM_MAX 32 // tune for your needs
#endif

typedef enum {
  ANIM_EASE_LINEAR,
  ANIM_EASE_INOUT_QUAD,
  ANIM_EASE_OUT_CUBIC,
} anim_ease_t;

typedef void (*anim_done_fn)(void *ctx);

typedef struct {
  volatile int32_t *out; // target integer variable
  int32_t start;         // start value (int)
  int32_t end;           // end value (int)
  int32_t delta;         // end - start

  uint32_t p_q16;  // progress in Q16.16 (0..65536)
  uint32_t dp_q16; // increment per tick in Q16.16 (≈ 1/duration)

  anim_ease_t ease;
  uint8_t active;

  anim_done_fn on_done;
  void *ctx;

} anim_slot_t;

typedef struct {
  anim_slot_t slots[ANIM_MAX];
} anim_sys_t;

extern anim_sys_t g_anim;

// ---------- core ----------
void anim_init(void);
int anim_to(volatile int32_t *out, int32_t to, uint32_t duration_ticks,
            anim_ease_t ease, anim_done_fn on_done, void *ctx);
static inline int anim_by(volatile int32_t *out, int32_t by,
                          uint32_t duration_ticks, anim_ease_t ease,
                          anim_done_fn on_done, void *ctx) {
  return anim_to(out, *out + by, duration_ticks, ease, on_done, ctx);
}
void anim_cancel(volatile int32_t *out, int snap_to_end);
void anim_tick(void);

static inline uint32_t q16_from_ratio(uint32_t num, uint32_t den) {
  // returns round((num << 16) / den), guarding den==0 at callsites.
  uint64_t t = ((uint64_t)num << 16) + (den >> 1);
  return (uint32_t)(t / den);
}
