# Prism cartridges

A cartridge is position-independent Cortex-M0+ code in a `.prism` package. The
same package runs on the device and in the host simulator, which executes it
under Unicorn while the OS, display, audio, and input stay native:

```sh
build_host/prism path/to/cartridge.prism
```

The simulator rejects malformed packages before it opens a window.

## Project shape

Start from `examples/hello-cartridge`. A project needs an `app.c`, a 36x36
monochrome icon, and a `CMakeLists.txt` that calls
`prism_add_installable_cartridge`. Building the project produces the `.prism`
file.

Include `<prism/sdk.h>` for the public API. Cartridge sources may include
`prism/*`, standard library headers, and their own headers. Both bundled and
standalone builds reject includes from `shared`, `platform`, `host`, or `rp2`.

## Declaration

Every cartridge exports one descriptor:

```c
PRISM_CARTRIDGE(cartridge_example,
    .id = "dev.example.prism.example",
    .name = "example",
    .version = 1,
    .tick_divider = 8, // 960 / 8 = 120 Hz
    .icon = example_icon,
    .enter = enter,
    .tick = tick,
    .frame = frame,
    .pause = pause,
    .resume = resume,
    .leave = leave,
);
```

- `id` is the stable identity. It must be a lowercase reverse domain name:
  1–253 ASCII bytes made of dot-separated 1–63 byte labels using lowercase
  letters, digits, and interior hyphens.
- `name` is shown to users and may change between versions.
- `version` is a 32-bit number. Installing the same version again is allowed,
  a higher version replaces the installed one, and a lower version is rejected.

The packager derives an internal 16-byte `app_key` from the ID for fixed-size
lookups; authors never set it. `icon` and `frame` are required. The packager
rejects missing or malformed metadata, icons, or frame callbacks, and the
runtime checks them again before launch. Everything else may be omitted and
defaults to zero.

`tick_divider` sets how often `tick` runs relative to the 960 Hz
`PRISM_ENGINE_TICK_RATE`: 1 runs at 960 Hz, 8 at 120 Hz, 16 at 60 Hz, and so
on. Any divider from 1 to 65535 works, and zero is treated as one.

## State

Use ordinary static variables for state that lasts one launch:

```c
static example_state_t state;
static uint32_t attempts = 3;
```

The package records the initialized data and BSS size, and each launch
rebuilds them. Zero-initialized statics start at zero and initialized statics
return to their initial values on every launch. `static const` data stays in
the read-only image.

State that survives relaunches is explicit. Set `persistent_size` and
`persistent_schema_version`, modify `prism->persistent`, then call
`prism_persist`. The OS coalesces writes. Uninstalling a cartridge deletes its
persistent state.

## Lifecycle

- `enter`: set up the cartridge. Statics are already initialized.
- `tick`: handle input and simulation at `960 / tick_divider` Hz.
- `frame`: draw the next 128x64 frame.
- `pause` / `resume`: called around the system menu and sleep.
- `leave`: release anything not held in statics or persistent state.

Input is sampled at 960 Hz regardless of the divider. Key-down and key-up
events stay latched until the next `tick`, so a press and release that both
happen between two ticks are still seen. Their exact times are available from
`prism_button_keydown_tick` and `prism_button_keyup_tick`.

`prism_ticks` counts active 960 Hz engine ticks and stops while the cartridge
is paused. `prism_millis` is the same clock in milliseconds.
`prism_now_us` is wall-clock time in microseconds. Animation durations are in
milliseconds.

## Rendering and services

`prism_display` returns the `u8g2_t *` for the screen, so the full u8g2
drawing API is available. `<prism/sdk.h>` also includes Elm, the layout helper
used by the bundled cartridges and the system UI.

The SDK has wrappers for buttons, time, LEDs, audio, animation, power, sleep,
persistence, and leaderboard QR codes. They call into the OS through a
versioned function table; cartridges never touch engine or hardware state
directly.

## C runtime

Cartridges use normal C operators and standard library functions without
including Pico SDK headers. The package linker turns Cortex-M0+ compiler
helpers into versioned imports. On the RP2040 these resolve to the Pico SDK's
optimized divider, Boot ROM, and floating-point routines, and the simulator
implements the same ARM EABI behavior.

This covers 32- and 64-bit division and remainder, 64-bit multiplication,
count-leading/trailing-zero and population-count builtins, compiler-generated
memory copies and fills, and single- and double-precision arithmetic,
comparisons, and conversions. Explicit `memcpy`, `memset`, and the supported
single-precision math functions use the firmware implementations too.

`printf`, `vprintf`, `puts`, `putchar`, `sprintf`, `snprintf`, and
`vsnprintf` are available. On hardware, output goes to the firmware's Pico
stdio backends; the simulator writes it to the console. `getchar` is also
available but blocks, so avoid it in lifecycle and rendering callbacks.

## Asset packs

An asset pack is an uncompressed `.prismpack` archive. Packs and cartridges
share the device's 192 storage blocks of 64 KiB each. A pack declares its own
reverse domain name ID, display name, 32-bit version, target cartridge ID, and
an optional inclusive range of target versions. Versions follow the same rules
as cartridges. A pack whose target is missing or outside the declared range stays
installed but disabled, so it can still be listed and deleted.

File paths are printable ASCII, such as `tracks/song.beatline`. There are no
directory entries, compression, or extraction; on the RP2040 the pack's
directory and file contents are read directly from flash. A cartridge can walk
its compatible packs and files without building an index in RAM:

```c
for (uint32_t pack_index = 0; pack_index < prism_asset_pack_count(prism);
     ++pack_index)
{
  prism_asset_pack_info_t pack;
  if (!prism_asset_pack_get(prism, pack_index, &pack))
    continue;
  for (uint32_t file_index = 0; file_index < pack.file_count; ++file_index)
  {
    prism_asset_file_t file;
    if (prism_asset_file_get(prism, pack.handle, file_index, &file))
    {
      // file.path, file.data, and file.size are valid for this launch.
    }
  }
}
```

Returned pointers are read-only and valid until the cartridge exits.
Installing, deleting, or compacting storage closes the running cartridge
before anything in flash moves. The simulator maps bundled pack data read-only
into the emulated address space.

In CMake, `prism_add_asset_pack` builds a pack from `FILES` arguments written
as `path=source`. It wraps `scripts/package_assets.py`, which can also be run
directly.

## Packages

The package header records the cartridge ABI version, a hash of the u8g2 ABI,
the generated `app_key`, image locations, imports, relocations, and the
persistence layout. The ID, name, and version are read from the descriptor and
are not duplicated in the header. The packager rejects unsupported imports and
malformed images, and the device checks the `app_key` against the descriptor's
ID before showing the cartridge in the launcher.

This is cartridge ABI v1 and package format v1. Cartridges built against an
earlier development SDK must be rebuilt.

The RP2040 build produces two firmware images:

- `prism.uf2` contains only firmware. Flashing it leaves installed cartridges,
  their saves, and device settings alone, and does not bring back cartridges
  the user removed.
- `prism_factory.uf2` installs the bundled `.prism` and `.prismpack` files
  through the normal storage catalog. It replaces both catalog slots,
  invalidates any interrupted compaction, and resets cartridge persistence and
  device settings, so it can be flashed over an existing device without
  erasing flash first. Bytes from old packages stay until their blocks are
  reused. Factory-installed packages are not marked in any way and are
  updated, deleted, and compacted like any other package.
