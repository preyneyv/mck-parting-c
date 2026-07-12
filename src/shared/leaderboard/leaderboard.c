#include <qrcodegen.h>
#include <string.h>

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
    const uint8_t *data;
} leaderboard_entry_t;

enum
{
    LEADERBOARD_PAYLOAD_BYTES = 16 + LEADERBOARD_MAX_DATA_BYTES,
    LEADERBOARD_BASE36_BYTES = 76,
    LEADERBOARD_URL_BYTES = 115,
};

static size_t pack_entry(const leaderboard_entry_t *entry, uint8_t *out)
{
    size_t offset = 0;
    out[offset++] = entry->app_id;
    for (size_t i = 0; i < sizeof(entry->board_id); i++)
        out[offset++] = entry->board_id[i];
    out[offset++] = (entry->entry_id >> 24) & 0xFF;
    out[offset++] = (entry->entry_id >> 16) & 0xFF;
    out[offset++] = (entry->entry_id >> 8) & 0xFF;
    out[offset++] = entry->entry_id & 0xFF;
    out[offset++] = entry->data_len;
    for (size_t i = 0; i < entry->data_len; i++)
        out[offset++] = entry->data[i];
    uint16_t checksum = fletcher16(out, offset);
    out[offset++] = checksum >> 8;
    out[offset++] = checksum & 0xFF;
    return offset;
}

bool leaderboard_get_qrcode(uint8_t app_id, const void *data, size_t data_len,
                            uint8_t qrcode[LEADERBOARD_QR_SIZE])
{
    if (qrcode == NULL || data_len > LEADERBOARD_MAX_DATA_BYTES ||
        (data_len > 0 && data == NULL))
        return false;

    leaderboard_entry_t entry = {
        .app_id = app_id,
        .entry_id = platform_rand_u32(),
        .data = data,
        .data_len = (uint8_t)data_len,
    };
    platform_device_id(entry.board_id);

    uint8_t payload[LEADERBOARD_PAYLOAD_BYTES];
    char encoded[LEADERBOARD_BASE36_BYTES];
    char url[LEADERBOARD_URL_BYTES];
    leaderboard_qrcode_t scratch;
    size_t payload_len = pack_entry(&entry, payload);
    size_t encoded_len = bytes_to_base36(payload, payload_len, encoded,
                                         sizeof(encoded));
    size_t prefix_len = strlen(LEADERBOARD_QR_PREFIX);
    if (encoded_len == 0 || prefix_len + encoded_len >= sizeof(url))
        return false;
    memcpy(url, LEADERBOARD_QR_PREFIX, prefix_len);
    memcpy(url + prefix_len, encoded, encoded_len + 1);

    return qrcodegen_encodeText(url, scratch, qrcode, qrcodegen_Ecc_LOW,
                                LEADERBOARD_QR_VERSION,
                                LEADERBOARD_QR_VERSION, qrcodegen_Mask_AUTO,
                                true);
}
