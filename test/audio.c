#include "audio_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

// Audio configuration
#define BUFFER_SIZE 256
#define HALF_BUFFER_SIZE (BUFFER_SIZE / 2)
#define DEFAULT_SAMPLE_RATE 22000
#define AUDIO_QUEUE_BUFFER_COUNT 3

// Audio buffer and state
uint8_t audio_buffer[BUFFER_SIZE];
volatile int buffer_position = 0;
volatile bool need_fill_first_half = false;
volatile bool need_fill_second_half = false;
volatile bool audio_running = false;

// Core Audio components
AudioQueueRef audioQueue = NULL;
AudioQueueBufferRef audioQueueBuffers[AUDIO_QUEUE_BUFFER_COUNT];
uint32_t sample_rate = DEFAULT_SAMPLE_RATE;

// Forward declarations
void synth_fill_buffers(void);

// Audio queue callback - called when a buffer needs to be filled
static void audioQueueCallback(void *inUserData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer) {
    (void)inUserData; // Suppress unused parameter warning
    
    if (!audio_running) {
        return;
    }
    
    // Convert 8-bit audio buffer to 16-bit stereo for Core Audio
    int16_t *output = (int16_t *)inBuffer->mAudioData;
    uint32_t frames = inBuffer->mAudioDataBytesCapacity / (2 * sizeof(int16_t)); // stereo 16-bit
    
    for (uint32_t i = 0; i < frames; i++) {
        // Check if we need to refill the synthesis buffer
        if (buffer_position >= BUFFER_SIZE) {
            buffer_position = 0;
        }
        
        // Trigger buffer fill when we cross half-buffer boundaries
        if (buffer_position == 0) {
            need_fill_second_half = true;
            synth_fill_buffers();
        } else if (buffer_position == HALF_BUFFER_SIZE) {
            need_fill_first_half = true;
            synth_fill_buffers();
        }
        
        // Convert 8-bit unsigned to 16-bit signed and duplicate for stereo
        int16_t sample = (int16_t)((audio_buffer[buffer_position] - 128) << 8);
        output[i * 2] = sample;     // Left channel
        output[i * 2 + 1] = sample; // Right channel
        
        buffer_position++;
    }
    
    inBuffer->mAudioDataByteSize = frames * 2 * sizeof(int16_t);
    AudioQueueEnqueueBuffer(inAQ, inBuffer, 0, NULL);
}

// Initialize audio system
int audio_init(uint32_t sample_rate_hz) {
    OSStatus status;
    
    sample_rate = sample_rate_hz;
    
    // Set up audio format (stereo 16-bit)
    AudioStreamBasicDescription audioFormat = {0};
    audioFormat.mSampleRate = sample_rate;
    audioFormat.mFormatID = kAudioFormatLinearPCM;
    audioFormat.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    audioFormat.mBytesPerPacket = 4; // 2 channels * 2 bytes per sample
    audioFormat.mFramesPerPacket = 1;
    audioFormat.mBytesPerFrame = 4;
    audioFormat.mChannelsPerFrame = 2;
    audioFormat.mBitsPerChannel = 16;
    
    // Create audio queue
    status = AudioQueueNewOutput(&audioFormat, audioQueueCallback, NULL, NULL, NULL, 0, &audioQueue);
    if (status != noErr) {
        printf("Error creating audio queue: %d\n", status);
        return -1;
    }
    
    // Create and allocate buffers
    uint32_t bufferSize = (sample_rate / 10) * 4; // 100ms buffer in bytes (stereo 16-bit)
    
    for (int i = 0; i < AUDIO_QUEUE_BUFFER_COUNT; i++) {
        status = AudioQueueAllocateBuffer(audioQueue, bufferSize, &audioQueueBuffers[i]);
        if (status != noErr) {
            printf("Error allocating audio buffer %d: %d\n", i, status);
            return -1;
        }
        
        // Initialize buffer with silence and enqueue
        memset(audioQueueBuffers[i]->mAudioData, 0, bufferSize);
        audioQueueBuffers[i]->mAudioDataByteSize = bufferSize;
        AudioQueueEnqueueBuffer(audioQueue, audioQueueBuffers[i], 0, NULL);
    }
    
    // Initialize synthesis buffer with silence
    memset(audio_buffer, 128, BUFFER_SIZE); // 128 = center for 8-bit unsigned
    
    printf("Audio initialized: %u Hz, %u ms buffers\n", sample_rate, (bufferSize * 1000) / (sample_rate * 4));
    
    return 0;
}

// Start audio playback
int audio_start() {
    if (!audioQueue) {
        printf("Audio not initialized\n");
        return -1;
    }
    
    audio_running = true;
    OSStatus status = AudioQueueStart(audioQueue, NULL);
    if (status != noErr) {
        printf("Error starting audio queue: %d\n", status);
        audio_running = false;
        return -1;
    }
    
    printf("Audio playback started\n");
    return 0;
}

// Stop audio playback
void audio_stop() {
    if (!audioQueue) {
        return;
    }
    
    audio_running = false;
    AudioQueueStop(audioQueue, true);
    printf("Audio playback stopped\n");
}

// Clean up audio resources
void audio_cleanup() {
    if (!audioQueue) {
        return;
    }
    
    audio_stop();
    
    // Dispose of buffers
    for (int i = 0; i < AUDIO_QUEUE_BUFFER_COUNT; i++) {
        if (audioQueueBuffers[i]) {
            AudioQueueFreeBuffer(audioQueue, audioQueueBuffers[i]);
        }
    }
    
    // Dispose of audio queue
    AudioQueueDispose(audioQueue, true);
    audioQueue = NULL;
    
    printf("Audio cleaned up\n");
}

// Default synthesis function (weak symbol, can be overridden)
__attribute__((weak))
void synth_fill_buffers() {
    static float phase = 0.0f;
    float freq = 440.0f; // A4 note
    float phase_increment = freq / sample_rate;
    
    int start_pos, end_pos;
    
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
    
    // Generate sine wave test signal
    for (int i = start_pos; i < end_pos; i++) {
        float sample = sinf(phase * 2.0f * M_PI);
        audio_buffer[i] = (uint8_t)((sample * 127.0f) + 128.0f); // Convert to 8-bit unsigned
        
        phase += phase_increment;
        if (phase >= 1.0f) {
            phase -= 1.0f;
        }
    }
}