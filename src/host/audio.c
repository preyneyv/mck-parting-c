#include <stdio.h>

#include <pthread.h>
#include <soundio/soundio.h>

#include <shared/audio/buffer.h>
#include <shared/audio/synth.h>
#include <shared/utils/timing.h>

#include "audio.h"
#include "config.h"
#include "math.h"
#include "time.h"

static audio_buffer_pool_t pool;

static inline void _write_frames_from_buffer(struct SoundIoChannelArea **areas,
                                             int frame_count,
                                             uint32_t *buffer) {
  for (int index = 0; index < frame_count; index++) {
    uint32_t frame = buffer[index];
    int16_t left = ((frame >> 16) & 0xFFFF);
    int16_t right = (frame & 0xFFFF);

    int16_t *buf = (int16_t *)((*areas)[0].ptr);
    *buf = left;
    (*areas)[0].ptr += (*areas)[0].step;

    buf = (int16_t *)((*areas)[1].ptr);
    *buf = right;
    (*areas)[1].ptr += (*areas)[1].step;
  }
}

static void audio_playback_write_callback(struct SoundIoOutStream *outstream,
                                          int frame_count_min,
                                          int frame_count_max) {
  int err;
  static uint32_t *buffer = NULL;
  static uint32_t unconsumed_frames = 0;

  uint32_t filled_frames = pool.count * pool.buffer_size;
  if (unconsumed_frames) {
    // we have leftover frames from the previous write.
    // replace one buffer_count with the unconsumed frames.
    filled_frames += unconsumed_frames;
    filled_frames -= pool.buffer_size;
  }

  struct SoundIoChannelArea *areas;
  // how many total frames we want to write in this callback
  int frames_left = filled_frames;

  if (frames_left < frame_count_min) {
    // not enough frames to write, lets just wait for more frames
    printf("audio underflow\n");
    return;
  }
  if (frames_left > frame_count_max) {
    // we have more frames than we can write in this callback, lets just
    // write as many as we can.
    frames_left = frame_count_max;
  }

  while (frames_left > 0) {
    // how many frames we can write in this iteration
    int frame_count = frames_left;

    if ((err =
             soundio_outstream_begin_write(outstream, &areas, &frame_count))) {
      panic("unrecoverable stream error when write: %s", soundio_strerror(err));
    }
    if (!frame_count)
      break;

    frames_left -= frame_count;

    if (buffer != NULL) {
      // printf("using leftover\n");
      // write as many as we can from the previous buffer.
      int frames_to_write = MIN(frame_count, unconsumed_frames);
      _write_frames_from_buffer(&areas, frames_to_write, buffer);
      frame_count -= frames_to_write;
      unconsumed_frames -= frames_to_write;
      if (unconsumed_frames == 0) {
        // we have consumed all frames from the previous buffer.
        audio_buffer_pool_commit_read(&pool);
        buffer = NULL;
      } else {
        // store partial buffer for next iteration
        buffer += frames_to_write;
      }
    }

    while (frame_count > 0) {
      // acquire a new buffer from the pool
      buffer = audio_buffer_pool_acquire_read(&pool, false);
      if (buffer == NULL) {
        panic("no audio buffer available, this should not happen");
      }

      // we have a new buffer, write as many frames as we can
      int frames_to_write = MIN(frame_count, pool.buffer_size);
      _write_frames_from_buffer(&areas, frames_to_write, buffer);
      frame_count -= frames_to_write;
      unconsumed_frames = pool.buffer_size - frames_to_write;
      if (unconsumed_frames == 0) {
        // we have consumed all frames from the new buffer.
        audio_buffer_pool_commit_read(&pool);
        buffer = NULL;
      } else {
        // store partial buffer for next iteration
        buffer += frames_to_write;
      }
    }

    if ((err = soundio_outstream_end_write(outstream))) {
      panic("unrecoverable stream error when end: %s", soundio_strerror(err));
    }
  }
}

static void
audio_playback_underflow_callback(struct SoundIoOutStream *outstream) {
  fprintf(stderr, "Audio underflow occurred\n");
}

// this will be handled by DMA + PIO on device.
static void audio_playback_main() {
  int err;
  struct SoundIo *soundio = soundio_create();
  if (!soundio) {
    fprintf(stderr, "Error creating SoundIo instance\n");
    return;
  }

  if ((err = soundio_connect(soundio))) {
    fprintf(stderr, "Unable to connect to backend: %s\n",
            soundio_strerror(err));
    return;
  }
  fprintf(stderr, "Backend: %s\n",
          soundio_backend_name(soundio->current_backend));
  soundio_flush_events(soundio);

  int device_index = soundio_default_output_device_index(soundio);
  struct SoundIoDevice *device =
      soundio_get_output_device(soundio, device_index);
  if (device->probe_error) {
    fprintf(stderr, "Cannot probe device: %s\n",
            soundio_strerror(device->probe_error));
    return;
  }
  printf("out %s\n", device->name);

  struct SoundIoOutStream *outstream = soundio_outstream_create(device);
  outstream->format = SoundIoFormatS16NE;
  outstream->sample_rate = AUDIO_SAMPLE_RATE;
  // outstream->layout = *soundio_channel_layout_get_default(1);

  outstream->name = PROJECT_NAME;
  outstream->write_callback = audio_playback_write_callback;
  outstream->underflow_callback = audio_playback_underflow_callback;
  outstream->software_latency = 0.0;

  if ((err = soundio_outstream_open(outstream))) {
    fprintf(stderr, "unable to open device: %s", soundio_strerror(err));
    return;
  }

  if ((err = soundio_outstream_start(outstream))) {
    fprintf(stderr, "unable to start device: %s", soundio_strerror(err));
    return;
  }

  while (true) {
    soundio_wait_events(soundio);
  }

  soundio_outstream_destroy(outstream);
  soundio_device_unref(device);
  soundio_destroy(soundio);
}

// this will be called on core1 on device.
void audio_init() {
  audio_buffer_pool_init(&pool, AUDIO_BUFFER_POOL_SIZE, AUDIO_BUFFER_SIZE);

  pthread_t audio_playback;
  pthread_create(&audio_playback, NULL, (void *)audio_playback_main, NULL);

  TimingInstrumenter ti_synth;

  audio_synth_t synth;
  audio_synth_init(&synth, AUDIO_SAMPLE_RATE, 1000);
  synth.master_level = q1x15_f(0.5f);

  audio_synth_operator_config_t config = audio_synth_operator_config_default;
  config.env = (audio_synth_env_config_t){
      .a = 0,
      .d = 700,
      .s = q1x31_f(0.f), // sustain level
      .r = 500,
  };
  config.freq_mult = 11;
  config.level = q1x15_f(0.3f);
  audio_synth_operator_set_config(&synth.voices[0].ops[0], config);

  config = audio_synth_operator_config_default;
  config.env = (audio_synth_env_config_t){
      .a = 0,
      .d = 1000,
      .s = q1x31_f(0.f), // sustain level
      .r = 600,
  };
  config.level = Q1X15_ONE;
  config.mode = AUDIO_SYNTH_OP_MODE_FREQ_MOD;
  audio_synth_operator_set_config(&synth.voices[0].ops[1], config);

  int i = 0;
  while (true) {
    if (i == 0) {
      audio_synth_handle_message(&synth,
                                 &(audio_synth_message_t){
                                     .type = AUDIO_SYNTH_MESSAGE_NOTE_ON,
                                     .data.note_on =
                                         {
                                             .voice = 0,
                                             .note_number = note("C4"),
                                             .velocity = 127,
                                         },
                                 });
    } else if (i == 10) {
      audio_synth_handle_message(&synth,
                                 &(audio_synth_message_t){
                                     .type = AUDIO_SYNTH_MESSAGE_NOTE_OFF,
                                     .data.note_off =
                                         {
                                             .voice = 0,
                                         },
                                 });
    } else if (i == 20) {
      audio_synth_handle_message(&synth,
                                 &(audio_synth_message_t){
                                     .type = AUDIO_SYNTH_MESSAGE_NOTE_ON,
                                     .data.note_on =
                                         {
                                             .voice = 0,
                                             .note_number = note("D4"),
                                             .velocity = 127,
                                         },
                                 });
    } else if (i == 30) {
      audio_synth_handle_message(&synth,
                                 &(audio_synth_message_t){
                                     .type = AUDIO_SYNTH_MESSAGE_NOTE_OFF,
                                     .data.note_off =
                                         {
                                             .voice = 0,
                                         },
                                 });
    } else if (i == 40) {
      audio_synth_handle_message(&synth,
                                 &(audio_synth_message_t){
                                     .type = AUDIO_SYNTH_MESSAGE_NOTE_ON,
                                     .data.note_on =
                                         {
                                             .voice = 0,
                                             .note_number = note("G4"),
                                             .velocity = 127,
                                         },
                                 });
    } else if (i == 80) {
      audio_synth_handle_message(&synth,
                                 &(audio_synth_message_t){
                                     .type = AUDIO_SYNTH_MESSAGE_NOTE_OFF,
                                     .data.note_off =
                                         {
                                             .voice = 0,
                                         },
                                 });
    } else if (i == 160) {
      i = -1;
    }

    audio_buffer_t buffer = audio_buffer_pool_acquire_write(&pool, true);
    ti_start(&ti_synth);
    audio_synth_fill_buffer(&synth, buffer, pool.buffer_size);
    ti_stop(&ti_synth);
    audio_buffer_pool_commit_write(&pool);

    i += 1;
    if (i % 1000 == 0) {
      printf("synth: %f ms\n", ti_get_average_ms(&ti_synth, true));
    }
  }

  pthread_join(audio_playback, NULL);
}
