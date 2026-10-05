# Beatline tracks

Beatline tracks are `.beatline` files stored in ordinary Prism asset packs.
Each file holds the title and artist, synth patches, playback events, and the
fully resolved note lists for the normal and hard charts. The file contains no
pointers. Beatline finds tracks by scanning compatible packs and plays the
selected file straight from flash, so installing more or longer songs does not
use more RAM.

The bundled Golden and Never Gonna tracks are separate asset packs. They are
not compiled into `beatline.prism` and load the same way as any installed
track.

## Leaderboard IDs

A `.beatline` file is playable without leaderboard IDs. Without them, the
results screen shows the Beatline icon instead of a score QR code.

A ranked track sets the ranked flag in its header together with three nonzero
IDs:

```c
uint64_t track_id;
uint64_t normal_chart_id;
uint64_t hard_chart_id;
```

The track ID identifies the song, and each difficulty has its own chart ID.
Adding the IDs does not change the chart or audio data. When a chart is
republished, it can keep its chart ID as long as its canonical digest is
unchanged. The digest covers the difficulty, scoring ruleset, and each
resolved note's hit tick, lane, type, and hold duration; audio, title, artist,
pack identity, and file path are excluded.

The bundled tracks use these IDs:

| Track | Track ID | Normal chart ID | Hard chart ID |
| --- | ---: | ---: | ---: |
| Never Gonna | 1 | 1 | 2 |
| Golden | 2 | 3 | 4 |

Ranked results are submitted under Beatline's leaderboard app ID (5) as a
22-byte little-endian payload:

```c
uint64_t chart_id;
uint32_t score;
uint16_t max_combo;
uint16_t perfect;
uint16_t good;
uint16_t bad;
uint16_t miss;
```

The server derives the track, difficulty, and rank from these fields.

## Authoring

`scripts/rea_midi_export.py --beatline-out` resolves the normal and hard
rhythm channels on the 960 Hz engine timeline, removes those channels from the
audio, and writes the `.beatline` file. It requires `--title` and `--artist`.
Pass `--track-id`, `--normal-chart-id`, and `--hard-chart-id` together to make
a ranked track, or none of them for an unranked one. Passing only some of them
is an error.
