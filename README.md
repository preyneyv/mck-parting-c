# Prism

Prism is a two-button RP2040 console with a 128x64 monochrome display, two RGB
LEDs, an FM synthesizer, and installable position-independent cartridges. The
same operating system and cartridge sources run in an SDL host simulator.

## Runtime architecture

Core 0 owns the engine, input, application lifecycle, display, LEDs, USB,
storage, and system UI. Its exposed integer clock and input sampler run at
960 Hz. Cartridges divide that clock to choose their simulation rate, while
the display renders at up to 120 frames per second.

Core 1 exclusively generates audio. Core 0 submits validated synth commands
through a bounded queue; voice/operator state and the render path are private
to the audio implementation. DMA sends packed 48 kHz stereo frames to the I2S
PIO state machine.

Code shared by both targets lives in `src/shared`. Hardware contracts are in
`src/platform`, with implementations in `src/rp2` and `src/host`. The public
cartridge API is under `src/prism`; cartridges cannot include engine or
platform internals.

## Building

The VS Code default build task configures an optimized RP2040 build, compiles
it, enters BOOTSEL, and flashes the device. Compilation-only tasks are also
available for both targets.

From a terminal:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The RP2040 build emits firmware outputs plus the bundled cartridges as ordinary
`.prism` packages. It requires the Raspberry Pi Pico SDK/toolchain; the Pico VS
Code extension installs the expected versions automatically.

For the host simulator:

```sh
cmake -S . -B build_host -G Ninja \
  -DPICO_PLATFORM=host -DCMAKE_BUILD_TYPE=Release
cmake --build build_host
```

The host target requires SDL2 with the `SDL2::SDL2-static` CMake target. On
Windows the configured tasks use the MSYS2 MinGW64 toolchain. The first host
build fetches the pinned Unicorn 2.1.4 sources and builds only its ARM backend;
MSYS2's `usr/bin/sh` is required for that one-time dependency build.

Run the normal launcher with no arguments, or pass an installable package to
execute that exact Cortex-M0+ payload under Unicorn and launch it directly:

```sh
build_host/prism build/hello.prism
```

The resulting host executable is self-contained. Unicorn is GPLv2-licensed;
distributing a simulator linked with it needs to comply with Unicorn's license.

## Cartridge development

`examples/hello-cartridge` is a standalone two-command project. Its normal
CMake build produces both a statically linked SDL simulator and `hello.prism`
from the same `app.c`; no Prism CLI is involved. See
[`docs/cartridges.md`](docs/cartridges.md) for the lifecycle, services, package
format, and persistence model.

## Assets

Generated sprite and song headers are committed, so ordinary builds never
launch authoring software. Configure with `-DPRISM_ASSET_AUTHORING=ON` only when
intentionally regenerating assets from Aseprite or REAPER sources.

## Repository map

- `src/prism`: public cartridge SDK and package/wire formats
- `src/shared`: engine, OS services, animation, and audio implementation
- `src/platform`: target-independent hardware contracts
- `src/rp2`: RP2040 drivers, USB management, flash storage, and persistence
- `src/host`: SDL simulator platform
- `src/cartridges`: bundled cartridge sources
- `examples/hello-cartridge`: cloneable cartridge project
- `web`: local Next.js management and leaderboard service
- `cmake`, `scripts`: package, asset, and build tooling
