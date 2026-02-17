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

If you get an error about a Python interpreter, make sure you have Python 3 installed. You can explicitly set the path by running

```sh
cmake -DPython3_EXECUTABLE=[path/to/python/executable] [other options] ..
```

On my Windows machine with `pyenv-win`, this becomes:

```powershell
cmake -GNinja -DPython3_EXECUTABLE="$(pyenv which python)" ..
```

## Project Structure

- `src/common/` - Cross-platform code
- `src/host/` - Host test executables (auto-discovered)
- `src/rp2/` - Pico-specific code
