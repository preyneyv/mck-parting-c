# macOS Audio Testing for RP2040 Synthesis

This test system allows you to run your RP2040 synthesis code natively on macOS using Core Audio for playback. It emulates the same 8-bit audio buffer system used on the RP2040, making it easy to test and debug your synthesis algorithms.

## Features

- **8-bit audio buffer compatibility**: Uses the same `audio_buffer[BUFFER_SIZE]` as your RP2040 code
- **Configurable sample rate**: Test with different sample rates (8kHz - 48kHz)
- **Real-time playback**: Uses Core Audio for low-latency audio output
- **Buffer management**: Emulates the same double-buffering system as the RP2040
- **Easy integration**: Drop in your existing synthesis functions

## Building

```bash
cd test
make all
```

This creates three executables:
- `synth_example`: Interactive example showing how to integrate synthesis code
- `rp2040_port_example`: Example showing how to port RP2040 synthesis code with lookup tables

## Usage

### Synthesis Example
```bash
make run
# or
./synth_example [sample_rate]
```

### RP2040 Port Example
```bash
make run-rp2040
# or
./rp2040_port_example [sample_rate]
```

Example with custom sample rate:
```bash
./synth_example 44100
```

## Integrating Your Synthesis Code

1. **Copy your synthesis functions** from `src/audio.c` to a new file in the `test/` directory
2. **Remove RP2040-specific includes** like `pico/stdlib.h`, `hardware/*`
3. **Adapt the `synth_fill_buffers()` function** to work with the test system:

```c
#include "audio_test.h"

// Your synthesis state variables here
static float phase = 0.0f;
static float frequency = 440.0f;

void synth_fill_buffers(void) {
    int start_pos, end_pos;
    
    // Determine which half of the buffer to fill
    if (need_fill_first_half) {
        start_pos = 0;
        end_pos = HALF_BUFFER_SIZE;
        need_fill_first_half = false;
    } else if (need_fill_second_half) {
        start_pos = HALF_BUFFER_SIZE;
        end_pos = BUFFER_SIZE;
        need_fill_second_half = false;
    } else {
        return; // Nothing to fill
    }
    
    // Your synthesis code here - fill audio_buffer[start_pos] to audio_buffer[end_pos-1]
    // Values should be 8-bit unsigned (0-255, where 128 = silence)
    for (int i = start_pos; i < end_pos; i++) {
        // Generate your audio sample
        float sample = /* your synthesis code */;
        audio_buffer[i] = (uint8_t)((sample * 127.0f) + 128.0f);
    }
}
```

4. **Compile your test**:
```bash
gcc -Wall -O2 -framework AudioToolbox -framework CoreAudio -o my_synth_test my_synth.c audio_lib.o
```

## Audio Buffer Format

- **Type**: `uint8_t audio_buffer[BUFFER_SIZE]`
- **Size**: 256 samples (configurable via `BUFFER_SIZE`)
- **Format**: 8-bit unsigned values (0-255)
- **Center/Silence**: 128
- **Range**: 0 = maximum negative, 255 = maximum positive

## Buffer Management

The system uses double-buffering:
- When `need_fill_first_half` is true, fill samples 0-127
- When `need_fill_second_half` is true, fill samples 128-255
- Always clear the flag after filling: `need_fill_first_half = false`

## Performance Tips

- The synthesis function runs in an audio callback thread
- Keep processing lightweight to avoid audio dropouts
- Precompute lookup tables and expensive calculations
- Use the same optimization techniques as your RP2040 code

## Sample Rates

Common sample rates for testing:
- 22050 Hz (matches your RP2040 config)
- 44100 Hz (CD quality)
- 48000 Hz (professional audio)
- 8000 Hz (telephone quality, good for testing performance)

## Debugging

- Use `printf()` sparingly in `synth_fill_buffers()` as it runs in audio thread
- Monitor buffer fill flags to ensure proper synchronization
- Test with different sample rates to verify timing
- Use the interactive example to test parameter changes in real-time

## Cleaning Up

```bash
make clean
```

This removes all compiled binaries and object files.
