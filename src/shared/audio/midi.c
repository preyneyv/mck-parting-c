#include "midi.h"

// ---- Protocol constants ----
enum
{
    PRISM_SYX_F0 = 0xF0,
    PRISM_SYX_F7 = 0xF7,
    PRISM_MFG_ID = 0x7D,
    PRISM_CMD_SET_PATCH = 0x01,
    PRISM_PROTO_VER = 0x01,
};

// ---- Helpers: decode 7-bit-chunk little-endian base-128 ----
static bool read_u7(const uint8_t *msg, size_t len, size_t *io, uint8_t *out)
{
    if (*io >= len)
        return false;
    uint8_t b = msg[*io];
    (*io)++;
    if (b & 0x80)
        return false; // must be 7-bit clean
    *out = b;
    return true;
}

static bool read_u16_7x3(const uint8_t *msg, size_t len, size_t *io, uint32_t *out)
{
    // 3 bytes of 7-bit chunks (little-endian): b0 + (b1<<7) + (b2<<14)
    uint8_t b0, b1, b2;
    if (!read_u7(msg, len, io, &b0))
        return false;
    if (!read_u7(msg, len, io, &b1))
        return false;
    if (!read_u7(msg, len, io, &b2))
        return false;

    *out = ((uint32_t)b0) | ((uint32_t)b1 << 7) | ((uint32_t)b2 << 14);
    return true;
}

static bool read_u32_7x5(const uint8_t *msg, size_t len, size_t *io, uint32_t *out)
{
    // 5 bytes of 7-bit chunks (little-endian): b0 + (b1<<7) + ... + (b4<<28)
    uint8_t b[5];
    for (int i = 0; i < 5; i++)
    {
        if (!read_u7(msg, len, io, &b[i]))
            return false;
    }

    *out = ((uint32_t)b[0]) |
           ((uint32_t)b[1] << 7) |
           ((uint32_t)b[2] << 14) |
           ((uint32_t)b[3] << 21) |
           ((uint32_t)b[4] << 28);
    return true;
}

bool audio_synth_sysex_cmd_parse_set_patch(const uint8_t *msg,
                                           size_t msg_len, uint8_t version, audio_synth_sysex_cmd_set_patch_t *out)
{
    if (version != AUDIO_SYNTH_SYSEX_CMD_SET_PATCH_VERSION)
        return false;

    size_t i = 0;
    if (!read_u7(msg, msg_len, &i, &out->patch_idx))
        return false;

    for (int op = 0; op < AUDIO_SYNTH_OPERATOR_COUNT; op++)
    {
        // read raw fields
        uint8_t freq_mult_u7;
        if (!read_u7(msg, msg_len, &i, &freq_mult_u7))
            return false;

        uint32_t level_q15_u32;
        if (!read_u16_7x3(msg, msg_len, &i, &level_q15_u32))
            return false;

        uint8_t mode_u7;
        if (!read_u7(msg, msg_len, &i, &mode_u7))
            return false;

        uint32_t a_u32, d_u32, s_u32, r_u32;
        if (!read_u32_7x5(msg, msg_len, &i, &a_u32))
            return false;
        if (!read_u32_7x5(msg, msg_len, &i, &d_u32))
            return false;
        if (!read_u32_7x5(msg, msg_len, &i, &s_u32))
            return false;
        if (!read_u32_7x5(msg, msg_len, &i, &r_u32))
            return false;

        // populate struct
        audio_synth_operator_config_t *cfg = &out->patch.ops[op];
        cfg->freq_mult = (int)freq_mult_u7;
        cfg->level = q1x15_clamp_s32((int32_t)level_q15_u32);
        cfg->mode = (audio_synth_operator_mode_t)mode_u7;
        cfg->env.a = a_u32;
        cfg->env.d = d_u32;
        cfg->env.s = q1x31_clamp_s64((int32_t)s_u32);
        cfg->env.r = r_u32;
    }
    return true;
}

bool audio_synth_sysex_cmd_parse(const uint8_t *msg,
                                 size_t msg_len,
                                 audio_synth_sysex_cmd_t *out)
{
    if (!msg || !out)
        return false;
    if (msg_len < 6)
        return false; // need at least header+F7

    // framing checks
    if (msg[0] != 0xF0)
        return false;
    if (msg[msg_len - 1] != 0xF7)
        return false;

    // check mfg
    if (msg[1] != AUDIO_SYNTH_SYSEX_MFG_ID)
        return false;

    // parse header
    out->cmd = (audio_synth_sysex_cmd_type_t)msg[2];
    uint8_t version = msg[3];
    out->version = version;

    msg = &msg[4];
    msg_len -= 5; // exclude header and tail

    bool success = false;
    switch (out->cmd)
    {
    case AUDIO_SYNTH_SYSEX_CMD_SET_PATCH:
    {
        success = audio_synth_sysex_cmd_parse_set_patch(msg, msg_len, version, &out->data.set_patch);
        break;
    }
    }

    return success;
}

// // parse a sysex message and extract the patch config
// bool prism_parse_patch_sysex(const uint8_t *msg,
//                              size_t msg_len,
//                              uint8_t *out_ch0,
//                              audio_synth_patch_config_t *out)
// {
//     if (!msg || !out)
//         return false;
//     if (msg_len < 6)
//         return false; // need at least header+F7

//     // Basic framing checks
//     if (msg[0] != PRISM_SYX_F0)
//         return false;
//     if (msg[msg_len - 1] != PRISM_SYX_F7)
//         return false;

//     // Header
//     if (msg[1] != PRISM_MFG_ID)
//         return false;
//     if (msg[2] != PRISM_CMD_SET_PATCH)
//         return false;

//     uint8_t ch0 = msg[3] & 0x0F;
//     uint8_t ver = msg[4];
//     if (ver != PRISM_PROTO_VER)
//         return false;

//     // Expected payload length for your fixed operator count
//     const size_t header_len = 5;  // F0, MFG, CMD, CH, VER
//     const size_t trailer_len = 1; // F7
//     const size_t per_op = 25;
//     const size_t expected_len = header_len + (size_t)AUDIO_SYNTH_OPERATOR_COUNT * per_op + trailer_len;

//     // If you want strict length enforcement, keep this check.
//     // If you might add fields later but keep VER bumped, you can relax it.
//     if (msg_len != expected_len)
//         return false;

//     size_t i = header_len; // start of operator payload

//     // Parse operators
//     for (int op = 0; op < AUDIO_SYNTH_OPERATOR_COUNT; op++)
//     {
//         uint8_t freq_mult_u7;
//         if (!read_u7(msg, msg_len - trailer_len, &i, &freq_mult_u7))
//             return false;

//         uint32_t level_q15_u32;
//         if (!read_u16_7x3(msg, msg_len - trailer_len, &i, &level_q15_u32))
//             return false;

//         uint8_t mode_u7;
//         if (!read_u7(msg, msg_len - trailer_len, &i, &mode_u7))
//             return false;

//         uint32_t a_u32, d_u32, s_u32, r_u32;
//         if (!read_u32_7x5(msg, msg_len - trailer_len, &i, &a_u32))
//             return false;
//         if (!read_u32_7x5(msg, msg_len - trailer_len, &i, &d_u32))
//             return false;
//         if (!read_u32_7x5(msg, msg_len - trailer_len, &i, &s_u32))
//             return false;
//         if (!read_u32_7x5(msg, msg_len - trailer_len, &i, &r_u32))
//             return false;

//         audio_synth_operator_mode_t mode;
//         if (!mode_from_u7(mode_u7, &mode))
//             return false;

//         // Assign into patch
//         audio_synth_operator_config_t *dst = &out->ops[op];
//         dst->freq_mult = (int)freq_mult_u7;

//         // Clamp to int16 range for q1x15 (sender uses 0..32767)
//         if (level_q15_u32 > 32767u)
//             level_q15_u32 = 32767u;
//         dst->level = (q1x15)(int16_t)level_q15_u32;

//         dst->mode = mode;
//         dst->env.a = a_u32;
//         dst->env.d = d_u32;

//         // s is Q1.31: sender uses 0..2147483647, treat as signed int32
//         if (s_u32 > 0x7FFFFFFFu)
//             s_u32 = 0x7FFFFFFFu;
//         dst->env.s = (q1x31)(int32_t)s_u32;

//         dst->env.r = r_u32;
//     }

//     // i should now point at msg_len-1 (the F7 trailer)
//     if (i != msg_len - 1)
//         return false;

//     if (out_ch0)
//         *out_ch0 = ch0;
//     return true;
// }
