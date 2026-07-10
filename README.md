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

This defaults to a `Release` build, enabling the compiler's optimized release flags. For a debug build, pass `-DCMAKE_BUILD_TYPE=Debug` when configuring.

### Host (macOS)

```sh
mkdir build_host
cd build_host
cmake -GNinja -DPICO_PLATFORM=host -DCMAKE_BUILD_TYPE=Release ..
ninja
```

If you get an error about a Python interpreter, make sure you have Python 3 installed. You can explicitly set the path by running

```sh
cmake -DPython3_EXECUTABLE=[path/to/python/executable] [other options] ..
```

On my Windows machine with `pyenv-win`, this becomes:

```powershell
cmake -GNinja -DPython3_EXECUTABLE="$(pyenv which python)" ..
```

## Asset Generation

Applications have sounds and sprites. Sounds can be authored with REAPER and sprites can be authored with Aseprite. The build system has a build step to automatically generate C header files from these source files.

Sounds live under `src/shared/apps/<app_name>/sounds/*.rpp` and sprites live under `src/shared/apps/<app_name>/sprites/*.aseprite`, both commited to Git.

CMake will try to find REAPER and Aseprite on your machine, and if it succeeds, it will automatically generate corresponding `.h` files in the same directory. I chose to commit these generated files to allow the firmware to be compiled even if neither of these programs are installed, since they're only required if you're modifying those sources.

## Project Structure

- `src/common/` - Cross-platform code
- `src/host/` - Host test executables (auto-discovered)
- `src/rp2/` - Pico-specific code
