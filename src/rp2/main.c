#include <stdio.h>

#include <pico/multicore.h>

#include <shared/engine.h>
#include <platform/platform.h>

#include "audio.h"

void midi_task(void);

static void core1_main(void)
{
  audio_playback_init();
  audio_playback_run_forever(&g_engine.synth);
}

static void on_frame_cb(void)
{
  midi_task();
}

int main(void)
{
  platform_init();
  printf("prism v1\n");

  engine_init();
  g_engine.on_frame_cb = on_frame_cb;

  multicore_reset_core1();
  multicore_launch_core1(core1_main);

  engine_run_forever();
  return 0;
}
