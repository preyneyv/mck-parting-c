// midi decoding utilities

// ---- SysEx protocol ---
// [F0 7D cmd version data... F7]
//      ^  ^    ^--- payload version (per command)
//      ^   ^------- command id
//      ^----------- manufacturer id (0x7D = non-commercial)

// Commands:
// 0x01: set patch data (v1)
//   payload: [patch_idx op0 .. op3]

#pragma once

#include "synth.h"

#include <shared/utils/q1x15.h>
#include <shared/utils/q1x31.h>

#define AUDIO_SYNTH_SYSEX_MFG_ID 0x7D
#define AUDIO_SYNTH_SYSEX_CMD_SET_PATCH_VERSION 0x01

typedef enum
{
    AUDIO_SYNTH_SYSEX_CMD_SET_PATCH = 0x01,
} audio_synth_sysex_cmd_type_t;

typedef struct
{
    uint8_t patch_idx; // 0..15
    audio_synth_patch_config_t patch;
} audio_synth_sysex_cmd_set_patch_t;

typedef struct audio_synth_sysex_cmd_t
{
    audio_synth_sysex_cmd_type_t cmd;
    uint8_t version;

    union
    {
        audio_synth_sysex_cmd_set_patch_t set_patch;
    } data;
} audio_synth_sysex_cmd_t;

bool audio_synth_sysex_cmd_parse(const uint8_t *msg,
                                 size_t msg_len,
                                 audio_synth_sysex_cmd_t *out_cmd);
