# Hello cartridge

This directory is a complete cartridge project. One normal configure and build
produces the installable ARM cartridge package:

```sh
cmake -S . -B build -G Ninja -DPRISM_SDK_ROOT=/path/to/prism
cmake --build build
```

The output is `build/hello.prism`. No Prism CLI is involved.

Run it through the main Prism host simulator so desktop and hardware use the
same packaged ARM code and per-launch static-data lifecycle:

```sh
../../build_host/prism build/hello.prism
```

Use Left or A for Prism's left button, Right or D for its right button, and
Space or Escape for the menu button.

The example requests a tick divider of 8, so its callback runs at 120 Hz while
input continues to be sampled on Prism's public 960 Hz engine clock.

The SDK checkout is the only project-level dependency. Packaging needs CMake,
a normal C compiler for its outer build, and `arm-none-eabi-gcc`; CMake finds
the ARM toolchain on `PATH` and in the Raspberry Pi Pico VS Code extension's
default location.
