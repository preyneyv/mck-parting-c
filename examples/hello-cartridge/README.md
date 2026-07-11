# Hello cartridge

This is the seed for a standalone, cloneable cartridge template. `app.c` is a
complete cartridge: it owns a small zero-initialized state block, handles both
buttons, and draws with the normal u8g2 API.

Until sideloading exists, copy this directory into `src/cartridges`, add its C
files to `CARTRIDGE_SOURCES`, declare its descriptor in
`src/prism/cartridges.h`, and add it to the bundled registry. No generator or
cartridge-specific CLI is required.

Cartridges normally use `PRISM_CARTRIDGE_FLAG_NONE`. Bundled, hidden, and
undeletable policy belongs to the OS registry and cannot be claimed by a
cartridge.
