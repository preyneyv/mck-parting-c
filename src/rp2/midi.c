#include <tusb.h>

#include <shared/engine.h>
#include <shared/audio/synth.h>
#include <shared/audio/midi.h>

static void handle_sysex(uint8_t *data, int len)
{

    audio_synth_sysex_cmd_t cmd;
    if (!audio_synth_sysex_cmd_parse(data, len, &cmd))
    {
        printf("  [ERR] failed to parse sysex\n");
        printf("SysEx (%d): ", len);
        for (int i = 0; i < len; i++)
            printf("%02X ", data[i]);
        printf("\n");
        return;
    }

    switch (cmd.cmd)
    {
    case AUDIO_SYNTH_SYSEX_CMD_SET_PATCH:
        // printf("  Set Patch SysEx: patch_idx=%d\n", cmd.data.set_patch.patch_idx);
        audio_synth_patch_config_set(&g_engine.synth,
                                     cmd.data.set_patch.patch_idx,
                                     cmd.data.set_patch.patch);
        break;
    default:
        printf("  [ERR] unknown sysex cmd %d\n", cmd.cmd);
        break;
    }
}

static void handle_midi(uint8_t status, uint8_t d1, uint8_t d2)
{
    uint8_t type = status & 0xF0;
    uint8_t ch = status & 0x0F;

    switch (type)
    {
    case 0x90: // note on
        if (d2 == 0)
        {
            // note on zero velocity = note off
            audio_synth_enqueue(&g_engine.synth,
                                &(audio_synth_message_t){
                                    .type = AUDIO_SYNTH_MESSAGE_NOTE_OFF,
                                    .data.note_off = {
                                        .patch_idx = ch,
                                        .note_number = d1,
                                    }});
            // printf("Note OFF   ch=%d note=%d vel=%d\n", ch + 1, d1, d2);
        }
        else
        {
            // note on >0 velocity
            audio_synth_enqueue(&g_engine.synth,
                                &(audio_synth_message_t){
                                    .type = AUDIO_SYNTH_MESSAGE_NOTE_ON,
                                    .data.note_on = {
                                        .patch_idx = ch,
                                        .note_number = d1,
                                        .velocity = d2,
                                    }});

            // printf("Note ON   ch=%d note=%d vel=%d\n", ch + 1, d1, d2);
        }
        break;

    case 0x80: // note off
    {
        // note off
        audio_synth_enqueue(&g_engine.synth,
                            &(audio_synth_message_t){
                                .type = AUDIO_SYNTH_MESSAGE_NOTE_OFF,
                                .data.note_off = {
                                    .patch_idx = ch,
                                    .note_number = d1,
                                }});
        // printf("Note OFF  ch=%d note=%d vel=%d\n", ch + 1, d1, d2);
        break;
    }

    case 0xB0: // control change
        if (d1 == 123)
        {
            // release all notes on patch
            audio_synth_enqueue(&g_engine.synth,
                                &(audio_synth_message_t){
                                    .type = AUDIO_SYNTH_MESSAGE_NOTE_OFF,
                                    .data.note_off = {
                                        .patch_idx = ch,
                                        .note_number = -1,
                                    }});
            // printf("All Notes Off ch=%d\n", ch + 1);
        }
        else if (d1 == 120)
        {

            audio_synth_enqueue(&g_engine.synth,
                                &(audio_synth_message_t){
                                    .type = AUDIO_SYNTH_MESSAGE_PANIC,
                                    .data.panic = {}});
            // printf("PANIC (All Sound Off) ch=%d\n", ch + 1);
        }
        break;

    default:
        break;
    }
}

#define SYSEX_MAX 1024

static uint8_t sysex_buf[SYSEX_MAX];
static int sysex_len = 0;

void midi_task(void)
{
    uint8_t packet[4];

    while (tud_midi_available())
    {
        // printf("midi_task: checking for MIDI data...\n");
        tud_midi_packet_read(packet);
        engine_mark_input();

        uint8_t cin = packet[0] & 0x0F;
        uint8_t status = packet[1];

        // ---------- SysEx ----------
        if (cin >= 0x4 && cin <= 0x7)
        {
            int bytes = 3;
            if (cin == 0x5)
                bytes = 1;
            if (cin == 0x6)
                bytes = 2;

            for (int i = 0; i < bytes && sysex_len < SYSEX_MAX; i++)
                sysex_buf[sysex_len++] = packet[1 + i];

            if (cin != 0x4) // end of sysex
            {
                handle_sysex(sysex_buf, sysex_len);
                sysex_len = 0;
            }

            continue;
        }

        // ---------- regular midi ----------
        handle_midi(status, packet[2], packet[3]);
    }
}
