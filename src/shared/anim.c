#include "anim.h"
#include <stdio.h>

anim_sys_t g_anim;

inline uint32_t anim_ease_linear(uint32_t p_q16) { return p_q16; }

// InOut quad: 2p^2 (p<0.5) else 1 - 2(1-p)^2
inline uint32_t anim_ease_inout_quad(uint32_t p_q16) {
  // p in [0, 65536]
  if (p_q16 <= 32768u) {
    // 2 * p^2
    // (p*p)>>16 fits in 32 bits if we cast to 64 during mul
    uint64_t pp = (uint64_t)p_q16 * (uint64_t)p_q16; // Q32.32
    uint32_t p2 = (uint32_t)(pp >> 16);              // Q16.16
    uint32_t r = p2 << 1;                            // *2
    return r > 65536u ? 65536u : r;
  } else {
    // 1 - 2*(1-p)^2
    uint32_t one = 65536u;
    uint32_t pm = one - p_q16;
    uint64_t pp = (uint64_t)pm * (uint64_t)pm; // Q32.32
    uint32_t p2 = (uint32_t)(pp >> 16);        // Q16.16
    uint32_t two_p2 = p2 << 1;
    return (two_p2 >= one) ? 0u : (one - two_p2);
  }
}

static inline uint32_t anim_apply_ease(anim_ease_t e, uint32_t p_q16) {
  switch (e) {
  case ANIM_EASE_INOUT_QUAD:
    return anim_ease_inout_quad(p_q16);
  case ANIM_EASE_LINEAR:
  default:
    return anim_ease_linear(p_q16);
  }
}

static inline int find_slot_by_ptr(volatile int32_t *ptr) {
  for (int i = 0; i < ANIM_MAX; ++i)
    if (g_anim.slots[i].active && g_anim.slots[i].out == ptr)
      return i;
  return -1;
}

static inline int find_free_slot(void) {
  for (int i = 0; i < ANIM_MAX; ++i)
    if (!g_anim.slots[i].active)
      return i;
  return -1;
}

static inline uint32_t q16_from_ratio(uint32_t num, uint32_t den) {
  // returns round((num << 16) / den), guarding den==0 at callsites.
  uint64_t t = ((uint64_t)num << 16) + (den >> 1);
  return (uint32_t)(t / den);
}

void anim_init(void) {
  for (int i = 0; i < ANIM_MAX; ++i)
    g_anim.slots[i].active = 0;
}

// Start/overwrite: animate *out from its current value to 'to' over
// 'duration_ticks'
int anim_to(volatile int32_t *out, int32_t to, uint32_t duration_ticks,
            anim_ease_t ease, anim_done_fn on_done, void *ctx) {
  int idx = find_slot_by_ptr(out);
  if (idx < 0)
    idx = find_free_slot();
  if (idx < 0) {
    *out = to; // jump to end
    return -1; // no capacity
  }

  anim_slot_t *s = &g_anim.slots[idx];
  s->out = out;
  s->start = (int32_t)(*out);
  s->end = to;
  s->delta = s->end - s->start;
  s->ease = ease;
  s->on_done = on_done;
  s->ctx = ctx;

  if (duration_ticks == 0u) {
    *s->out = s->end;
    s->active = 0;
    if (s->on_done)
      s->on_done(s->ctx);
    return idx;
  }

  s->p_q16 = 0u;
  s->dp_q16 = q16_from_ratio(1u, duration_ticks); // ≈ (1<<16)/duration
  s->active = 1;
  return idx;
}

void anim_cancel(volatile int32_t *out, int snap_to_end) {
  int idx = find_slot_by_ptr(out);
  if (idx < 0)
    return;
  anim_slot_t *s = &g_anim.slots[idx];
  if (snap_to_end)
    *s->out = s->end;
  s->active = 0;
}

void anim_tick(void) {
  // printf("hi.");
  for (int i = 0; i < ANIM_MAX; ++i) {
    anim_slot_t *s = &g_anim.slots[i];
    if (!s->active)
      continue;

    uint32_t p = s->p_q16;
    // advance progress
    uint32_t p_next = p + s->dp_q16;
    if (p_next >= 65536u)
      p_next = 65536u;
    s->p_q16 = p_next;

    // eased progress
    uint32_t e = anim_apply_ease(s->ease, p_next);

    // value = start + (delta * e)>>16  (Q16.16 scale)
    int64_t prod = (int64_t)s->delta * (int64_t)e; // up to ~48 bits
    int32_t inc = (int32_t)(prod >> 16);
    int32_t val = s->start + inc;

    *(s->out) = val;

    if (p_next >= 65536u) {
      *(s->out) = s->end; // ensure exact final value
      s->active = 0;
      if (s->on_done)
        s->on_done(s->ctx);
    }
  }
}
