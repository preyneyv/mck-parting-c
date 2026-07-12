# Prism cartridges

A cartridge is position-independent ARM code packaged in a `.prism` file. The
same source also runs in the SDL host simulator, so cartridge authors can
iterate without hardware.

The main Prism simulator can also execute the packaged ARM artifact itself:

```sh
build_host/prism path/to/cartridge.prism
```

In that mode the OS, display, audio, and input remain native desktop code while
the cartridge's Cortex-M0+ lifecycle runs through Unicorn. This is the closest
desktop test to installing the package on hardware; malformed packages are
rejected before the simulator window starts.

## Project shape

Start with `examples/hello-cartridge`. A cartridge project needs an `app.c`, a
36x36 monochrome icon, and a small `CMakeLists.txt` that calls
`prism_add_installable_cartridge`. Building the project produces the `.prism`
file; there is no Prism-specific CLI.

Include `<prism/sdk.h>` for the public API. Cartridge source may include
`prism/*`, standard-library headers, and its own local headers. Both bundled and
standalone builds reject direct includes from `shared`, `platform`, `host`, or
`rp2`.

## Cartridge declaration

Every cartridge exports one descriptor with named fields:

```c
PRISM_CARTRIDGE(cartridge_example,
    .id = "dev.example.prism.example",
    .name = "example",
    .version = 1,
    .tick_divider = 8, // 960 / 8 = 120 Hz
    .icon = example_icon,
    .state_size = sizeof(example_state_t),
    .enter = enter,
    .tick = tick,
    .frame = frame,
    .pause = pause,
    .resume = resume,
    .leave = leave,
);
```

`id`, `name`, and `version` are the cartridge's three authored metadata fields.
The ID is the stable identity and must be a canonical lowercase reverse-FQDN:
1–253 ASCII bytes, dot-separated 1–63 byte labels, and only lowercase letters,
digits, and interior hyphens. The mutable name is displayed to users, while the
32-bit version controls updates. Reinstalling the same version is allowed;
lower versions are rejected and higher versions replace the installed package.

The descriptor is the sole source of truth for this metadata. The packager
derives a 16-byte internal `app_key` from the complete ID for fixed-size lookup
records. Authors do not choose or declare that key. A 36x36 monochrome `icon`
and `frame` are also required. The packager rejects missing or malformed
metadata, icons, or frame callbacks before producing a `.prism` file, and the
runtime repeats those checks before launch. Other callbacks and sizes may be
omitted and default to zero.

`tick_divider` controls how often `tick` runs relative to the public
`PRISM_ENGINE_TICK_RATE` clock. A zero or omitted divider is normalized to the
default of one. Divider 1 runs at 960 Hz, 2 at 480 Hz, 4 at 240 Hz, 8 at
120 Hz, and 16 at 60 Hz. Arbitrary nonzero integer dividers are valid.

The runtime allocates and zeroes `state_size` bytes before `enter`, exposes the
allocation as `prism->state`, and frees it after `leave`. Persistent state works
the same way: set `persistent_size` and `persistent_schema_version`, mutate
`prism->persistent`, then call `prism_persist`. Writes are coalesced by the OS.
Uninstalling a cartridge deletes its persistent state.

## Lifecycle

- `enter`: initialize the cartridge after state is available.
- `tick`: handle input and simulation at `960 / tick_divider` Hz.
- `frame`: draw the next 128x64 frame.
- `pause` / `resume`: surround the system menu and sleep.
- `leave`: release resources not owned by the state blocks.

Input is sampled at 960 Hz independently of the cartridge divider. Key-down
and key-up transitions remain latched until the next cartridge tick, and their
raw timestamps are available through `prism_button_keydown_tick` and
`prism_button_keyup_tick`. A press and release between divided callbacks are
therefore both preserved.

`prism_ticks` is an integer count of active 960 Hz engine ticks. It freezes
while the cartridge is paused. `prism_millis` converts that same paused clock
to integer milliseconds, while `prism_now_us` exposes wall-clock microseconds.
Animation durations are always authored in milliseconds.

## Rendering and services

`prism_display` returns the active `u8g2_t *`, giving cartridges the complete
u8g2 drawing API. `<prism/sdk.h>` also exposes Elm, the layout helper used by
the bundled cartridges and system UI.

Typed wrappers cover buttons, time, LEDs, audio, animation, power, sleep,
persistence, and leaderboard QR generation. The runtime passes these services
through a versioned function table; cartridges never access engine or hardware
state directly.

### Optimized C runtime

Cartridges use ordinary C operators and standard functions; they do not include
Pico SDK headers. The package linker turns Cortex-M0+ compiler helpers into
versioned Prism imports. On RP2040 those imports resolve to the Pico SDK's
optimized divider, Boot ROM, and numeric routines. Prism Host implements the
same ARM EABI behavior for the packaged ARM program.

This applies to 32- and 64-bit integer division and remainder, 64-bit
multiplication, count-leading/trailing-zero and population-count builtins,
compiler-generated memory copy/set operations, and the basic single- and
double-precision arithmetic, comparison, and conversion helpers. Explicit
`memcpy`, `memset`, and the supported single-precision math functions also use
the firmware implementations. Constant expressions may still be folded or
inlined by the compiler.

Standard diagnostic output is available through `printf`, `vprintf`, `puts`,
and `putchar`; formatted buffer functions include `sprintf`, `snprintf`, and
`vsnprintf`. On hardware, console output uses the firmware's configured Pico
stdio backends. Prism Host writes it to the host process console. `getchar` is
also available but blocks, so cartridges should avoid it in lifecycle and
rendering callbacks. A bounded Prism logging service is preferred for normal
cartridge diagnostics once available.

## Packages

The package header records the cartridge ABI, u8g2 ABI hash, generated
`app_key`, image locations, imports, relocations, and persistence layout. It
does not duplicate the descriptor's ID, name, or version. The packager rejects
unsupported imports and malformed images. The device verifies the generated
key against the descriptor's complete ID before exposing an installed
cartridge to the launcher.

Cartridge ABI v1 and package format v1 define the initial compatibility
baseline. Rebuild older development cartridges with the current SDK.

The default firmware embeds the normal bundled cartridges as ordinary
`.prism` packages in the initial storage image. They use the same loader and
remain uninstallable like any other cartridge.
