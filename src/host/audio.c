#include "audio.h"

#include <SDL.h>
#include <stdbool.h>

#include <shared/audio/synth_internal.h>
#include <shared/config.h>

#include "config.h"

static SDL_AudioDeviceID audio_device;

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
  audio_synth_t *synth = userdata;
  uint32_t frames = (uint32_t)len / sizeof(uint32_t);
  audio_synth_fill_buffer(synth, (audio_buffer_t)stream, frames);
}

void audio_playback_init(void)
{
  SDL_InitSubSystem(SDL_INIT_AUDIO);
}

void audio_playback_run_forever(audio_synth_t *synth)
{
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
    SDL_PauseAudioDevice(audio_device, 0);

  while (true)
    SDL_Delay(1000);
}

void audio_playback_suspend(void)
{
  if (audio_device != 0)
    SDL_PauseAudioDevice(audio_device, 1);
}

void audio_playback_set_enabled(bool enabled) { (void)enabled; }

void audio_playback_resume(void)
{
  if (audio_device != 0)
    SDL_PauseAudioDevice(audio_device, 0);
}
