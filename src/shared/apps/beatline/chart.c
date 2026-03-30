#include "chart.h"
#include "sounds/never_gonna.h"
#include "sounds/golden.h"

// Compile-time constant versions for static initializers
#define Q1X15_C(f) ((q1x15)((int)((f) * 32767.0)))
#define Q1X31_C(f) ((q1x31)((long long)((f) * 2147483647.0)))

const beatline_chart_t beatline_tracks[] = {
    {
        .title = "Never Gonna",
        .display_info = "Rick Astley",
        .song = &sound_never_gonna_song,
    },
    {
        .title = "Golden",
        .display_info = "HUNTR/X",
        .song = &sound_golden_song,
    }};

const uint8_t BEATLINE_TRACK_COUNT =
    sizeof(beatline_tracks) / sizeof(beatline_tracks[0]);
