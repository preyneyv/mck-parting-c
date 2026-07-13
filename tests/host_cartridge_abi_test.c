#include "host/cartridge_abi.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition)                                                     \
  do                                                                         \
  {                                                                          \
    if (!(condition))                                                        \
    {                                                                        \
      fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__,   \
              #condition);                                                   \
      return 1;                                                              \
    }                                                                        \
  } while (0)

static void write_u16(uint8_t *bytes, uint16_t value)
{
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t *bytes, uint32_t value)
{
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
  bytes[2] = (uint8_t)(value >> 16);
  bytes[3] = (uint8_t)(value >> 24);
}

int main(void)
{
  audio_synth_message_t message;
  const uint8_t note_on[] = {AUDIO_SYNTH_MESSAGE_NOTE_ON, 2, 50, 63};
  CHECK(host_cartridge_decode_synth_message(note_on, &message));
  CHECK(message.type == AUDIO_SYNTH_MESSAGE_NOTE_ON);
  CHECK(message.data.note_on.patch_idx == 2);
  CHECK(message.data.note_on.note_number == 50);
  CHECK(message.data.note_on.velocity == 63);

  const uint8_t note_off[] = {AUDIO_SYNTH_MESSAGE_NOTE_OFF, 3, 0xff, 0};
  CHECK(host_cartridge_decode_synth_message(note_off, &message));
  CHECK(message.data.note_off.patch_idx == 3);
  CHECK(message.data.note_off.note_number == -1);

  const uint8_t stop[] = {AUDIO_SYNTH_MESSAGE_STOP, 4, 72, 0};
  CHECK(host_cartridge_decode_synth_message(stop, &message));
  CHECK(message.data.stop.patch_idx == 4);
  CHECK(message.data.stop.note_number == 72);

  const uint8_t invalid[] = {AUDIO_SYNTH_MESSAGE_PANIC + 1, 0, 0, 0};
  CHECK(!host_cartridge_decode_synth_message(invalid, &message));

  uint8_t encoded[HOST_GUEST_SYNTH_PATCH_BYTES] = {0};
  for (uint8_t i = 0; i < AUDIO_SYNTH_OPERATOR_COUNT; ++i)
  {
    uint8_t *operator = encoded + i * HOST_GUEST_SYNTH_OPERATOR_BYTES;
    write_u32(operator, (uint32_t)(int32_t)(i - 1));
    write_u16(operator + 4, (uint16_t)(int16_t)(1000 + i));
    operator[6] = i & 1u;
    write_u32(operator + 8, 10u + i);
    write_u32(operator + 12, 20u + i);
    write_u32(operator + 16, 0x12340000u + i);
    write_u32(operator + 20, 30u + i);
  }

  audio_synth_patch_config_t patch;
  CHECK(host_cartridge_decode_synth_patch(encoded, &patch));
  for (uint8_t i = 0; i < AUDIO_SYNTH_OPERATOR_COUNT; ++i)
  {
    CHECK(patch.ops[i].freq_mult == (int)i - 1);
    CHECK(patch.ops[i].level == 1000 + i);
    CHECK(patch.ops[i].mode == (audio_synth_operator_mode_t)(i & 1u));
    CHECK(patch.ops[i].env.a == 10u + i);
    CHECK(patch.ops[i].env.d == 20u + i);
    CHECK(patch.ops[i].env.s == (q1x31)(0x12340000u + i));
    CHECK(patch.ops[i].env.r == 30u + i);
  }
  encoded[6] = 2;
  CHECK(!host_cartridge_decode_synth_patch(encoded, &patch));

  srand(1u);
  bool saw_wide_random = false;
  for (uint8_t i = 0; i < 64; ++i)
  {
    uint32_t value = host_cartridge_rand31();
    CHECK(value <= 0x7fffffffu);
    saw_wide_random = saw_wide_random || value > 0xffffu;
  }
  CHECK(saw_wide_random);

  puts("host cartridge ABI tests passed");
  return 0;
}
