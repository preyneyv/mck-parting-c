#include <pthread.h>
#include <stdio.h>

#include <platform/platform.h>
#include <platform/cartridge.h>
#include <shared/engine.h>

#include "audio.h"
#include "cartridge.h"
#include <prism/runtime.h>

static void *audio_thread_main(void *unused)
{
  (void)unused;
  audio_playback_init();
  audio_playback_run_forever(engine_synth());
  return NULL;
}

int main(int argc, char **argv)
{
  if (argc > 2)
  {
    fprintf(stderr, "usage: %s [cartridge.prism]\n", argv[0]);
    return 2;
  }

  if (argc == 2 && !host_cartridge_load(argv[1]))
    return 1;
  platform_init();
  printf("prism host\n");
  engine_init();
  if (argc == 2 &&
      !prism_cartridge_launch(platform_cartridge_installed_get(0)))
  {
    fprintf(stderr, "failed to launch cartridge: %s\n", argv[1]);
    return 1;
  }

  pthread_t audio_thread;
  pthread_create(&audio_thread, NULL, audio_thread_main, NULL);

  engine_run_forever();
  return 0;
}
