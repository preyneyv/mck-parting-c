#include <pthread.h>
#include <soundio/soundio.h>

#include "audio.h"
#include "config.h"
#include "math.h"
#include <shared/utils/timing.h>

static audio_buffer_pool_t pool;

static void write_sample(char *ptr, double sample) {
  int16_t *buf = (int16_t *)ptr;
  double range = (double)INT16_MAX - (double)INT16_MIN;
  double val = sample * range / 2.0;
  *buf = val;
  printf("  write_sample: %.2f -> %d\n", sample, (int16_t)val);
}

static const double PI = 3.14159265358979323846264338328;
static double seconds_offset = 0.0;

static void do_sample(struct SoundIoOutStream *outstream, int frame_count_min,
                      int frame_count_max) {
  double float_sample_rate = outstream->sample_rate;
  double seconds_per_frame = 1.0 / float_sample_rate;
  struct SoundIoChannelArea *areas;
  int err;
  int frames_left = frame_count_max;
  for (;;) {
    int frame_count = frames_left;
    if ((err =
             soundio_outstream_begin_write(outstream, &areas, &frame_count))) {
      fprintf(stderr, "unrecoverable stream error when start: %s\n",
              soundio_strerror(err));
      exit(1);
    }
    if (!frame_count)
      break;
    const struct SoundIoChannelLayout *layout = &outstream->layout;
    double pitch = 440.0;
    double radians_per_second = pitch * 2.0 * PI;
    for (int frame = 0; frame < frame_count; frame += 1) {
      double sample = sin((seconds_offset + frame * seconds_per_frame) *
                          radians_per_second);
      printf("sample %d: %.2f\n", frame, sample);
      for (int channel = 0; channel < layout->channel_count; channel += 1) {
        write_sample(areas[channel].ptr, sample);
        areas[channel].ptr += areas[channel].step;
      }
    }
    seconds_offset =
        fmod(seconds_offset + seconds_per_frame * frame_count, 1.0);
    if ((err = soundio_outstream_end_write(outstream))) {
      if (err == SoundIoErrorUnderflow)
        return;
      fprintf(stderr, "unrecoverable stream error when end: %s\n",
              soundio_strerror(err));
      exit(1);
    }
    frames_left -= frame_count;
    if (frames_left <= 0)
      break;
  }
}

TimingInstrumenter ti_audio_playback;

static inline void _write_frames_from_buffer(struct SoundIoChannelArea *areas,
                                             int frame_count,
                                             uint32_t *buffer) {
  for (int index = 0; index < frame_count; index++) {
    uint32_t frame = buffer[index];
    int16_t left = ((frame >> 16) & 0xFFFF);
    int16_t right = (frame & 0xFFFF);

    int16_t *buf = (int16_t *)(areas[0].ptr);
    *buf = left;

    buf = (int16_t *)(areas[1].ptr);
    *buf = right;
    areas[0].ptr += areas[0].step;
    areas[1].ptr += areas[1].step;
  }
}

static void audio_playback_write_callback(struct SoundIoOutStream *outstream,
                                          int frame_count_min,
                                          int frame_count_max) {
  ti_start(&ti_audio_playback);
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
    printf("not enough frames to write, waiting for more\n");
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
    // printf("writing %d frames\n", frame_count);

    frames_left -= frame_count;

    printf("\npre: frames_left: %d, frame_count: %d\n", frames_left,
           frame_count);

    if (buffer != NULL) {
      // printf("using leftover\n");
      // write as many as we can from the previous buffer.
      int frames_to_write = MIN(frame_count, unconsumed_frames);
      _write_frames_from_buffer(areas, frames_to_write, buffer);
      frame_count -= frames_to_write;
      unconsumed_frames -= frames_to_write;
      if (unconsumed_frames == 0) {
        // we have consumed all frames from the previous buffer.
        audio_buffer_pool_commit_read(&pool);
        buffer = NULL;
      }
    }
    printf("lft: frames_left: %d, frame_count: %d\n", frames_left, frame_count);

    while (frame_count > 0) {
      // acquire a new buffer from the pool
      buffer = audio_buffer_pool_acquire_read(&pool, false);
      if (buffer == NULL) {
        panic("no audio buffer available, this should not happen");
      }

      // we have a new buffer, write as many frames as we can
      int frames_to_write = MIN(frame_count, pool.buffer_size);
      _write_frames_from_buffer(areas, frames_to_write, buffer);
      frame_count -= frames_to_write;
      unconsumed_frames = pool.buffer_size - frames_to_write;
      if (unconsumed_frames == 0) {
        // we have consumed all frames from the new buffer.
        audio_buffer_pool_commit_read(&pool);
        buffer = NULL;
      }
    }
    printf("end: frames_left: %d, frame_count: %d\n", frames_left, frame_count);

    if ((err = soundio_outstream_end_write(outstream))) {
      panic("unrecoverable stream error when end: %s", soundio_strerror(err));
    }
  }
  ti_stop(&ti_audio_playback);
}

static void
audio_playback_underflow_callback(struct SoundIoOutStream *outstream) {
  fprintf(stderr, "Audio underflow occurred\n");
}

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

  outstream->name = PROJECT_NAME;
  outstream->write_callback = do_sample;
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

  while (true)
    soundio_wait_events(soundio);

  soundio_outstream_destroy(outstream);
  soundio_device_unref(device);
  soundio_destroy(soundio);
}

void audio_init() {
  // audio_playback_main();
  // return;
  audio_buffer_pool_init(&pool, AUDIO_BUFFER_POOL_SIZE, AUDIO_BUFFER_SIZE);

  pthread_t audio_playback;
  pthread_create(&audio_playback, NULL, (void *)audio_playback_main, NULL);

  // start synth thread
  double float_sample_rate = AUDIO_SAMPLE_RATE;
  double seconds_per_frame = 1.0 / float_sample_rate;
  double pitch = 440.0;
  double rads_per_second = pitch * 2.0 * PI;

  TimingInstrumenter ti_synth;

  int i = 0;
  while (true) {
    uint32_t *buf = audio_buffer_pool_acquire_write(&pool, false);
    if (buf) {

      ti_start(&ti_synth);
      for (int frame = 0; frame < pool.buffer_size; frame++) {
        float sample = sinf((seconds_offset + frame * seconds_per_frame) *
                            rads_per_second);
        double value =
            (uint16_t)(sample * ((double)INT16_MAX - (double)INT16_MIN) / 2.0);
        uint16_t intval = (uint16_t)value;
        buf[frame] = ((uint32_t)intval << 16) | (intval);
        // printf("sample %d: %.2f %04x  %d  %08x\n", frame, sample,
        //        (uint16_t)value, value, (uint32_t)buf[frame]);
      }
      ti_stop(&ti_synth);
      audio_buffer_pool_commit_write(&pool);
      seconds_offset =
          fmod(seconds_offset + seconds_per_frame * pool.buffer_size, 1.0);
      i++;
      if (i == 100) {
        printf("synth: %f ms  | playback: %f ms\n",
               ti_get_average_ms(&ti_synth, true),
               ti_get_average_ms(&ti_audio_playback, true));
        i = 0;
      }
    } else {
      // printf("xd\n");
      sched_yield();
    }
  }

  pthread_join(audio_playback, NULL);
}
