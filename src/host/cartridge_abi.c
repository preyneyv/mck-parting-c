#include "cartridge_abi.h"

#include <stdlib.h>
#include <string.h>

static uint16_t read_u16(const uint8_t *bytes)
{
  return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t read_u32(const uint8_t *bytes)
{
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
         ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

bool host_cartridge_decode_synth_message(
    const uint8_t bytes[HOST_GUEST_SYNTH_MESSAGE_BYTES],
    audio_synth_message_t *message)
{
  if (bytes == NULL || message == NULL ||
      bytes[0] > AUDIO_SYNTH_MESSAGE_PANIC)
    return false;

  memset(message, 0, sizeof(*message));
  message->type = (audio_synth_message_type_t)bytes[0];
  switch (message->type)
  {
  case AUDIO_SYNTH_MESSAGE_NOTE_ON:
    message->data.note_on.patch_idx = bytes[1];
    message->data.note_on.note_number = bytes[2];
    message->data.note_on.velocity = bytes[3];
    break;
  case AUDIO_SYNTH_MESSAGE_NOTE_OFF:
    message->data.note_off.patch_idx = bytes[1];
    message->data.note_off.note_number = (int8_t)bytes[2];
    break;
  case AUDIO_SYNTH_MESSAGE_STOP:
    message->data.stop.patch_idx = bytes[1];
    message->data.stop.note_number = (int8_t)bytes[2];
    break;
  case AUDIO_SYNTH_MESSAGE_PANIC: break;
  }
  return true;
}

bool host_cartridge_decode_synth_patch(
    const uint8_t bytes[HOST_GUEST_SYNTH_PATCH_BYTES],
    audio_synth_patch_config_t *patch)
{
  if (bytes == NULL || patch == NULL)
    return false;

  memset(patch, 0, sizeof(*patch));
  for (uint8_t i = 0; i < AUDIO_SYNTH_OPERATOR_COUNT; ++i)
  {
    const uint8_t *source = bytes + i * HOST_GUEST_SYNTH_OPERATOR_BYTES;
    uint8_t mode = source[6];
    if (mode > AUDIO_SYNTH_OP_MODE_FREQ_MOD)
      return false;
    audio_synth_operator_config_t *operator = &patch->ops[i];
    operator->freq_mult = (int32_t)read_u32(source);
    operator->level = (q1x15)(int16_t)read_u16(source + 4);
    operator->mode = (audio_synth_operator_mode_t)mode;
    operator->env.a = read_u32(source + 8);
    operator->env.d = read_u32(source + 12);
    operator->env.s = (q1x31)(int32_t)read_u32(source + 16);
    operator->env.r = read_u32(source + 20);
  }
  return true;
}

uint32_t host_cartridge_rand31(void)
{
#if RAND_MAX >= 0x7fffffff
  return (uint32_t)rand() & 0x7fffffffu;
#else
  uint32_t high = (uint32_t)rand() & 0x7fffu;
  uint32_t middle = (uint32_t)rand() & 0x7fffu;
  uint32_t low = (uint32_t)rand() & 1u;
  return (high << 16) | (middle << 1) | low;
#endif
}
