#pragma once

#include <hardware/sync.h>
#include <pico/stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  uint32_t *buffers;

  uint8_t size;
  uint32_t buffer_size;

  uint8_t count;
  uint8_t write_head;
  uint8_t read_head;
} audio_buffer_pool_t;

void audio_buffer_pool_init(audio_buffer_pool_t *pool, uint8_t size,
                            uint32_t buffer_size);
void audio_buffer_pool_free(audio_buffer_pool_t *pool);
uint32_t *audio_buffer_pool_acquire_write(audio_buffer_pool_t *pool,
                                          bool blocking);
void audio_buffer_pool_commit_write(audio_buffer_pool_t *pool);
uint32_t *audio_buffer_pool_acquire_read(audio_buffer_pool_t *pool,
                                         bool blocking);
void audio_buffer_pool_commit_read(audio_buffer_pool_t *pool);
