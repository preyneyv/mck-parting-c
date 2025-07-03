#include "audio_test.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// This example shows how to port your RP2040 synthesis code
// It includes the lookup tables and functions from your original audio.c

// Sine lookup table (copied from your original code)
#define SIN_LUT_SIZE 1024
static float sin_lut[SIN_LUT_SIZE];

// Exponential lookup table for 2^x over x in [-0.5, 0.5]
#define EXP_LUT_SIZE 128
static float exp_lut[EXP_LUT_SIZE];
#define INV_1200F (1.0f / 1200.0f)

// Fast sine lookup (phase in [0,1)) - copied from your original code
static inline float f_sin(float phase)
{
    int idx = (int)(phase * SIN_LUT_SIZE) & (SIN_LUT_SIZE - 1);
    return sin_lut[idx];
}

static inline float f_exp(float x)
{
    // clamp to LUT range
    if (x < -0.5f)
        x = -0.5f;
    else if (x > 0.5f)
        x = 0.5f;
    // map to index
    int idx = (int)((x + 0.5f) * (EXP_LUT_SIZE - 1) + 0.5f);
    return exp_lut[idx];
}

// Initialize lookup tables
static void init_lookup_tables(void) {
    // Initialize sine lookup table
    for (int i = 0; i < SIN_LUT_SIZE; i++) {
        sin_lut[i] = sinf(2.0f * M_PI * i / SIN_LUT_SIZE);
    }
    
    // Initialize exponential lookup table for 2^x
    for (int i = 0; i < EXP_LUT_SIZE; i++) {
        float x = (i / (float)(EXP_LUT_SIZE - 1)) - 0.5f; // x in [-0.5, 0.5]
        exp_lut[i] = powf(2.0f, x);
    }
    
    printf("Lookup tables initialized\n");
}

// Example synthesis state (add your own state variables here)
static float osc_phase = 0.0f;
static float osc_frequency = 440.0f;
static float lfo_phase = 0.0f;
static float lfo_frequency = 2.0f;
static uint32_t current_sample_rate = 22000;

// Your synthesis code goes here - this is where you port your RP2040 synth_fill_buffers
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
    
    // Calculate phase increments
    float osc_phase_inc = osc_frequency / current_sample_rate;
    float lfo_phase_inc = lfo_frequency / current_sample_rate;
    
    // Generate audio samples using your synthesis algorithm
    for (int i = start_pos; i < end_pos; i++) {
        // LFO for frequency modulation
        float lfo_value = f_sin(lfo_phase);
        float modulated_freq = osc_frequency + (lfo_value * 50.0f); // 50Hz vibrato
        
        // Generate oscillator sample using fast sine lookup
        float sample = f_sin(osc_phase);
        
        // Apply some filtering or effects here if needed
        // sample = apply_filter(sample);
        
        // Convert to 8-bit unsigned (0-255, where 128 = silence)
        audio_buffer[i] = (uint8_t)((sample * 127.0f) + 128.0f);
        
        // Update phases
        osc_phase += osc_phase_inc;
        if (osc_phase >= 1.0f) {
            osc_phase -= 1.0f;
        }
        
        lfo_phase += lfo_phase_inc;
        if (lfo_phase >= 1.0f) {
            lfo_phase -= 1.0f;
        }
    }
}

// Control functions
void set_oscillator_frequency(float freq) {
    osc_frequency = freq;
    printf("Oscillator frequency: %.2f Hz\n", freq);
}

void set_lfo_frequency(float freq) {
    lfo_frequency = freq;
    printf("LFO frequency: %.2f Hz\n", freq);
}

int main(int argc, char *argv[]) {
    printf("RP2040 Synthesis Code Port - macOS Test\n");
    printf("========================================\n");
    
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
    
    // Initialize lookup tables (important for performance!)
    init_lookup_tables();
    
    printf("Sample rate: %u Hz\n", sample_rate);
    printf("Buffer size: %d samples\n", BUFFER_SIZE);
    printf("Commands:\n");
    printf("  1-9: Oscillator frequency (100-900 Hz)\n");
    printf("  a-j: LFO frequency (0.5-5.0 Hz)\n");
    printf("  s: Show current settings\n");
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
                set_oscillator_frequency((input - '0') * 100.0f);
                break;
                
            case 'a': case 'b': case 'c': case 'd': case 'e':
            case 'f': case 'g': case 'h': case 'i': case 'j':
                set_lfo_frequency(0.5f + (input - 'a') * 0.5f);
                break;
                
            case 's':
                printf("Current settings:\n");
                printf("  Oscillator: %.2f Hz\n", osc_frequency);
                printf("  LFO: %.2f Hz\n", lfo_frequency);
                printf("  Sample rate: %u Hz\n", current_sample_rate);
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
