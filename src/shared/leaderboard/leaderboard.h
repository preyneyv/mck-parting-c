#pragma once

#include <stdint.h>
#include <stdlib.h>

#define LEADERBOARD_QR_VERSION 4
#define LEADERBOARD_QR_PREFIX "HTTPS://PRISM.PREYNEYV.DEV/L/"

/**
 * Generate a leaderboard QR code for the given app and data.
 * Buffer is only valid until the next call to this function.
 * @param app_id The application ID.
 * @param data Pointer to the data to encode.
 * @param data_len Length of the data to encode.
 * @return Pointer to the generated QR code buffer, or NULL on failure.
 */
uint8_t *leaderboard_get_qrcode(uint8_t app_id, void *data, size_t data_len);
