# Prism

Prism is a two-button RP2040 handheld with a 128x64 monochrome display, two
RGB LEDs, an FM synthesizer, and installable cartridges. The same OS and
cartridge sources also run in an SDL desktop simulator.

## Repository layout

- `src/prism`: public cartridge SDK, package format, and management protocol
- `src/shared`: engine, OS services, system UI, animation, and audio
- `src/platform`: hardware contracts implemented by each target
- `src/rp2`: RP2040 drivers, USB management, flash storage, and persistence
- `src/host`: SDL simulator
- `src/cartridges`: bundled cartridges
- `examples/hello-cartridge`: standalone cartridge project to copy from
- `web`: Next.js site with the WebUSB device manager and score leaderboards
- `cmake`, `scripts`: packaging, asset, and build tooling
- `tests`: host unit tests
- `lib`: vendored u8g2 and qrcodegen

## Architecture

Core 0 runs the engine, input, cartridge lifecycle, display, LEDs, USB,
storage, and system UI. The engine clock and input sampler run at 960 Hz.
Cartridges divide that clock to pick their simulation rate, and the display
renders at up to 120 frames per second.

Core 1 only generates audio. Core 0 sends validated synth commands through a
bounded queue, and DMA feeds 48 kHz stereo frames to an I2S PIO state machine.

Cartridges see only the API in `src/prism`. The build rejects cartridge
sources that include engine or platform headers.

## Firmware

The build needs the Raspberry Pi Pico SDK and ARM toolchain. The Pico VS Code
extension installs the expected versions. The default VS Code build task
regenerates assets, compiles, reboots the device into BOOTSEL, and loads the
firmware. Because it regenerates assets, it needs Aseprite and REAPER; without
them, use the commands below.

From a terminal:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This produces the bundled cartridges as `.prism` packages, the bundled asset
packs as `.prismpack` archives, and two UF2 images:

- `prism.uf2` holds firmware only and leaves installed cartridges, saves, and
  settings alone.
- `prism_factory.uf2` also resets storage and installs the bundled packages.
  They behave like any other installed package afterwards and can be updated
  or removed.

See [Packages](docs/cartridges.md#packages) for exactly what the factory image
rewrites.

## Simulator

```sh
cmake -S . -B build_host -G Ninja \
  -DPICO_PLATFORM=host -DCMAKE_BUILD_TYPE=Release
cmake --build build_host
```

The simulator needs SDL2 with the `SDL2::SDL2-static` CMake target. On Windows
the VS Code tasks use the MSYS2 MinGW64 toolchain. The first build fetches and
builds the ARM backend of Unicorn 2.1.4, which needs MSYS2's `usr/bin/sh` on
Windows. Unicorn is GPLv2, so distributing a simulator binary means following
its license.

Run `build_host/prism` for the normal launcher, or pass a package to run that
exact Cortex-M0+ build under Unicorn:

```sh
build_host/prism build/hello.prism
```

| Key | Action |
| --- | --- |
| Left or A | left button |
| Right or D | right button |
| Space, Return, or Escape | menu button |
| Down / Up / Home | toggle a held left / right / menu press |
| 0 | release all toggled presses |

The host build also builds the unit tests; run them with
`ctest --test-dir build_host`.

## Cartridges

Start from `examples/hello-cartridge`. [docs/cartridges.md](docs/cartridges.md)
covers the cartridge lifecycle, services, asset packs, persistence, and the
package format.

## Assets

Generated sprite and song headers are committed, so normal builds never run
authoring tools. Configure with `-DPRISM_ASSET_AUTHORING=ON` to regenerate them
from the Aseprite and REAPER sources.

[docs/beatline-tracks.md](docs/beatline-tracks.md) describes Beatline's track
format.

## Web

`web` is a Next.js app. The `/manage` page talks to a connected Prism over
WebUSB (Chrome or Edge on desktop) to install and remove packages, change
settings, and mirror the screen. The leaderboard stores scores in libSQL
(Turso). Copy `web/.env.example` to `web/.env.local` and fill in
`TURSO_DATABASE_URL` and `TURSO_AUTH_TOKEN`.

```sh
cd web
npm install
npm run db:migrate
npm run dev
```
