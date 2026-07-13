#pragma once

#include <stdint.h>

#define BEATLINE_FILE_MAGIC 0x4e4c5442u /* BTLN */
#define BEATLINE_FILE_FORMAT_VERSION 1u
#define BEATLINE_FILE_HEADER_BYTES 256u
#define BEATLINE_SCORING_RULESET 1u
#define BEATLINE_FILE_FLAG_RANKED (1u << 0)
#define BEATLINE_FILE_MAX_NOTES 1024u
#define BEATLINE_FILE_MAX_EVENTS 65535u
#define BEATLINE_LEADERBOARD_PAYLOAD_BYTES 22u

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t format_version;
    uint16_t header_size;
    uint32_t file_size;
    uint32_t scoring_ruleset;
    uint32_t flags;
    uint32_t reserved0;
    uint64_t track_id;
    uint64_t normal_chart_id;
    uint64_t hard_chart_id;
    uint32_t bpm_q8;
    uint32_t duration_ms;
    uint32_t duration_ticks;
    uint8_t numerator;
    uint8_t denominator;
    uint16_t reserved1;
    uint32_t title_offset;
    uint16_t title_length;
    uint16_t artist_length;
    uint32_t artist_offset;
    uint32_t patches_offset;
    uint32_t patch_count;
    uint32_t events_offset;
    uint32_t event_count;
    uint32_t normal_notes_offset;
    uint32_t normal_note_count;
    uint32_t hard_notes_offset;
    uint32_t hard_note_count;
    uint8_t reserved[148];
} beatline_file_header_t;

typedef struct __attribute__((packed))
{
    int32_t freq_mult;
    int16_t level;
    uint8_t mode;
    uint8_t reserved;
    uint32_t attack;
    uint32_t decay;
    int32_t sustain;
    uint32_t release;
} beatline_file_operator_t;

typedef struct __attribute__((packed))
{
    uint8_t patch_idx;
    uint8_t reserved[3];
    beatline_file_operator_t ops[4];
} beatline_file_patch_t;

typedef enum
{
    BEATLINE_FILE_EVENT_NOTE_ON = 0,
    BEATLINE_FILE_EVENT_NOTE_OFF = 1,
} beatline_file_event_type_t;

typedef struct __attribute__((packed))
{
    uint32_t time_ms;
    uint8_t type;
    uint8_t patch_idx;
    uint8_t note;
    uint8_t velocity;
} beatline_file_event_t;

_Static_assert(sizeof(beatline_file_header_t) == BEATLINE_FILE_HEADER_BYTES,
               ".beatline header is part of the file format");
_Static_assert(sizeof(beatline_file_operator_t) == 24,
               ".beatline operator is part of the file format");
_Static_assert(sizeof(beatline_file_patch_t) == 100,
               ".beatline patch is part of the file format");
_Static_assert(sizeof(beatline_file_event_t) == 8,
               ".beatline event is part of the file format");

static inline void beatline_leaderboard_payload(
    uint8_t output[BEATLINE_LEADERBOARD_PAYLOAD_BYTES], uint64_t chart_id,
    uint32_t score, uint16_t max_combo, uint16_t perfect, uint16_t good,
    uint16_t bad, uint16_t miss)
{
    for (uint8_t i = 0; i < 8; ++i)
        output[i] = (uint8_t)(chart_id >> (8u * i));
    output[8] = (uint8_t)score;
    output[9] = (uint8_t)(score >> 8);
    output[10] = (uint8_t)(score >> 16);
    output[11] = (uint8_t)(score >> 24);
    const uint16_t values[5] = {max_combo, perfect, good, bad, miss};
    for (uint8_t i = 0; i < 5; ++i)
    {
        output[12 + i * 2] = (uint8_t)values[i];
        output[13 + i * 2] = (uint8_t)(values[i] >> 8);
    }
}
