# Beatline tracks

Beatline tracks are pointer-free `.beatline` files stored inside ordinary
Prism asset packs. Each file contains its title and artist, playback patches
and events, and fully resolved normal and hard note arrays. Beatline scans
compatible pack directories when needed and keeps the selected file in flash;
the number or total size of installed songs therefore does not create a
matching RAM allocation.

The bundled Golden and Never Gonna tracks are separate asset packs. They use
the same discovery and playback path as user-installed tracks and are not
compiled into `beatline.prism`.

## Registration

A `.beatline` file is complete and playable without registration. An
unregistered result screen shows the Beatline cartridge icon instead of a
leaderboard QR.

Leaderboard publication can add this optional binding without changing chart
or audio contents:

```c
uint64_t track_id;
uint64_t normal_chart_id;
uint64_t hard_chart_id;
```

The ranked flag and all three nonzero IDs must appear together. Track identity
belongs to the song, while each difficulty has an independent chart identity.
A publishing service can retain a chart ID when its canonical digest is
unchanged and assign a new ID only to a changed difficulty. The digest input is
the difficulty, scoring ruleset, and each resolved note's hit tick, lane, type,
and hold duration. Audio, title/artist metadata, pack identity, and asset path
are excluded.

Ranked results use the existing Beatline leaderboard app ID and an exact
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

Difficulty, track identity, and rank are resolved or derived server-side.

## Authoring

`scripts/rea_midi_export.py --beatline-out` resolves the normal and hard rhythm
channels at the 960-Hz engine timeline, removes those authoring channels from
audio playback, and writes the compact file. Omitting the three ID options
creates an unregistered track; passing `--track-id`, `--normal-chart-id`, and
`--hard-chart-id` together creates a registered one. Partial bindings are
rejected.
