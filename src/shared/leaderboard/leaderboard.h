#pragma once

#include <stdint.h>
#include <stdlib.h>

#define LEADERBOARD_QR_VERSION 4
#define LEADERBOARD_QR_SIZE (qrcodegen_BUFFER_LEN_FOR_VERSION(LEADERBOARD_QR_VERSION))
#define LEADERBOARD_QR_PREFIX "HTTPS://PRISM.PREYNEYV.DEV/L/"

typedef uint8_t leaderboard_qrcode_t[LEADERBOARD_QR_SIZE];

/**
 * Generate a leaderboard QR code for the given app and data.
 * Buffer is only valid until the next call to this function.
 * @param app_id The application ID.
 * @param data Pointer to the data to encode.
 * @param data_len Length of the data to encode.
 * @param qrcode Pointer to the buffer where the generated QR code will be stored. Must be at least LEADERBOARD_QR_SIZE bytes.
 * @return true on success, false on failure.
 */
bool leaderboard_get_qrcode(uint8_t app_id, void *data, size_t data_len, uint8_t *qrcode);
