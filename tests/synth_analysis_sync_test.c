#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <shared/audio/synth_internal.h>

#define CHECK(condition)                                                     \
  do                                                                         \
  {                                                                          \
    if (!(condition))                                                        \
    {                                                                        \
      fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__,    \
              #condition);                                                   \
      abort();                                                               \
    }                                                                        \
  } while (0)

platform_time_t platform_now_us(void)
{
  static platform_time_t now;
  return ++now;
}

static void *disable_analysis(void *raw_synth)
{
  audio_synth_analysis_disable_sync(raw_synth);
  return NULL;
}

int main(void)
{
  audio_synth_t synth = {0};
  audio_synth_init(&synth, 48000.0f, 1000);
  uint32_t buffer[256];
  audio_synth_analysis_buffers_t analysis;

  for (uint8_t cycle = 0; cycle < 8; ++cycle)
  {
    CHECK(audio_synth_analysis_enable(&synth, &analysis));
    audio_synth_fill_buffer(&synth, buffer, 256);
    CHECK(__atomic_load_n(&synth.analysis_active, __ATOMIC_ACQUIRE));

    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, disable_analysis, &synth) == 0);
    while (__atomic_load_n(&synth.analysis_requested, __ATOMIC_ACQUIRE))
      platform_yield();
    audio_synth_fill_buffer(&synth, buffer, 256);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(!__atomic_load_n(&synth.analysis_active, __ATOMIC_ACQUIRE));
    CHECK(__atomic_load_n(&synth.analysis_buffers, __ATOMIC_ACQUIRE) == NULL);
  }

  audio_synth_patch_config_t patch = audio_synth_patch_config_default;
  patch.ops[0].level = Q1X15_ONE;
  audio_synth_patch_config_set(&synth, 0, patch);
  audio_synth_set_master_level(&synth, Q1X15_ONE);
  audio_synth_message_t note_on = {
      .type = AUDIO_SYNTH_MESSAGE_NOTE_ON,
      .data.note_on = {
          .patch_idx = 0,
          .note_number = 60,
          .velocity = 127,
      },
  };
  CHECK(audio_synth_enqueue(&synth, &note_on));
  audio_synth_fill_buffer(&synth, buffer, 256);
  bool signal = false;
  for (uint16_t i = 0; i < 256; ++i)
    signal = signal || buffer[i] != 0;
  CHECK(signal);

  audio_synth_set_master_level(&synth, Q1X15_ZERO);
  audio_synth_fill_buffer(&synth, buffer, 256);
  for (uint16_t i = 0; i < 256; ++i)
    CHECK(buffer[i] == 0);

  puts("synth analysis synchronization tests passed");
  return 0;
}
