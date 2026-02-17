#include <qrcodegen.h>
#include <stdio.h>

#include <platform/identity.h>

#include "leaderboard.h"
#include "encoding.h"

// total header bytes: 1 (app_id) + 8 (board_id) + 4 (entry_id) + 1 (data_len) = 14 bytes
// plus data for app
// plus checksum (2 bytes)
// total overhead = 16 bytes
typedef struct
{
    uint8_t app_id;
    uint8_t board_id[8];
    uint32_t entry_id;
    uint8_t data_len; // max 255 bytes of data
    uint8_t *data;
} leaderboard_entry_t;

static leaderboard_qrcode_t temp;
static char url_buffer[115]; // max len for v4-L qr code in alphanum

static void _pack_leaderboard_entry(leaderboard_entry_t *entry, uint8_t *out_buf, size_t *out_len)
{
    size_t offset = 0;

    // app id [0:1] (1 byte)
    out_buf[offset++] = entry->app_id;

    // board id [1:9] (8 bytes)
    for (size_t i = 0; i < sizeof(entry->board_id); i++)
    {
        out_buf[offset++] = entry->board_id[i];
    }

    // entry id [9:13] (4 bytes)
    out_buf[offset++] = (entry->entry_id >> 24) & 0xFF;
    out_buf[offset++] = (entry->entry_id >> 16) & 0xFF;
    out_buf[offset++] = (entry->entry_id >> 8) & 0xFF;
    out_buf[offset++] = (entry->entry_id) & 0xFF;

    // data_len (1 byte)
    out_buf[offset++] = entry->data_len;

    // data [14:14+data_len]
    for (size_t i = 0; i < entry->data_len; i++)
    {
        out_buf[offset++] = entry->data[i];
    }

    // checksum [14+data_len:16+data_len] (2 bytes)
    uint16_t checksum = fletcher16(out_buf, offset);
    out_buf[offset++] = (checksum >> 8) & 0xFF;
    out_buf[offset++] = (checksum) & 0xFF;

    *out_len = offset;
}

bool leaderboard_get_qrcode(uint8_t app_id, void *data, size_t data_len, uint8_t *qrcode)
{
    leaderboard_entry_t entry = {
        .app_id = app_id,
        .entry_id = platform_rand_u32(), // simple random entry ID
        .data = data,
        .data_len = data_len,
    };
    platform_device_id(entry.board_id);

    size_t packed_len = 0;
    // abuse url buffer for packed data
    _pack_leaderboard_entry(&entry, url_buffer, &packed_len);
    // abuse temp buffer for base36 encoding
    bytes_to_base36(url_buffer, packed_len, temp, sizeof(temp));
    // construct final url
    snprintf(
        url_buffer, sizeof(url_buffer),
        "%s%s",
        LEADERBOARD_QR_PREFIX,
        (char *)temp);

    printf("[leaderboard] url gen: %s\n", url_buffer);

    // build final qr code
    bool ok = qrcodegen_encodeText(url_buffer, temp, qrcode, qrcodegen_Ecc_LOW, LEADERBOARD_QR_VERSION, LEADERBOARD_QR_VERSION, qrcodegen_Mask_AUTO, true);
    return ok;
}
