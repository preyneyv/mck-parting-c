#include <pthread.h>
#include <soundio/soundio.h>

#include "audio.h"
#include "math.h"

static audio_buffer_pool_t pool;

static void write_sample(char *ptr, double sample) {
  int16_t *buf = (int16_t *)ptr;
  double range = (double)INT16_MAX - (double)INT16_MIN;
  double val = sample * range / 2.0;
  *buf = val;
}

static const double PI = 3.14159265358979323846264338328;
static double seconds_offset = 0.0;
static void audio_playback_write_callback(struct SoundIoOutStream *outstream,
                                          int frame_count_min,
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
      fprintf(stderr, "unrecoverable stream error: %s\n",
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
      fprintf(stderr, "unrecoverable stream error: %s\n",
              soundio_strerror(err));
      exit(1);
    }
    frames_left -= frame_count;
    if (frames_left <= 0)
      break;
  }
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
  outstream->write_callback = audio_playback_write_callback;
  outstream->format = SoundIoFormatS16NE;
  outstream->sample_rate = AUDIO_SAMPLE_RATE;
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
    soundio_flush_events(soundio);

  soundio_outstream_destroy(outstream);
  soundio_device_unref(device);
  soundio_destroy(soundio);
}

void audio_init() {
  audio_buffer_pool_init(&pool, AUDIO_BUFFER_POOL_SIZE, AUDIO_BUFFER_SIZE);

  pthread_t audio_playback;
  pthread_create(&audio_playback, NULL, (void *)audio_playback_main, NULL);

  pthread_join(audio_playback, NULL);
}
