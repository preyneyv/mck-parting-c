# Hello cartridge

A minimal standalone cartridge project. Copy this directory to start a new
cartridge, and change the `.id` in `app.c` to a reverse domain name you
control.

```sh
cmake -S . -B build -G Ninja -DPRISM_SDK_ROOT=/path/to/prism
cmake --build build
```

This produces `build/hello.prism`. `PRISM_SDK_ROOT` defaults to the repository
root two levels up, so it can be left out when building in place.

The build needs CMake, Python 3, a C compiler for the outer project, and
`arm-none-eabi-gcc` for the package itself. CMake looks for the ARM compiler on
`PATH` and in the Pico VS Code extension's default install location.

Run the package in the Prism simulator. The main README covers building the
simulator and its keyboard controls.

```sh
../../build_host/prism build/hello.prism
```

The cartridge sets `tick_divider` to 8, so `tick` runs at 120 Hz. Input is
still sampled at 960 Hz. See [docs/cartridges.md](../../docs/cartridges.md)
for the full cartridge API.
