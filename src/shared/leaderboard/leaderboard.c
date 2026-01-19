#include <pico/unique_id.h>
#include <pico/rand.h>

#include <qrcodegen.h>
#include <stdio.h>

#include "leaderboard.h"
#include "encoding.h"

// total header bytes: 1 (app_id) + 8 (board_id) + 4 (entry_id) = 13 bytes
// plus data for app
// plus checksum
typedef struct
{
    uint8_t app_id;
    pico_unique_board_id_t board_id;
    uint32_t entry_id;
    uint8_t *data;
    size_t data_len;
} leaderboard_entry_t;

static uint8_t buffer[qrcodegen_BUFFER_LEN_FOR_VERSION(LEADERBOARD_QR_VERSION)];
static uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(LEADERBOARD_QR_VERSION)];
static char url_buffer[115]; // max len for v4-L qr code in alphanum

static void _pack_leaderboard_entry(leaderboard_entry_t *entry, uint8_t *out_buf, size_t *out_len)
{
    size_t offset = 0;

    // app id [0:1] (1 byte)
    out_buf[offset++] = entry->app_id;

    // board id [1:9] (8 bytes)
    for (size_t i = 0; i < sizeof(pico_unique_board_id_t); i++)
    {
        out_buf[offset++] = entry->board_id.id[i];
    }

    // entry id [9:13] (4 bytes)
    out_buf[offset++] = (entry->entry_id >> 24) & 0xFF;
    out_buf[offset++] = (entry->entry_id >> 16) & 0xFF;
    out_buf[offset++] = (entry->entry_id >> 8) & 0xFF;
    out_buf[offset++] = (entry->entry_id) & 0xFF;

    // data [13:13+data_len]
    for (size_t i = 0; i < entry->data_len; i++)
    {
        out_buf[offset++] = entry->data[i];
    }
    *out_len = offset;

    // checksum [13+data_len:15+data_len] (2 bytes)
    uint16_t checksum = fletcher16(out_buf, offset);
    out_buf[offset++] = (checksum >> 8) & 0xFF;
    out_buf[offset++] = (checksum) & 0xFF;
}

uint8_t *leaderboard_get_qrcode(uint8_t app_id, void *data, size_t data_len)
{
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);

    leaderboard_entry_t entry = {
        .app_id = app_id,
        .board_id = board_id,
        .entry_id = get_rand_32(), // simple random entry ID
        .data = data,
        .data_len = data_len,
    };
    printf("Generating leaderboard QR code for app %u, entry ID %u\n", app_id, entry.entry_id);

    size_t packed_len = 0;
    _pack_leaderboard_entry(&entry, buffer, &packed_len);
    bytes_to_base36(buffer, packed_len, temp, sizeof(temp));
    printf("b36 %s\n", temp);

    snprintf(
        url_buffer, sizeof(url_buffer),
        "%s%s",
        LEADERBOARD_QR_PREFIX,
        (char *)temp);

    printf("url %s\n", url_buffer);

    bool ok = qrcodegen_encodeText(url_buffer, temp, buffer, qrcodegen_Ecc_LOW, LEADERBOARD_QR_VERSION, LEADERBOARD_QR_VERSION, qrcodegen_Mask_AUTO, true);

    if (!ok)
        return NULL;

    return buffer;
}
