#include "audio_test.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>

// This example shows how to use your existing synthesis code
// You can copy functions from your src/audio.c and adapt them here

// Example synthesis state
static float phase = 0.0f;
static float frequency = 440.0f; // A4
static uint32_t current_sample_rate = 22000;

// Example: Simple sine wave generator (replace with your synthesis code)
void synth_fill_buffers(void) {
    int start_pos, end_pos;
    
    // Determine which half of the buffer to fill
    if (need_fill_first_half) {
        start_pos = 0;
        end_pos = HALF_BUFFER_SIZE;
        need_fill_first_half = false;
        printf("Filling first half of buffer\n");
    } else if (need_fill_second_half) {
        start_pos = HALF_BUFFER_SIZE;
        end_pos = BUFFER_SIZE;
        need_fill_second_half = false;
        printf("Filling second half of buffer\n");
    } else {
        return; // Nothing to fill
    }
    
    // Generate audio samples
    float phase_increment = frequency / current_sample_rate;
    
    for (int i = start_pos; i < end_pos; i++) {
        // Generate sine wave sample
        float sample = sinf(phase * 2.0f * M_PI);
        
        // Convert to 8-bit unsigned (0-255, where 128 is center/silence)
        audio_buffer[i] = (uint8_t)((sample * 127.0f) + 128.0f);
        
        // Update phase
        phase += phase_increment;
        if (phase >= 1.0f) {
            phase -= 1.0f;
        }
    }
}

// Example of how to change the frequency during playback
void set_frequency(float freq) {
    frequency = freq;
    printf("Frequency set to %.2f Hz\n", freq);
}

int main(int argc, char *argv[]) {
    printf("macOS Audio Test for RP2040 Synthesis Code\n");
    printf("==========================================\n");
    
    // Parse command line arguments for sample rate
    uint32_t sample_rate = 22000;
    if (argc > 1) {
        sample_rate = (uint32_t)atoi(argv[1]);
        if (sample_rate < 8000 || sample_rate > 48000) {
            printf("Invalid sample rate. Using default 22000 Hz\n");
            sample_rate = 22000;
        }
    }
    
    current_sample_rate = sample_rate;
    
    printf("Sample rate: %u Hz\n", sample_rate);
    printf("Buffer size: %d samples\n", BUFFER_SIZE);
    printf("Commands:\n");
    printf("  Enter: Start/continue playback\n");
    printf("  1-9: Change frequency (100-900 Hz)\n");
    printf("  q: Quit\n\n");
    
    // Initialize audio system
    if (audio_init(sample_rate) != 0) {
        printf("Failed to initialize audio system\n");
        return 1;
    }
    
    // Start audio playback
    if (audio_start() != 0) {
        printf("Failed to start audio playback\n");
        audio_cleanup();
        return 1;
    }
    
    // Interactive loop
    char input;
    while (1) {
        printf("Command: ");
        fflush(stdout);
        
        input = getchar();
        
        switch (input) {
            case 'q':
            case 'Q':
                goto cleanup;
                
            case '1': case '2': case '3': case '4': case '5':
            case '6': case '7': case '8': case '9':
                set_frequency((input - '0') * 100.0f);
                break;
                
            case '\n':
                // Just continue
                break;
                
            default:
                printf("Unknown command '%c'\n", input);
                break;
        }
    }
    
cleanup:
    printf("\nShutting down...\n");
    audio_cleanup();
    return 0;
}
