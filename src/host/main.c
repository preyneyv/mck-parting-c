#include <pthread.h>
#include <stdio.h>

#include <platform/platform.h>
#include <shared/engine.h>

#include "audio.h"

static void *audio_thread_main(void *unused)
{
  (void)unused;
  audio_playback_init();
  audio_playback_run_forever(&g_engine.synth);
  return NULL;
}

int main(void)
{
  platform_init();
  printf("prism host\n");
  engine_init();

  pthread_t audio_thread;
  pthread_create(&audio_thread, NULL, audio_thread_main, NULL);

  engine_run_forever();
  return 0;
}
