#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <prism/types.h>

#ifndef ANIM_MAX
#define ANIM_MAX 32
#endif

typedef prism_anim_ease_t anim_ease_t;
typedef prism_anim_done_fn anim_done_fn;

#define ANIM_EASE_LINEAR PRISM_ANIM_EASE_LINEAR
#define ANIM_EASE_INOUT_QUAD PRISM_ANIM_EASE_INOUT_QUAD
#define ANIM_EASE_OUT_CUBIC PRISM_ANIM_EASE_OUT_CUBIC

typedef struct {
  volatile int32_t *out; // target integer variable
  int32_t start;         // start value (int)
  int32_t end;           // end value (int)
  int32_t delta;         // end - start

  uint64_t elapsed_us;
  uint32_t duration_ms;

  anim_ease_t ease;
  uint8_t active;

  anim_done_fn on_done;
  void *ctx;

  bool is_sys; // sys animations are not paused and cancelled by app switch

} anim_slot_t;

typedef struct {
  anim_slot_t slots[ANIM_MAX];
  bool paused;
} anim_sys_t;

extern anim_sys_t g_anim;

void anim_init(void);

void anim_sys_set_paused(bool paused);
// clear all non-sys animations (used for scene switches)
void anim_sys_clear_all();
// a non-pausable animation (used by system UI)
int anim_sys_to(volatile int32_t *out, int32_t to, uint32_t duration_ms,
                anim_ease_t ease, anim_done_fn on_done, void *ctx);

// a pausable animation (used by apps)
int anim_to(volatile int32_t *out, int32_t to, uint32_t duration_ms,
            anim_ease_t ease, anim_done_fn on_done, void *ctx);
static inline int anim_by(volatile int32_t *out, int32_t by,
                          uint32_t duration_ms, anim_ease_t ease,
                          anim_done_fn on_done, void *ctx) {
  return anim_to(out, *out + by, duration_ms, ease, on_done, ctx);
}
void anim_cancel(volatile int32_t *out, int snap_to_end);
void anim_tick_us(uint32_t elapsed_us);
