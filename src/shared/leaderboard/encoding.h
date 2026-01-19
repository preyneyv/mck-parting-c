#pragma once

#include <stdint.h>
#include <stddef.h>

static char base36_digit(uint8_t v)
{
    return (v < 10) ? ('0' + v) : ('A' + (v - 10));
}

static size_t bytes_to_base36(
    const uint8_t *in, size_t in_len,
    char *out, size_t out_cap)
{
    // Make a working copy of input (big-endian)
    uint8_t buf[64]; // adjust if you need larger inputs
    if (in_len > sizeof(buf))
        return 0;

    for (size_t i = 0; i < in_len; i++)
        buf[i] = in[i];

    size_t buf_len = in_len;
    size_t out_len = 0;

    while (buf_len > 0)
    {
        uint16_t rem = 0;

        // Divide buffer by 36 in-place
        for (size_t i = 0; i < buf_len; i++)
        {
            uint16_t acc = (rem << 8) | buf[i];
            buf[i] = acc / 36;
            rem = acc % 36;
        }

        if (out_len >= out_cap)
            return 0;
        out[out_len++] = base36_digit(rem);

        // Trim leading zeros
        size_t leading = 0;
        while (leading < buf_len && buf[leading] == 0)
            leading++;

        buf_len -= leading;
        for (size_t i = 0; i < buf_len; i++)
            buf[i] = buf[i + leading];
    }

    // Reverse output
    for (size_t i = 0; i < out_len / 2; i++)
    {
        char tmp = out[i];
        out[i] = out[out_len - 1 - i];
        out[out_len - 1 - i] = tmp;
    }

    out[out_len] = '\0';
    return out_len;
}

// Fletcher-16 checksum
static uint16_t fletcher16(const uint8_t *data, size_t len)
{
    uint16_t sum1 = 0;
    uint16_t sum2 = 0;

    for (size_t i = 0; i < len; i++)
    {
        sum1 = (sum1 + data[i]) % 255;
        sum2 = (sum2 + sum1) % 255;
    }

    return (sum2 << 8) | sum1;
}
