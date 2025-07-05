#include "audio.h"

static audio_buffer_pool_t pool;

void audio_init() {
  // create audio buffer
  audio_buffer_pool_init(&pool, 5, 256);
}
