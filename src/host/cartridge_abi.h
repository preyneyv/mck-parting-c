#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <shared/audio/synth.h>

enum
{
  HOST_GUEST_SYNTH_MESSAGE_BYTES = 4,
  HOST_GUEST_SYNTH_OPERATOR_BYTES = 24,
  HOST_GUEST_SYNTH_PATCH_BYTES =
      AUDIO_SYNTH_OPERATOR_COUNT * HOST_GUEST_SYNTH_OPERATOR_BYTES,
};

/* ARM EABI represents these enums with their smallest valid integer type,
 * while desktop ABIs commonly use a 32-bit int. Decode the cartridge layout
 * explicitly instead of memcpying it into native structs. */
bool host_cartridge_decode_synth_message(
    const uint8_t bytes[HOST_GUEST_SYNTH_MESSAGE_BYTES],
    audio_synth_message_t *message);
bool host_cartridge_decode_synth_patch(
    const uint8_t bytes[HOST_GUEST_SYNTH_PATCH_BYTES],
    audio_synth_patch_config_t *patch);

/* ARM newlib exposes a 31-bit RAND_MAX. Keep guest rand() semantics stable
 * even when the host C library (notably MinGW) only returns 15 bits. */
uint32_t host_cartridge_rand31(void);
