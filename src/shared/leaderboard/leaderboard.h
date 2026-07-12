#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <qrcodegen.h>

#define LEADERBOARD_QR_VERSION 4
#define LEADERBOARD_QR_SIZE (qrcodegen_BUFFER_LEN_FOR_VERSION(LEADERBOARD_QR_VERSION))
#define LEADERBOARD_QR_PREFIX "HTTPS://PRISM.PREYNEYV.DEV/L/"
#define LEADERBOARD_MAX_DATA_BYTES 32

typedef uint8_t leaderboard_qrcode_t[LEADERBOARD_QR_SIZE];

bool leaderboard_get_qrcode(uint8_t app_id, const void *data, size_t data_len,
                            uint8_t qrcode[LEADERBOARD_QR_SIZE]);
