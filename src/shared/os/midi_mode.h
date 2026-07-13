#pragma once

#include <stdbool.h>

void prism_midi_mode_enter(void);
void prism_midi_mode_usb_disconnected(void);
bool prism_midi_mode_active(void);

#if defined(PRISM_TESTING)
#include <stddef.h>
void prism_midi_mode_test_set_allocation_failure(bool fail);
void prism_midi_mode_test_enter(void);
void prism_midi_mode_test_leave(void);
size_t prism_midi_mode_test_allocation_bytes(void);
#endif
