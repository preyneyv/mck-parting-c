#include "system_sound.h"

#include <string.h>

#include <shared/utils/q1x15.h>
#include <shared/utils/q1x31.h>

enum
{
  SYSTEM_PATCH_NAVIGATION = AUDIO_SYNTH_SYSTEM_PATCH_FIRST,
  SYSTEM_PATCH_HOLD,
  SYSTEM_PATCH_MENU,
  SYSTEM_PATCH_WAKE,
  SYSTEM_SOUND_PENDING_COUNT = 16,
  SYSTEM_SOUND_PENDING_PLAY_COUNT = 8,
};

_Static_assert(SYSTEM_PATCH_WAKE < AUDIO_SYNTH_PATCH_COUNT,
               "system sounds must use reserved synth patches");

static audio_synth_t *system_synth;

typedef struct
{
  bool active;
  uint8_t patch;
  uint8_t note;
  platform_time_t release_at;
} pending_release_t;

static pending_release_t pending_releases[SYSTEM_SOUND_PENDING_COUNT];

typedef struct
{
  bool active;
  uint8_t patch;
  uint8_t note;
  uint8_t velocity;
  uint16_t duration_ms;
  platform_time_t play_at;
} pending_play_t;

static pending_play_t pending_plays[SYSTEM_SOUND_PENDING_PLAY_COUNT];

static audio_synth_patch_config_t pluck_patch(uint32_t decay_ms,
                                               float carrier_level,
                                               float mod_level,
                                               int mod_multiplier)
{
  audio_synth_patch_config_t patch = audio_synth_patch_config_default;
  patch.ops[0].freq_mult = mod_multiplier;
  patch.ops[0].level = q1x15_f(mod_level);
  patch.ops[0].env = (audio_synth_env_config_t){
      .a = 2,
      .d = decay_ms / 2,
      .s = q1x31_f(.02f),
      .r = 12,
  };
  patch.ops[1].freq_mult = 1;
  patch.ops[1].level = q1x15_f(carrier_level);
  patch.ops[1].mode = AUDIO_SYNTH_OP_MODE_FREQ_MOD;
  patch.ops[1].env = (audio_synth_env_config_t){
      .a = 2,
      .d = decay_ms,
      .s = q1x31_f(.04f),
      .r = 20,
  };
  return patch;
}

static void release(uint8_t patch, uint8_t note)
{
  if (system_synth == NULL)
    return;
  audio_synth_enqueue(
      system_synth,
      &(audio_synth_message_t){
          .type = AUDIO_SYNTH_MESSAGE_NOTE_OFF,
          .data.note_off = {
              .patch_idx = patch,
              .note_number = (int8_t)note,
          },
      });
}

static void schedule_release(uint8_t patch, uint8_t note,
                             uint16_t duration_ms)
{
  pending_release_t *slot = NULL;
  for (uint8_t i = 0; i < SYSTEM_SOUND_PENDING_COUNT; ++i)
  {
    pending_release_t *candidate = &pending_releases[i];
    if (candidate->active && candidate->patch == patch &&
        candidate->note == note)
    {
      slot = candidate;
      break;
    }
    if (!candidate->active && slot == NULL)
      slot = candidate;
  }
  if (slot == NULL)
  {
    slot = &pending_releases[0];
    release(slot->patch, slot->note);
  }
  slot->active = true;
  slot->patch = patch;
  slot->note = note;
  slot->release_at = platform_time_add_ms(platform_now_us(), duration_ms);
}

static void play(uint8_t patch, uint8_t note, uint8_t velocity,
                 uint16_t duration_ms)
{
  if (system_synth == NULL)
    return;
  audio_synth_enqueue(
      system_synth,
      &(audio_synth_message_t){
          .type = AUDIO_SYNTH_MESSAGE_NOTE_ON,
          .data.note_on = {
              .patch_idx = patch,
              .note_number = note,
              .velocity = velocity,
          },
      });
  schedule_release(patch, note, duration_ms);
}

static void play_delayed(uint8_t patch, uint8_t note, uint8_t velocity,
                         uint16_t duration_ms, uint16_t delay_ms)
{
  for (uint8_t i = 0; i < SYSTEM_SOUND_PENDING_PLAY_COUNT; ++i)
  {
    pending_play_t *pending = &pending_plays[i];
    if (pending->active)
      continue;
    *pending = (pending_play_t){
        .active = true,
        .patch = patch,
        .note = note,
        .velocity = velocity,
        .duration_ms = duration_ms,
        .play_at = platform_time_add_ms(platform_now_us(), delay_ms),
    };
    return;
  }
  play(patch, note, velocity, duration_ms);
}

void system_sound_init(audio_synth_t *synth)
{
  system_synth = synth;
  memset(pending_releases, 0, sizeof(pending_releases));
  memset(pending_plays, 0, sizeof(pending_plays));
  if (synth == NULL)
    return;
  audio_synth_patch_config_set(
      synth, SYSTEM_PATCH_NAVIGATION, pluck_patch(45, .576f, .035f, 3));
  audio_synth_patch_config_set(
      synth, SYSTEM_PATCH_HOLD, pluck_patch(70, .40f, .065f, 3));
  audio_synth_patch_config_set(
      synth, SYSTEM_PATCH_MENU, pluck_patch(110, .72f, .035f, 2));
  audio_synth_patch_config_set(
      synth, SYSTEM_PATCH_WAKE, pluck_patch(55, .68f, .025f, 2));
}

void system_sound_tick(platform_time_t now)
{
  for (uint8_t i = 0; i < SYSTEM_SOUND_PENDING_COUNT; ++i)
  {
    pending_release_t *pending = &pending_releases[i];
    if (!pending->active || now < pending->release_at)
      continue;
    release(pending->patch, pending->note);
    pending->active = false;
  }
  for (uint8_t i = 0; i < SYSTEM_SOUND_PENDING_PLAY_COUNT; ++i)
  {
    pending_play_t *pending = &pending_plays[i];
    if (!pending->active || now < pending->play_at)
      continue;
    play(pending->patch, pending->note, pending->velocity,
         pending->duration_ms);
    pending->active = false;
  }
}

void system_sound_navigation(void)
{
  play(SYSTEM_PATCH_NAVIGATION, 78, 104, 45);
}

void system_sound_hold(uint8_t step)
{
  static const uint8_t notes[] = {72, 73, 74, 75, 76};
  if (step == 0 || step > sizeof(notes))
    return;
  play(SYSTEM_PATCH_HOLD, notes[step - 1u], 96, 70);
}

void system_sound_menu(bool opening)
{
  play(SYSTEM_PATCH_MENU, opening ? 69u : 66u, 100, 110);
}

void system_sound_wake(uint8_t step)
{
  static const uint8_t notes[] = {74, 78, 81};
  if (step == 0 || step > sizeof(notes))
    step = 1;
  for (uint8_t i = 0; i < step; ++i)
  {
    uint8_t velocity = i + 1u == step ? 100u : 68u;
    if (i == 0)
      play(SYSTEM_PATCH_WAKE, notes[i], velocity, 55);
    else
      play_delayed(SYSTEM_PATCH_WAKE, notes[i], velocity, 55,
                   (uint16_t)(i * 24u));
  }
}
