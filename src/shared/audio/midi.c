#include "midi.h"

#include <limits.h>

static bool read_7bit_uint(const uint8_t *data, size_t length, size_t *cursor,
                           uint8_t width, uint32_t *value)
{
  uint32_t result = 0;
  for (uint8_t i = 0; i < width; ++i)
  {
    if (*cursor >= length || data[*cursor] >= 0x80)
      return false;
    uint8_t byte = data[(*cursor)++];
    if (i == 4 && byte > 0x0f)
      return false;
    result |= (uint32_t)byte << (i * 7);
  }
  *value = result;
  return true;
}

static bool parse_set_patch(const uint8_t *data, size_t length,
                            uint8_t version,
                            audio_synth_sysex_cmd_set_patch_t *command)
{
  if (version != AUDIO_SYNTH_SYSEX_CMD_SET_PATCH_VERSION || length == 0)
    return false;

  size_t cursor = 0;
  uint32_t patch_index;
  if (!read_7bit_uint(data, length, &cursor, 1, &patch_index) ||
      patch_index >= AUDIO_SYNTH_PATCH_COUNT)
    return false;
  command->patch_idx = (uint8_t)patch_index;

  for (uint8_t i = 0; i < AUDIO_SYNTH_OPERATOR_COUNT; ++i)
  {
    uint32_t frequency_multiplier;
    uint32_t level;
    uint32_t mode;
    uint32_t attack;
    uint32_t decay;
    uint32_t sustain;
    uint32_t release;
    if (!read_7bit_uint(data, length, &cursor, 1, &frequency_multiplier) ||
        !read_7bit_uint(data, length, &cursor, 3, &level) ||
        !read_7bit_uint(data, length, &cursor, 1, &mode) ||
        !read_7bit_uint(data, length, &cursor, 5, &attack) ||
        !read_7bit_uint(data, length, &cursor, 5, &decay) ||
        !read_7bit_uint(data, length, &cursor, 5, &sustain) ||
        !read_7bit_uint(data, length, &cursor, 5, &release) ||
        mode > AUDIO_SYNTH_OP_MODE_FREQ_MOD)
      return false;

    audio_synth_operator_config_t *operator = &command->patch.ops[i];
    operator->freq_mult = (int)frequency_multiplier;
    operator->level = q1x15_clamp_s32((int32_t)level);
    operator->mode = (audio_synth_operator_mode_t)mode;
    operator->env.a = attack;
    operator->env.d = decay;
    operator->env.s = sustain > INT32_MAX ? Q1X31_ONE : (q1x31)sustain;
    operator->env.r = release;
  }

  return cursor == length;
}

bool audio_synth_sysex_cmd_parse(const uint8_t *message, size_t length,
                                 audio_synth_sysex_cmd_t *command)
{
  if (message == NULL || command == NULL || length < 6 ||
      message[0] != 0xf0 || message[length - 1] != 0xf7 ||
      message[1] != AUDIO_SYNTH_SYSEX_MFG_ID)
    return false;

  command->cmd = (audio_synth_sysex_cmd_type_t)message[2];
  command->version = message[3];
  const uint8_t *payload = message + 4;
  size_t payload_length = length - 5;

  switch (command->cmd)
  {
  case AUDIO_SYNTH_SYSEX_CMD_SET_PATCH:
    return parse_set_patch(payload, payload_length, command->version,
                           &command->data.set_patch);
  default:
    return false;
  }
}
