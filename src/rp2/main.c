#include <stdio.h>

#include <pico/multicore.h>
#include <pico/flash.h>
#include <pico/sync.h>

#include <shared/engine.h>
#include <platform/platform.h>

#include "audio.h"
#include "management.h"

void midi_task(void);
static semaphore_t core1_flash_ready;

static void core1_main(void)
{
  /* Every flash mutation is initiated by core 0. Register the audio core as
   * the multicore lockout victim before it starts executing from XIP, or
   * flash_safe_execute() will assert (and release builds could erase while
   * core 1 is still fetching instructions). */
  hard_assert(flash_safe_execute_core_init());
  sem_release(&core1_flash_ready);
  audio_playback_init();
  audio_playback_run_forever(engine_synth());
}

static void on_frame_cb(void)
{
  midi_task();
  management_task();
}

int main(void)
{
  platform_init();
  printf("prism v1\n");

  engine_init();
  sem_init(&core1_flash_ready, 0, 1);
  multicore_reset_core1();
  multicore_launch_core1(core1_main);
  /* cartridge_storage_init() may need to finish a journalled compaction at
   * boot. Do not let it mutate flash until core 1 can honor lockout. */
  sem_acquire_blocking(&core1_flash_ready);

  management_init();
  /* Cartridge storage must be loaded before onboarding decides whether the
   * bundled guide exists. Otherwise a reboot during the guide can mistake the
   * not-yet-populated registry for a missing package and dismiss it. */
  engine_set_app(NULL);
  engine_set_frame_callback(on_frame_cb);

  engine_run_forever();
  return 0;
}
