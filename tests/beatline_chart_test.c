#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <prism/sdk.h>

#include "chart.h"

typedef struct
{
  uint8_t bytes[320];
  uint32_t size;
} test_track_t;

static test_track_t tracks[3];

static void make_track(test_track_t *track, const char *title,
                       uint64_t normal_id, uint64_t hard_id)
{
  memset(track, 0, sizeof(*track));
  beatline_file_header_t *header = (void *)track->bytes;
  *header = (beatline_file_header_t){
      .magic = BEATLINE_FILE_MAGIC,
      .format_version = BEATLINE_FILE_FORMAT_VERSION,
      .header_size = sizeof(*header),
      .scoring_ruleset = BEATLINE_SCORING_RULESET,
      .flags = normal_id != 0 ? BEATLINE_FILE_FLAG_RANKED : 0,
      .track_id = normal_id != 0 ? 100 : 0,
      .normal_chart_id = normal_id,
      .hard_chart_id = hard_id,
      .bpm_q8 = 120 * 256,
      .duration_ms = 1000,
      .duration_ticks = 960,
      .numerator = 4,
      .denominator = 4,
  };
  uint32_t cursor = sizeof(*header);
  header->title_offset = cursor;
  header->title_length = (uint16_t)strlen(title);
  memcpy(track->bytes + cursor, title, header->title_length + 1u);
  cursor += header->title_length + 1u;
  header->artist_offset = cursor;
  header->artist_length = 6;
  memcpy(track->bytes + cursor, "artist", 7);
  cursor += 7;
  cursor = (cursor + 3u) & ~3u;
  header->patches_offset = cursor;
  header->events_offset = cursor;
  header->normal_notes_offset = cursor;
  header->hard_notes_offset = cursor;
  header->file_size = cursor;
  track->size = cursor;
}

static uint32_t asset_pack_count(void) { return 2; }

static bool asset_pack_get(uint32_t index, prism_asset_pack_info_t *info)
{
  if (index >= 2 || info == NULL)
    return false;
  *info = (prism_asset_pack_info_t){
      .handle = index + 1u,
      .version = 1,
      .file_count = index == 0 ? 2 : 3,
      .id = index == 0 ? "dev.example.one" : "dev.example.two",
      .name = index == 0 ? "one" : "two",
  };
  return true;
}

static bool asset_file_get(prism_asset_pack_handle_t pack, uint32_t index,
                           prism_asset_file_t *file)
{
  static const uint8_t ignored[] = {1, 2, 3};
  if (file == NULL)
    return false;
  if (pack == 1 && index == 0)
  {
    *file = (prism_asset_file_t){
        .path = "readme.txt", .data = ignored, .size = sizeof(ignored)};
    return true;
  }
  if (pack == 1 && index == 1)
    index = 0;
  else if (pack == 2 && index == 0)
  {
    *file = (prism_asset_file_t){
        .path = "broken.beatline", .data = ignored, .size = sizeof(ignored)};
    return true;
  }
  else if (pack == 2 && index < 3)
    index -= 1;
  else
    return false;
  *file = (prism_asset_file_t){
      .path = index == 0 ? "tracks/a.beatline" : "tracks/b.beatline",
      .data = tracks[index].bytes,
      .size = tracks[index].size,
  };
  if (pack == 2)
  {
    file->path = index == 0 ? "tracks/b.beatline" : "tracks/c.beatline";
    file->data = tracks[index + 1u].bytes;
    file->size = tracks[index + 1u].size;
  }
  return true;
}

int main(void)
{
  make_track(&tracks[0], "a", 0, 0);
  make_track(&tracks[1], "b", 200, 201);
  make_track(&tracks[2], "c", 300, 301);
  static const prism_api_v1_t api = {
      .asset_pack_count = asset_pack_count,
      .asset_pack_get = asset_pack_get,
      .asset_file_get = asset_file_get,
  };
  prism_t prism = {.api = &api};

  assert(beatline_chart_count(&prism) == 3);
  beatline_chart_t a, b, c, cursor, opened;
  assert(beatline_chart_get(&prism, 0, &a) && strcmp(a.title, "a") == 0);
  assert(beatline_chart_get(&prism, 1, &b) && strcmp(b.title, "b") == 0);
  assert(beatline_chart_get(&prism, 2, &c) && strcmp(c.title, "c") == 0);
  assert(beatline_chart_next(&prism, &a, &cursor) &&
         strcmp(cursor.title, "b") == 0);
  assert(beatline_chart_next(&prism, &b, &cursor) &&
         strcmp(cursor.title, "c") == 0);
  assert(beatline_chart_next(&prism, &c, &cursor) &&
         strcmp(cursor.title, "a") == 0);
  assert(beatline_chart_previous(&prism, &a, &cursor) &&
         strcmp(cursor.title, "c") == 0);
  assert(beatline_chart_previous(&prism, &c, &cursor) &&
         strcmp(cursor.title, "b") == 0);
  assert(beatline_chart_open(&prism, &b, &opened));
  assert(beatline_chart_ranked_id(&opened, BEATLINE_DIFFICULTY_NORMAL) ==
         200);
  assert(beatline_chart_ranked_id(&opened, BEATLINE_DIFFICULTY_HARD) ==
         201);
  return 0;
}
