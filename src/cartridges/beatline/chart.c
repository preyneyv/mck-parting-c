#include "chart.h"

#include <limits.h>
#include <string.h>

static bool range_valid(uint32_t offset, uint32_t size, uint32_t limit)
{
    return offset <= limit && size <= limit - offset;
}

static bool array_valid(uint32_t offset, uint32_t count,
                        uint32_t item_size, uint32_t limit)
{
    return offset <= limit && item_size != 0 &&
           count <= (limit - offset) / item_size;
}

static bool path_is_beatline(const char *path)
{
    static const char suffix[] = ".beatline";
    if (path == NULL)
        return false;
    size_t length = strlen(path);
    return length >= sizeof(suffix) - 1u &&
           memcmp(path + length - (sizeof(suffix) - 1u), suffix,
                  sizeof(suffix) - 1u) == 0;
}

static bool text_valid(const uint8_t *data, uint32_t size, uint32_t offset,
                       uint16_t length, const char **text)
{
    if (length == 0 || !range_valid(offset, (uint32_t)length + 1u, size) ||
        data[offset + length] != '\0')
        return false;
    for (uint16_t i = 0; i < length; ++i)
        if (data[offset + i] == '\0')
            return false;
    *text = (const char *)data + offset;
    return true;
}

static bool notes_valid(const beatline_note_t *notes, uint32_t count)
{
    uint32_t last_tick = 0;
    uint8_t last_lane = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        const beatline_note_t *note = &notes[i];
        if (note->lane > BEATLINE_LANE_RIGHT ||
            note->type > BEATLINE_NOTE_HOLD ||
            (note->type == BEATLINE_NOTE_TAP && note->hold_duration != 0) ||
            note->hold_duration > UINT32_MAX - note->hit_tick ||
            (i > 0 && (note->hit_tick < last_tick ||
                       (note->hit_tick == last_tick &&
                        note->lane < last_lane))))
            return false;
        last_tick = note->hit_tick;
        last_lane = note->lane;
    }
    return true;
}

static bool chart_parse(const prism_asset_file_t *file, bool validate_content,
                        beatline_chart_t *chart)
{
    if (file == NULL || chart == NULL || file->data == NULL ||
        file->size < sizeof(beatline_file_header_t))
        return false;
    const uint8_t *data = file->data;
    const beatline_file_header_t *header = file->data;
    if (header->magic != BEATLINE_FILE_MAGIC ||
        header->format_version != BEATLINE_FILE_FORMAT_VERSION ||
        header->header_size != sizeof(*header) ||
        header->file_size != file->size ||
        header->scoring_ruleset != BEATLINE_SCORING_RULESET ||
        (header->flags & ~BEATLINE_FILE_FLAG_RANKED) != 0 ||
        header->bpm_q8 == 0 || header->duration_ms == 0 ||
        header->duration_ticks == 0 || header->numerator == 0 ||
        header->denominator == 0 ||
        header->patch_count > AUDIO_SYNTH_CARTRIDGE_PATCH_COUNT ||
        header->event_count > BEATLINE_FILE_MAX_EVENTS ||
        header->normal_note_count > BEATLINE_FILE_MAX_NOTES ||
        header->hard_note_count > BEATLINE_FILE_MAX_NOTES ||
        (header->patches_offset & 3u) != 0 ||
        (header->events_offset & 3u) != 0 ||
        (header->normal_notes_offset & 3u) != 0 ||
        (header->hard_notes_offset & 3u) != 0 ||
        !array_valid(header->patches_offset, header->patch_count,
                     sizeof(beatline_file_patch_t), file->size) ||
        !array_valid(header->events_offset, header->event_count,
                     sizeof(beatline_file_event_t), file->size) ||
        !array_valid(header->normal_notes_offset,
                     header->normal_note_count, sizeof(beatline_note_t),
                     file->size) ||
        !array_valid(header->hard_notes_offset,
                     header->hard_note_count, sizeof(beatline_note_t),
                     file->size))
        return false;

    bool ranked = (header->flags & BEATLINE_FILE_FLAG_RANKED) != 0;
    if (ranked != (header->track_id != 0 &&
                   header->normal_chart_id != 0 &&
                   header->hard_chart_id != 0))
        return false;

    const char *title;
    const char *artist;
    if (!text_valid(data, file->size, header->title_offset,
                    header->title_length, &title) ||
        !text_valid(data, file->size, header->artist_offset,
                    header->artist_length, &artist))
        return false;

    const beatline_file_patch_t *patches =
        (const void *)(data + header->patches_offset);
    const beatline_file_event_t *events =
        (const void *)(data + header->events_offset);
    const beatline_note_t *normal =
        (const void *)(data + header->normal_notes_offset);
    const beatline_note_t *hard =
        (const void *)(data + header->hard_notes_offset);
    if (validate_content)
    {
        uint32_t patch_mask = 0;
        for (uint32_t i = 0; i < header->patch_count; ++i)
        {
            if (patches[i].patch_idx >= AUDIO_SYNTH_CARTRIDGE_PATCH_COUNT ||
                (patch_mask & (1u << patches[i].patch_idx)) != 0)
                return false;
            patch_mask |= 1u << patches[i].patch_idx;
            for (uint8_t op = 0; op < AUDIO_SYNTH_OPERATOR_COUNT; ++op)
                if (patches[i].ops[op].mode > AUDIO_SYNTH_OP_MODE_FREQ_MOD)
                    return false;
        }

        uint32_t last_ms = 0;
        for (uint32_t i = 0; i < header->event_count; ++i)
        {
            if (events[i].type > BEATLINE_FILE_EVENT_NOTE_OFF ||
                events[i].patch_idx >= AUDIO_SYNTH_CARTRIDGE_PATCH_COUNT ||
                (patch_mask & (1u << events[i].patch_idx)) == 0 ||
                events[i].note > 127 || events[i].time_ms < last_ms)
                return false;
            last_ms = events[i].time_ms;
        }
        if (!notes_valid(normal, header->normal_note_count) ||
            !notes_valid(hard, header->hard_note_count))
            return false;
    }

    *chart = (beatline_chart_t){
        .title = title,
        .display_info = artist,
        .header = header,
        .patches = patches,
        .events = events,
        .normal_notes = normal,
        .hard_notes = hard,
    };
    return true;
}

static bool chart_file_at(prism_t *prism, uint32_t pack_index,
                          uint32_t file_index, bool validate_content,
                          beatline_chart_t *chart)
{
    prism_asset_pack_info_t pack;
    prism_asset_file_t file;
    beatline_chart_t candidate;
    if (!prism_asset_pack_get(prism, pack_index, &pack) ||
        file_index >= pack.file_count ||
        !prism_asset_file_get(prism, pack.handle, file_index, &file) ||
        !path_is_beatline(file.path) ||
        !chart_parse(&file, validate_content, &candidate))
        return false;
    candidate.pack_index = pack_index;
    candidate.file_index = file_index;
    if (chart != NULL)
        *chart = candidate;
    return true;
}

static bool chart_at(prism_t *prism, uint32_t wanted,
                     beatline_chart_t *chart)
{
    uint32_t pack_count = prism_asset_pack_count(prism);
    for (uint32_t pack_index = 0; pack_index < pack_count; ++pack_index)
    {
        prism_asset_pack_info_t pack;
        if (!prism_asset_pack_get(prism, pack_index, &pack))
            continue;
        for (uint32_t file_index = 0; file_index < pack.file_count;
             ++file_index)
        {
            beatline_chart_t candidate;
            if (!chart_file_at(prism, pack_index, file_index, false,
                               &candidate))
                continue;
            if (wanted-- == 0)
            {
                if (chart != NULL)
                    *chart = candidate;
                return true;
            }
        }
    }
    return false;
}

uint32_t beatline_chart_count(prism_t *prism)
{
    uint32_t count = 0;
    uint32_t pack_count = prism_asset_pack_count(prism);
    for (uint32_t pack_index = 0; pack_index < pack_count; ++pack_index)
    {
        prism_asset_pack_info_t pack;
        if (!prism_asset_pack_get(prism, pack_index, &pack))
            continue;
        for (uint32_t file_index = 0; file_index < pack.file_count;
             ++file_index)
        {
            beatline_chart_t chart;
            if (chart_file_at(prism, pack_index, file_index, false, &chart) &&
                count != UINT32_MAX)
                ++count;
        }
    }
    return count;
}

bool beatline_chart_get(prism_t *prism, uint32_t index,
                        beatline_chart_t *chart)
{
    return chart_at(prism, index, chart);
}

static bool chart_forward_range(prism_t *prism, uint32_t pack_index,
                                uint32_t first, uint32_t last,
                                beatline_chart_t *chart)
{
    prism_asset_pack_info_t pack;
    if (!prism_asset_pack_get(prism, pack_index, &pack))
        return false;
    if (last > pack.file_count)
        last = pack.file_count;
    for (uint32_t file_index = first; file_index < last; ++file_index)
        if (chart_file_at(prism, pack_index, file_index, false, chart))
            return true;
    return false;
}

static bool chart_reverse_range(prism_t *prism, uint32_t pack_index,
                                uint32_t first, uint32_t last,
                                beatline_chart_t *chart)
{
    prism_asset_pack_info_t pack;
    if (!prism_asset_pack_get(prism, pack_index, &pack))
        return false;
    if (last > pack.file_count)
        last = pack.file_count;
    while (last > first)
    {
        --last;
        if (chart_file_at(prism, pack_index, last, false, chart))
            return true;
    }
    return false;
}

bool beatline_chart_next(prism_t *prism, const beatline_chart_t *current,
                         beatline_chart_t *chart)
{
    if (prism == NULL || current == NULL || chart == NULL)
        return false;
    uint32_t pack_count = prism_asset_pack_count(prism);
    if (current->pack_index >= pack_count)
        return false;
    if (chart_forward_range(prism, current->pack_index,
                            current->file_index + 1u, UINT32_MAX, chart))
        return true;
    for (uint32_t pack = current->pack_index + 1u; pack < pack_count; ++pack)
        if (chart_forward_range(prism, pack, 0, UINT32_MAX, chart))
            return true;
    for (uint32_t pack = 0; pack < current->pack_index; ++pack)
        if (chart_forward_range(prism, pack, 0, UINT32_MAX, chart))
            return true;
    return chart_forward_range(prism, current->pack_index, 0,
                               current->file_index + 1u, chart);
}

bool beatline_chart_previous(prism_t *prism,
                             const beatline_chart_t *current,
                             beatline_chart_t *chart)
{
    if (prism == NULL || current == NULL || chart == NULL)
        return false;
    uint32_t pack_count = prism_asset_pack_count(prism);
    if (current->pack_index >= pack_count)
        return false;
    if (chart_reverse_range(prism, current->pack_index, 0,
                            current->file_index, chart))
        return true;
    for (uint32_t pack = current->pack_index; pack > 0;)
    {
        --pack;
        if (chart_reverse_range(prism, pack, 0, UINT32_MAX, chart))
            return true;
    }
    for (uint32_t pack = pack_count; pack > current->pack_index + 1u;)
    {
        --pack;
        if (chart_reverse_range(prism, pack, 0, UINT32_MAX, chart))
            return true;
    }
    return chart_reverse_range(prism, current->pack_index,
                               current->file_index + 1u, UINT32_MAX, chart);
}

bool beatline_chart_open(prism_t *prism, const beatline_chart_t *reference,
                         beatline_chart_t *chart)
{
    return prism != NULL && reference != NULL && chart != NULL &&
           chart_file_at(prism, reference->pack_index,
                         reference->file_index, true, chart);
}

const beatline_note_t *beatline_chart_notes(
    const beatline_chart_t *chart, beatline_difficulty_t difficulty,
    uint16_t *count)
{
    if (chart == NULL || chart->header == NULL || count == NULL)
        return NULL;
    if (difficulty == BEATLINE_DIFFICULTY_HARD)
    {
        *count = (uint16_t)chart->header->hard_note_count;
        return chart->hard_notes;
    }
    *count = (uint16_t)chart->header->normal_note_count;
    return chart->normal_notes;
}

uint64_t beatline_chart_ranked_id(
    const beatline_chart_t *chart, beatline_difficulty_t difficulty)
{
    if (chart == NULL || chart->header == NULL ||
        (chart->header->flags & BEATLINE_FILE_FLAG_RANKED) == 0)
        return 0;
    uint64_t chart_id;
    const void *source = difficulty == BEATLINE_DIFFICULTY_HARD
                             ? (const void *)&chart->header->hard_chart_id
                             : (const void *)&chart->header->normal_chart_id;
    memcpy(&chart_id, source, sizeof(chart_id));
    return chart_id;
}
