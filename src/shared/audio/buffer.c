#include <stdio.h>
#include <stdlib.h>

#include <platform/sync.h>
#include <platform/time.h>

#include "buffer.h"

static inline uint32_t pool_count_load(const audio_buffer_pool_t *pool) {
  return __atomic_load_n(&pool->count, __ATOMIC_ACQUIRE);
}

static inline void pool_count_store(audio_buffer_pool_t *pool, uint32_t value) {
  __atomic_store_n(&pool->count, value, __ATOMIC_RELEASE);
}

void audio_buffer_pool_init(audio_buffer_pool_t *pool, uint8_t size,
                            uint32_t buffer_size) {
  pool->buffers = malloc(size * buffer_size * sizeof(uint32_t));
  pool->size = size;
  pool->buffer_size = buffer_size;
  pool->count = 0;
  pool->write_head = 0;
  pool->read_head = 0;
}

void audio_buffer_pool_free(audio_buffer_pool_t *pool) {
  free(pool->buffers);

  pool->buffers = NULL;

  pool->size = 0;
  pool->buffer_size = 0;

  pool->count = 0;
  pool->write_head = 0;
  pool->read_head = 0;
}

uint32_t *audio_buffer_pool_acquire_write(audio_buffer_pool_t *pool,
                                          bool blocking) {
  while (true) {
    // did we flow into unread buffers?
    if (pool_count_load(pool) >= pool->size) {
      if (!blocking)
        return NULL;
      // wait for read head to catch up
      platform_sleep_us(100);
      continue;
    }

    uint32_t *buf = pool->buffers + (pool->write_head * pool->buffer_size);
    return buf;
  }
}

void audio_buffer_pool_commit_write(audio_buffer_pool_t *pool) {
  pool->write_head = (pool->write_head + 1) % pool->size;
  uint32_t prev = __atomic_fetch_add(&pool->count, 1u, __ATOMIC_RELEASE);
  if (prev >= pool->size) {
    pool_count_store(pool, pool->size);
  }
}

static const double PI = 3.14159265358979323846264338328;
static double seconds_offset = 0.0;
const double pitch = 440.0;
const double radians_per_second = pitch * 2.0 * PI;
const double seconds_per_frame = 1.0 / 24000.0;

uint32_t *audio_buffer_pool_acquire_read(audio_buffer_pool_t *pool,
                                         bool blocking) {
  while (true) {
    // did we flow into written buffers?
    if (pool_count_load(pool) == 0) {
      if (!blocking)
        return NULL;
      // wait for write head to catch up
      platform_sleep_us(100);
      continue;
    }

    uint32_t *buf = pool->buffers + (pool->read_head * pool->buffer_size);

    return buf;
  }
}

void audio_buffer_pool_commit_read(audio_buffer_pool_t *pool) {
  pool->read_head = (pool->read_head + 1) % pool->size;
  uint32_t prev = __atomic_fetch_sub(&pool->count, 1u, __ATOMIC_RELEASE);
  if (prev == 0) {
    pool_count_store(pool, 0);
  }
}
