#include <stdio.h>

#include <tusb.h>

#include <shared/audio/midi.h>
#include <shared/engine.h>
#include <shared/os/midi_mode.h>

enum
{
  MIDI_NOTE_OFF = 0x80,
  MIDI_NOTE_ON = 0x90,
  MIDI_CONTROL_CHANGE = 0xb0,
  MIDI_ALL_SOUND_OFF = 120,
  MIDI_ALL_NOTES_OFF = 123,
  SYSEX_CAPACITY = 1024,
};

static uint8_t sysex_buffer[SYSEX_CAPACITY];
static size_t sysex_length;
static bool sysex_overflow;
static audio_synth_message_t pending_message;
static bool message_pending;

static void handle_sysex(const uint8_t *data, size_t length)
{
  audio_synth_sysex_cmd_t command;
  if (!audio_synth_sysex_cmd_parse(data, length, &command))
  {
    printf("midi: invalid sysex (%lu bytes)\n", (unsigned long)length);
    return;
  }

  if (command.cmd == AUDIO_SYNTH_SYSEX_CMD_SET_PATCH)
    audio_synth_patch_config_set(engine_synth(), command.data.set_patch.patch_idx,
                                 command.data.set_patch.patch);
}

static bool submit_message(const audio_synth_message_t *message)
{
  if (audio_synth_enqueue(engine_synth(), message))
    return true;
  pending_message = *message;
  message_pending = true;
  return false;
}

static bool enqueue_note(uint8_t type, uint8_t patch, int8_t note,
                         uint8_t velocity)
{
  audio_synth_message_t message = {.type = type};
  if (type == AUDIO_SYNTH_MESSAGE_NOTE_ON)
  {
    message.data.note_on.patch_idx = patch;
    message.data.note_on.note_number = (uint8_t)note;
    message.data.note_on.velocity = velocity;
  }
  else
  {
    message.data.note_off.patch_idx = patch;
    message.data.note_off.note_number = note;
  }
  return submit_message(&message);
}

static bool handle_midi_message(uint8_t status, uint8_t data1, uint8_t data2)
{
  uint8_t type = status & 0xf0;
  uint8_t channel = status & 0x0f;

  switch (type)
  {
  case MIDI_NOTE_ON:
    return enqueue_note(data2 == 0 ? AUDIO_SYNTH_MESSAGE_NOTE_OFF
                                   : AUDIO_SYNTH_MESSAGE_NOTE_ON,
                        channel, (int8_t)data1, data2);
  case MIDI_NOTE_OFF:
    return enqueue_note(AUDIO_SYNTH_MESSAGE_NOTE_OFF, channel, (int8_t)data1,
                        0);
  case MIDI_CONTROL_CHANGE:
    if (data1 == MIDI_ALL_NOTES_OFF)
      return enqueue_note(AUDIO_SYNTH_MESSAGE_NOTE_OFF, channel, -1, 0);
    else if (data1 == MIDI_ALL_SOUND_OFF)
    {
      audio_synth_message_t panic = {.type = AUDIO_SYNTH_MESSAGE_PANIC};
      return submit_message(&panic);
    }
    return true;
  default:
    return true;
  }
}

static void append_sysex_packet(const uint8_t packet[4], uint8_t code)
{
  uint8_t byte_count = code == 0x5 ? 1 : code == 0x6 ? 2 : 3;
  for (uint8_t i = 0; i < byte_count; ++i)
  {
    if (sysex_length < sizeof(sysex_buffer))
      sysex_buffer[sysex_length++] = packet[i + 1];
    else
      sysex_overflow = true;
  }

  if (code == 0x4)
    return;
  if (!sysex_overflow)
    handle_sysex(sysex_buffer, sysex_length);
  sysex_length = 0;
  sysex_overflow = false;
}

void midi_task(void)
{
  if (!tud_mounted())
  {
    prism_midi_mode_usb_disconnected();
    sysex_length = 0;
    sysex_overflow = false;
    message_pending = false;
    return;
  }

  if (message_pending)
  {
    if (!audio_synth_enqueue(engine_synth(), &pending_message))
      return;
    message_pending = false;
  }

  uint8_t packet[4];
  while (tud_midi_available() && tud_midi_packet_read(packet))
  {
    prism_midi_mode_enter();
    uint8_t code = packet[0] & 0x0f;
    if (code >= 0x4 && code <= 0x7)
      append_sysex_packet(packet, code);
    else if (!handle_midi_message(packet[1], packet[2], packet[3]))
      return;
  }
}
