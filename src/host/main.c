#include <pthread.h>
#include <stdio.h>
#include <string.h>

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
  if (argc == 3 && strcmp(argv[1], "--test-cartridge-lifecycle") == 0)
    return host_cartridge_test_lifecycle(argv[2]) ? 0 : 1;
  if (argc == 3 && strcmp(argv[1], "--test-cartridge-audio") == 0)
    return host_cartridge_test_audio(argv[2]) ? 0 : 1;
  if (argc > 2)
  {
    fprintf(stderr, "usage: %s [cartridge.prism]\n", argv[0]);
    return 2;
  }

  if (!host_cartridge_init_bundled())
  {
    fprintf(stderr, "failed to load bundled cartridge packages\n");
    return 1;
  }
  if (argc == 2 && !host_cartridge_load(argv[1]))
    return 1;
  platform_init();
  printf("prism host\n");
  engine_init();
  if (argc == 2 &&
      !prism_cartridge_launch(platform_cartridge_installed_get(
          platform_cartridge_installed_count() - 1u)))
  {
    fprintf(stderr, "failed to launch cartridge: %s\n", argv[1]);
    return 1;
  }

  pthread_t audio_thread;
  pthread_create(&audio_thread, NULL, audio_thread_main, NULL);

  engine_run_forever();
  return 0;
}
