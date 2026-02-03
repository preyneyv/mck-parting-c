#include <stdbool.h>
#include <stdio.h>

#include <SDL.h>

#include <shared/audio/buffer.h>
#include <shared/audio/playback.h>
#include <shared/audio/synth.h>
#include <shared/platform/sleep.h>

#include "audio.h"
#include "config.h"

static audio_buffer_pool_t pool;
static SDL_AudioDeviceID audio_device = 0;
static bool playback_enabled = true;

static void audio_playback_write_callback(void *userdata, uint8_t *stream,
                                          int len) {
  (void)userdata;
  int frames_to_write = len / (int)sizeof(uint32_t);
  uint32_t *out = (uint32_t *)stream;
  static uint32_t *buffer = NULL;
  static uint32_t buffer_offset = 0;
  int frames_written = 0;

  while (frames_written < frames_to_write) {
    if (!buffer) {
      buffer = audio_buffer_pool_acquire_read(&pool, false);
      buffer_offset = 0;
    }
    if (!buffer) {
      memset(out + frames_written, 0,
             (frames_to_write - frames_written) * sizeof(uint32_t));
      return;
    }
    int remaining = frames_to_write - frames_written;
    int available = (int)pool.buffer_size - (int)buffer_offset;
    int frames = remaining < available ? remaining : available;
    memcpy(out + frames_written, buffer + buffer_offset,
           frames * sizeof(uint32_t));
    frames_written += frames;
    buffer_offset += (uint32_t)frames;
    if (buffer_offset >= pool.buffer_size) {
      audio_buffer_pool_commit_read(&pool);
      buffer = NULL;
      buffer_offset = 0;
    }
  }
}

// this will be called on core1 on device.
void audio_playback_set_enabled(bool enabled) {
  playback_enabled = enabled;
  if (audio_device != 0) {
    SDL_PauseAudioDevice(audio_device, enabled ? 0 : 1);
  }
}

void audio_init(audio_synth_t *synth) {
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    fprintf(stderr, "SDL audio init failed: %s\n", SDL_GetError());
    return;
  }

  audio_buffer_pool_init(&pool, AUDIO_BUFFER_POOL_SIZE, AUDIO_BUFFER_SIZE);

  SDL_AudioSpec desired = {0};
  SDL_AudioSpec obtained = {0};
  desired.freq = AUDIO_SAMPLE_RATE;
  desired.format = AUDIO_S16SYS;
  desired.channels = 2;
  desired.samples = AUDIO_BUFFER_SIZE;
  desired.callback = audio_playback_write_callback;
  audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
  if (audio_device == 0) {
    fprintf(stderr, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
    return;
  }
  SDL_PauseAudioDevice(audio_device, 0);

  while (true) {
    if (!playback_enabled) {
      platform_sleep_ms(10);
      continue;
    }
    audio_buffer_t buffer = audio_buffer_pool_acquire_write(&pool, true);
    audio_synth_fill_buffer(synth, buffer, pool.buffer_size);
    audio_buffer_pool_commit_write(&pool);
  }

  if (audio_device != 0) {
    SDL_CloseAudioDevice(audio_device);
    audio_device = 0;
  }
}
