# Prism cartridges

Prism OS owns boot, the launcher, scheduling, the system menu, hardware
drivers, and the cartridge registry. Everything under `src/cartridges` is an
application compiled against the versioned public API in `src/prism`.
The build rejects direct cartridge includes of `shared`, `platform`, `host`, or
`rp2` headers, as well as direct access to `g_engine`.

## Authoring model

A cartridge exports one `prism_cartridge_t` descriptor with
`PRISM_CARTRIDGE`. The descriptor contains stable identity and lifecycle
callbacks:

- `enter` runs after state allocation.
- `tick` runs at the OS tick rate and is the right place for input and game
  simulation.
- `frame` draws the next 128x64 monochrome frame.
- `pause` and `resume` surround the system menu or sleep.
- `leave` releases resources owned outside the automatic state block.

Set `state_size` in the descriptor and use `prism->state`; the runtime allocates
and zeroes it on entry and frees it after `leave`. Module globals remain valid
for native PIC cartridges, but the state block makes teardown and testing much
cleaner.

Start with `examples/hello-cartridge`. The intended DX is a tiny repository
containing `app.c`, an icon, and assets—not a mandatory CLI or framework.

## Display and Elm

`prism_display(prism)` returns the real `u8g2_t *`. Cartridge code therefore
gets the full upstream u8g2 surface without waiting for Prism wrappers to catch
up. `prism/sdk.h` also exposes Elm, the lightweight layout helpers used by the
OS and bundled cartridges.

OS-owned operations use typed wrappers: buttons, ticks and time, LEDs, synth,
power state, animation, leaderboard QR generation, sleep, and reset. The ABI is
a versioned function table, so new operations can be appended without changing
the meaning of older entries.

## Bundled policy

The launcher enumerates OS-owned registry entries and ignores entries whose
policy contains `HIDDEN`. Built-in entries carry both `BUNDLED` and
`UNDELETABLE`; cartridges cannot assign those flags to themselves. Diagnostics
are bundled, undeletable, and hidden.

## Path to installable PIC binaries

Descriptors are retained in a dedicated `.prism_cartridge` ELF section. The
next loader milestone can package that section with position-independent code,
relocations, assets, ABI requirements, and a signature. A curated web installer
would validate the package, write it to an app flash region, and update an
atomic registry index. None of that storage or USB/web protocol is implemented
yet; the current milestone deliberately proves the same descriptor and OS API
with firmware-bundled cartridges first.

The PIC SDK must pin the compiler ABI and u8g2 version. Because cartridges get a
raw `u8g2_t *`, mismatched u8g2 struct layouts must be rejected by package ABI
metadata rather than guessed at runtime.

## Assets and builds

Generated sound and sprite headers are committed. Normal host and RP2040 builds
never launch REAPER or Aseprite. Configure with
`-DPRISM_ASSET_AUTHORING=ON` when intentionally using the authoring export
targets.
