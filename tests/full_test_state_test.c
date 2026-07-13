#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <prism/sdk.h>

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

extern const prism_cartridge_t cartridge_full_test;
void prism_full_test_set_allocation_failure(bool fail);
bool prism_full_test_has_allocation(void);

bool audio_synth_enqueue(audio_synth_t *synth,
                         const audio_synth_message_t *message)
{
  (void)synth;
  (void)message;
  return true;
}

void audio_synth_patch_config_set(audio_synth_t *synth, uint8_t patch_idx,
                                  audio_synth_patch_config_t config)
{
  (void)synth;
  (void)patch_idx;
  (void)config;
}

static uint32_t ticks(void) { return 0; }
static prism_time_t now_us(void) { return 0; }
static audio_synth_t *synth(void) { return (audio_synth_t *)(uintptr_t)1; }

int main(void)
{
  const prism_api_v1_t api = {
      .ticks = ticks,
      .now_us = now_us,
      .synth = synth,
  };
  prism_t prism = {
      .api = &api,
      .cartridge = &cartridge_full_test,
  };

  prism_full_test_set_allocation_failure(false);
  cartridge_full_test.enter(&prism);
  CHECK(prism_full_test_has_allocation());
  cartridge_full_test.leave(&prism);
  CHECK(!prism_full_test_has_allocation());
  cartridge_full_test.leave(&prism);

  prism_full_test_set_allocation_failure(true);
  cartridge_full_test.enter(&prism);
  CHECK(!prism_full_test_has_allocation());
  cartridge_full_test.tick(&prism);
  cartridge_full_test.leave(&prism);

  puts("full-test state tests passed");
  return 0;
}
