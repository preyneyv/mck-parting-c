# Hello cartridge

This directory is a complete cartridge project. One normal configure and build
produces both the installable cartridge and a statically linked SDL simulator:

```sh
cmake -S . -B build -G Ninja -DPRISM_SDK_ROOT=/path/to/prism
cmake --build build
```

The outputs are `build/hello.prism` and `build/hello-simulator` (with the
platform executable suffix where applicable). No Prism CLI is involved.

The simulator compiles the same `app.c` used by the RP2040 package. Use Left or
A for Prism's left button, Right or D for its right button, and Space or Escape
for the menu button.

The example requests a tick divider of 8, so its callback runs at 120 Hz while
input continues to be sampled on Prism's public 960 Hz engine clock.

The SDK checkout is the only project-level dependency. The host needs CMake,
an SDL2 package containing the `SDL2::SDL2-static` target, and a normal C
compiler. Packaging also needs `arm-none-eabi-gcc`; CMake finds the toolchain on
`PATH` and in the Raspberry Pi Pico VS Code extension's default location.
