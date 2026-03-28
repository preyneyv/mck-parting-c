#include "song.h"

#include <string.h>

static uint32_t song_loop_end_ms(const audio_song_asset_t *song)
{
  if (song->loop_end_ms != 0)
    return song->loop_end_ms;
  return song->duration_ms;
}

static void song_apply_patches(audio_song_player_t *player,
                               const audio_song_asset_t *song)
{
  if (player->synth == NULL || song == NULL || song->patches == NULL)
    return;

  for (uint32_t i = 0; i < song->patch_count; i++)
  {
    audio_song_patch_event_t patch = song->patches[i];
    audio_synth_patch_config_set(player->synth, patch.patch_idx, patch.patch);
  }
}

static bool song_event_is_due(const audio_song_event_t *event,
                              uint32_t from_ms,
                              uint32_t to_ms)
{
  return event->time_ms > from_ms && event->time_ms <= to_ms;
}

static void song_dispatch_to_synth(audio_song_player_t *player,
                                   const audio_song_event_t *event)
{
  if (player->synth == NULL || event == NULL)
    return;

  switch (event->type)
  {
  case AUDIO_SONG_EVENT_NOTE_ON:
    audio_synth_enqueue(
        player->synth,
        &(audio_synth_message_t){
            .type = AUDIO_SYNTH_MESSAGE_NOTE_ON,
            .data.note_on =
                {
                    .patch_idx = event->data.note_on.patch_idx,
                    .note_number = event->data.note_on.note_number,
                    .velocity = event->data.note_on.velocity,
                }});
    break;
  case AUDIO_SONG_EVENT_NOTE_OFF:
    audio_synth_enqueue(
        player->synth,
        &(audio_synth_message_t){
            .type = AUDIO_SYNTH_MESSAGE_NOTE_OFF,
            .data.note_off =
                {
                    .patch_idx = event->data.note_off.patch_idx,
                    .note_number = event->data.note_off.note_number,
                }});
    break;
  case AUDIO_SONG_EVENT_PATCH:
    audio_synth_patch_config_set(player->synth,
                                 event->data.patch.patch_idx,
                                 event->data.patch.patch);
    break;
  case AUDIO_SONG_EVENT_MARKER:
  case AUDIO_SONG_EVENT_TEMPO:
  case AUDIO_SONG_EVENT_TIMESIG:
    break;
  }
}

static void song_dispatch_event(audio_song_player_t *player,
                                const audio_song_event_t *event)
{
  audio_song_event_action_t action = AUDIO_SONG_EVENT_ACTION_PASS;
  if (player->hook.callback != NULL)
  {
    uint32_t type_mask = 1u << event->type;
    if (player->hook.mask & type_mask)
    {
      action = player->hook.callback(player, event, player->hook.user);
    }
  }

  if (action == AUDIO_SONG_EVENT_ACTION_PASS)
  {
    song_dispatch_to_synth(player, event);
  }
}

static void song_seek_to_time(audio_song_player_t *player, uint32_t time_ms)
{
  player->song_time_ms = time_ms;
  player->next_event_idx = 0;

  const audio_song_asset_t *song = player->song;
  if (song == NULL || song->events == NULL)
    return;

  while (player->next_event_idx < song->event_count)
  {
    if (song->events[player->next_event_idx].time_ms > time_ms)
      break;
    player->next_event_idx++;
  }
}

void audio_song_player_init(audio_song_player_t *player, audio_synth_t *synth)
{
  memset(player, 0, sizeof(*player));
  player->synth = synth;
  player->hook.mask = AUDIO_SONG_EVENT_MASK_ALL;
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

  audio_song_player_stop(player, true);

  player->song = song;
  player->playing = true;
  player->paused = false;
  player->loop = options.loop;
  player->last_engine_ms = now_ms;

  song_apply_patches(player, song);
  song_seek_to_time(player, 0);
}

void audio_song_player_stop(audio_song_player_t *player, bool panic)
{
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
}

void audio_song_player_pause(audio_song_player_t *player)
{
  player->paused = true;
}

void audio_song_player_resume(audio_song_player_t *player, uint32_t now_ms)
{
  if (!player->playing)
    return;
  player->paused = false;
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

      song_dispatch_event(player, event);
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

size_t audio_song_player_peek_events(const audio_song_player_t *player,
                                     uint32_t lookahead_ms,
                                     uint32_t mask,
                                     const audio_song_event_t **out_events,
                                     size_t max_events)
{
  if (player == NULL || player->song == NULL || out_events == NULL || max_events == 0)
    return 0;

  const audio_song_asset_t *song = player->song;
  uint32_t horizon = player->song_time_ms + lookahead_ms;
  size_t out_count = 0;

  for (uint32_t i = player->next_event_idx; i < song->event_count; i++)
  {
    const audio_song_event_t *event = &song->events[i];
    if (event->time_ms > horizon)
      break;

    if (event->time_ms <= player->song_time_ms)
      continue;

    uint32_t type_mask = 1u << event->type;
    if ((mask & type_mask) == 0)
      continue;

    out_events[out_count++] = event;
    if (out_count >= max_events)
      break;
  }

  return out_count;
}
