#include "song.h"
#include "synth_internal.h"

#include <assert.h>
#include <string.h>

static uint32_t song_loop_end_ms(const audio_song_asset_t *song)
{
  if (song->loop_end_ms != 0)
    return song->loop_end_ms;
  return song->duration_ms;
}

static inline uint8_t song_map_patch_idx(const audio_song_player_t *player,
                                         uint8_t patch_idx)
{
  uint8_t mapped = (uint8_t)(player->patch_base + patch_idx);
  assert(mapped < AUDIO_SYNTH_PATCH_COUNT);
  return mapped;
}

static void song_apply_patches(audio_song_player_t *player,
                               const audio_song_asset_t *song)
{
  if (player->synth == NULL || song == NULL || song->patches == NULL)
    return;

  for (uint32_t i = 0; i < song->patch_count; i++)
  {
    audio_song_patch_event_t patch = song->patches[i];
    audio_synth_patch_config_set(
        player->synth,
        song_map_patch_idx(player, patch.patch_idx),
        patch.patch);
  }
}

static void song_stop_all_patches(audio_song_player_t *player)
{
  if (player == NULL || player->synth == NULL || player->song == NULL ||
      player->song->patches == NULL)
    return;

  bool seen[AUDIO_SYNTH_PATCH_COUNT] = {false};
  for (uint32_t i = 0; i < player->song->patch_count; i++)
  {
    uint8_t mapped_patch_idx =
        song_map_patch_idx(player, player->song->patches[i].patch_idx);
    if (seen[mapped_patch_idx])
      continue;

    seen[mapped_patch_idx] = true;
    audio_synth_enqueue(player->synth,
                        &(audio_synth_message_t){
                            .type = AUDIO_SYNTH_MESSAGE_STOP,
                            .data.stop =
                                {
                                    .patch_idx = mapped_patch_idx,
                                    .note_number = -1,
                                }});
  }
}

static bool song_event_is_due(const audio_song_event_t *event,
                              uint32_t from_ms,
                              uint32_t to_ms)
{
  return event->time_ms >= from_ms && event->time_ms <= to_ms;
}

static bool song_dispatch_to_synth(audio_song_player_t *player,
                                   const audio_song_event_t *event)
{
  if (player->synth == NULL || event == NULL)
    return true;

  switch (event->type)
  {
  case AUDIO_SONG_EVENT_NOTE_ON:
  {
    uint8_t patch_idx =
        song_map_patch_idx(player, event->data.note_on.patch_idx);
    return audio_synth_enqueue(
        player->synth,
        &(audio_synth_message_t){
            .type = AUDIO_SYNTH_MESSAGE_NOTE_ON,
            .data.note_on =
                {
                    .patch_idx = patch_idx,
                    .note_number = event->data.note_on.note_number,
                    .velocity = event->data.note_on.velocity,
                }});
  }
  case AUDIO_SONG_EVENT_NOTE_OFF:
  {
    uint8_t patch_idx =
        song_map_patch_idx(player, event->data.note_off.patch_idx);
    return audio_synth_enqueue(
        player->synth,
        &(audio_synth_message_t){
            .type = AUDIO_SYNTH_MESSAGE_NOTE_OFF,
            .data.note_off =
                {
                    .patch_idx = patch_idx,
                    .note_number = event->data.note_off.note_number,
                }});
  }
  case AUDIO_SONG_EVENT_PATCH:
  {
    uint8_t patch_idx =
        song_map_patch_idx(player, event->data.patch.patch_idx);
    audio_synth_patch_config_set(player->synth,
                                 patch_idx,
                                 event->data.patch.patch);
    return true;
  }
  case AUDIO_SONG_EVENT_MARKER:
  case AUDIO_SONG_EVENT_TEMPO:
  case AUDIO_SONG_EVENT_TIMESIG:
    return true;
  }

  return true;
}

static bool song_dispatch_event(audio_song_player_t *player,
                                const audio_song_event_t *event)
{
  if (!player->pending_event_hooked)
  {
    player->pending_event_action = AUDIO_SONG_EVENT_ACTION_PASS;
    if (player->hook.callback != NULL)
    {
      uint32_t type_mask = 1u << event->type;
      if (player->hook.mask & type_mask)
      {
        player->pending_event_action =
            player->hook.callback(player, event, player->hook.user);
      }
    }
    player->pending_event_hooked = true;
  }

  bool accepted =
      player->pending_event_action == AUDIO_SONG_EVENT_ACTION_CONSUME ||
      song_dispatch_to_synth(player, event);
  if (accepted)
    player->pending_event_hooked = false;
  return accepted;
}

static void song_seek_to_time(audio_song_player_t *player, uint32_t time_ms)
{
  player->song_time_ms = time_ms;
  player->next_event_idx = 0;
  player->pending_event_hooked = false;

  const audio_song_asset_t *song = player->song;
  if (song == NULL || song->events == NULL)
    return;

  while (player->next_event_idx < song->event_count)
  {
    if (song->events[player->next_event_idx].time_ms >= time_ms)
      break;
    player->next_event_idx++;
  }
}

void audio_song_player_init(audio_song_player_t *player, audio_synth_t *synth)
{
  memset(player, 0, sizeof(*player));
  player->synth = synth;
  player->hook.mask = AUDIO_SONG_EVENT_MASK_ALL;
  player->patch_base = 0;
}

void audio_song_player_set_hook(audio_song_player_t *player,
                                audio_song_event_hook_desc_t hook)
{
  player->hook = hook;
}

void audio_song_player_play(audio_song_player_t *player,
                            const audio_song_asset_t *song,
                            audio_song_play_options_t options,
                            uint32_t now_ms)
{
  if (song == NULL || song->events == NULL)
  {
    audio_song_player_stop(player, false);
    return;
  }

  if (player->playing && !options.restart_if_playing && player->song == song)
  {
    return;
  }

  // Silence the previous song synchronously. A queued PANIC would also
  // discard the new song's time-zero events queued behind it.
  if (player->song != NULL)
  {
    audio_song_player_stop(player, false);
    if (player->synth != NULL)
      audio_synth_panic_sync(player->synth);
  }

  player->song = song;
  player->playing = true;
  player->paused = false;
  player->loop = options.loop;
  player->patch_base = options.patch_base;
  player->last_engine_ms = now_ms;

  song_apply_patches(player, song);
  song_seek_to_time(player, 0);

  // Dispatch time-zero events now. Waiting for the first tick() would let the
  // audio core render a buffer first and drop the song's opening notes.
  while (player->next_event_idx < song->event_count &&
         song->events[player->next_event_idx].time_ms == 0)
  {
    if (!song_dispatch_event(player,
                             &song->events[player->next_event_idx]))
      break;
    player->next_event_idx++;
  }
}

void audio_song_player_stop(audio_song_player_t *player, bool panic)
{
  song_stop_all_patches(player);

  if (panic && player->synth != NULL)
  {
    audio_synth_enqueue(player->synth,
                        &(audio_synth_message_t){
                            .type = AUDIO_SYNTH_MESSAGE_PANIC,
                            .data.panic = {}});
  }

  player->playing = false;
  player->paused = false;
  player->song = NULL;
  player->song_time_ms = 0;
  player->last_engine_ms = 0;
  player->next_event_idx = 0;
  player->pending_event_hooked = false;
}

void audio_song_player_pause(audio_song_player_t *player)
{
  song_stop_all_patches(player);
  player->paused = true;
}

void audio_song_player_resume(audio_song_player_t *player, uint32_t now_ms)
{
  if (!player->playing)
    return;
  player->paused = false;
  player->last_engine_ms = now_ms;
}

void audio_song_player_seek(audio_song_player_t *player,
                            uint32_t time_ms,
                            uint32_t now_ms)
{
  if (player == NULL || !player->playing || player->song == NULL)
    return;

  if (time_ms > player->song->duration_ms)
    time_ms = player->song->duration_ms;

  // Seeking skips NOTE_OFF events, so release everything that is sounding
  // (including time-zero notes when a caller plays and then seeks).
  song_stop_all_patches(player);
  song_seek_to_time(player, time_ms);
  player->last_engine_ms = now_ms;
}

void audio_song_player_tick(audio_song_player_t *player, uint32_t now_ms)
{
  if (!player->playing || player->paused || player->song == NULL)
    return;

  if (now_ms < player->last_engine_ms)
  {
    player->last_engine_ms = now_ms;
    return;
  }

  uint32_t dt_ms = now_ms - player->last_engine_ms;
  if (dt_ms == 0)
    return;

  player->last_engine_ms = now_ms;

  const audio_song_asset_t *song = player->song;
  uint32_t from_ms = player->song_time_ms;
  uint32_t to_ms = from_ms + dt_ms;

  uint32_t loop_start = song->loop_start_ms;
  uint32_t loop_end = song_loop_end_ms(song);
  bool do_loop = player->loop && loop_end > loop_start;

  while (true)
  {
    uint32_t section_end = to_ms;
    bool wrapped = false;

    if (do_loop && to_ms >= loop_end)
    {
      section_end = loop_end;
      wrapped = true;
    }

    while (player->next_event_idx < song->event_count)
    {
      const audio_song_event_t *event = &song->events[player->next_event_idx];
      if (!song_event_is_due(event, from_ms, section_end))
      {
        if (event->time_ms > section_end)
          break;
        player->next_event_idx++;
        continue;
      }

      if (!song_dispatch_event(player, event))
      {
        player->song_time_ms = event->time_ms;
        return;
      }
      player->next_event_idx++;
    }

    if (!wrapped)
      break;

    uint32_t overflow = to_ms - loop_end;
    song_seek_to_time(player, loop_start);
    from_ms = loop_start;
    to_ms = loop_start + overflow;
  }

  player->song_time_ms = to_ms;

  if (!do_loop && player->song_time_ms >= song->duration_ms)
  {
    player->song_time_ms = song->duration_ms;
    player->playing = false;
  }
}
