#include "audio.h"

#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>

#include <shared/audio/synth_internal.h>
#include <shared/config.h>

#include "config.h"

static SDL_AudioDeviceID audio_device;
static bool audio_initialized;

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
  audio_synth_t *synth = userdata;
  uint32_t frames = (uint32_t)len / sizeof(uint32_t);
  audio_synth_fill_buffer(synth, (audio_buffer_t)stream, frames);
}

void audio_playback_init(void)
{
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
  {
    fprintf(stderr, "SDL audio initialization failed: %s\n", SDL_GetError());
    return;
  }
  audio_initialized = true;
}

void audio_playback_run_forever(audio_synth_t *synth)
{
  if (!audio_initialized)
  {
    while (true)
      SDL_Delay(1000);
  }

  SDL_AudioSpec desired = {
      .freq = AUDIO_SAMPLE_RATE,
      .format = AUDIO_S16SYS,
      .channels = 2,
      .samples = AUDIO_BUFFER_SIZE,
      .callback = audio_callback,
      .userdata = synth,
  };

  audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, NULL, 0);
  if (audio_device != 0)
  {
    printf("SDL audio: %s at %d Hz\n", SDL_GetCurrentAudioDriver(),
           desired.freq);
    fflush(stdout);
    SDL_PauseAudioDevice(audio_device, 0);
  }
  else
    fprintf(stderr, "SDL audio device open failed: %s\n", SDL_GetError());

  while (true)
    SDL_Delay(1000);
}

void audio_playback_suspend(void)
{
  if (audio_device != 0)
    SDL_PauseAudioDevice(audio_device, 1);
}

void audio_playback_set_enabled(bool enabled)
{
  if (audio_device != 0)
    SDL_PauseAudioDevice(audio_device, enabled ? 0 : 1);
}

void audio_playback_resume(void)
{
  if (audio_device != 0)
    SDL_PauseAudioDevice(audio_device, 0);
}
