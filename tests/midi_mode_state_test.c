#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <platform/display.h>
#include <shared/audio/synth_internal.h>
#include <shared/engine.h>
#include <shared/os/midi_mode.h>

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

static audio_synth_analysis_buffers_t *published;
static uint32_t enable_count;
static uint32_t disable_count;

audio_synth_t *engine_synth(void)
{
  return (audio_synth_t *)(uintptr_t)1;
}
void engine_mark_input(void) {}
bool engine_is_app(const app_t *app)
{
  (void)app;
  return false;
}
void engine_set_app(app_t *app) { (void)app; }
u8g2_t *platform_display_get_u8g2(void) { return NULL; }

bool audio_synth_analysis_enable(audio_synth_t *synth,
                                 audio_synth_analysis_buffers_t *buffers)
{
  (void)synth;
  CHECK(published == NULL);
  published = buffers;
  enable_count++;
  return true;
}

void audio_synth_analysis_disable_sync(audio_synth_t *synth)
{
  (void)synth;
  CHECK(published != NULL);
  published = NULL;
  disable_count++;
}

uint16_t audio_synth_active_voice_mask(const audio_synth_t *synth)
{
  (void)synth;
  return 0;
}
uint16_t audio_synth_analysis_take_peak(audio_synth_t *synth)
{
  (void)synth;
  return 0;
}
bool audio_synth_analysis_snapshot(const audio_synth_t *synth,
                                   int16_t samples[AUDIO_SYNTH_ANALYSIS_SAMPLE_COUNT],
                                   uint32_t *sequence)
{
  (void)synth;
  (void)samples;
  (void)sequence;
  return false;
}
uint16_t audio_synth_message_queue_take_peak(audio_synth_t *synth)
{
  (void)synth;
  return 0;
}
uint32_t audio_synth_message_queue_full_count(const audio_synth_t *synth)
{
  (void)synth;
  return 0;
}
uint32_t audio_synth_analysis_late_count(const audio_synth_t *synth)
{
  (void)synth;
  return 0;
}
uint32_t audio_synth_analysis_underrun_count(const audio_synth_t *synth)
{
  (void)synth;
  return 0;
}

int main(void)
{
  for (uint8_t i = 0; i < 4; ++i)
  {
    prism_midi_mode_test_enter();
    CHECK(prism_midi_mode_test_allocation_bytes() >= 12 * 1024);
    CHECK(published != NULL);
    prism_midi_mode_test_leave();
    CHECK(prism_midi_mode_test_allocation_bytes() == 0);
    CHECK(published == NULL);
  }
  CHECK(enable_count == 4);
  CHECK(disable_count == 4);

  prism_midi_mode_test_set_allocation_failure(true);
  prism_midi_mode_test_enter();
  CHECK(prism_midi_mode_test_allocation_bytes() == 0);
  CHECK(published == NULL);
  prism_midi_mode_test_leave();
  CHECK(disable_count == 4);

  puts("MIDI mode state tests passed");
  return 0;
}
