## Requirements

- CMake 3.13+
- Ninja
- Raspberry Pi Pico SDK
  - Install the Raspberry Pi Pico extension for VSCode to auto-download/setup
- Host builds: Standard C compiler (`clang`/`gcc`)

## Building

Run `scripts/configure.sh` to create build directories, or do the following manually.

### Pico

```sh
mkdir build
cd build
cmake -GNinja ..
ninja
```

### Host (macOS)

```sh
mkdir build_host
cd build_host
cmake -GNinja -DPICO_PLATFORM=host ..
ninja
```

## Project Structure

- `src/common/` - Cross-platform code
- `src/host/` - Host test executables (auto-discovered)
- `src/rp2/` - Pico-specific code
