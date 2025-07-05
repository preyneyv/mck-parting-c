// Stubs for Pico SDK functions that are not available in the current
// environment.

#pragma once

#if !PICO_ON_DEVICE

// hardware/sync.h
#define __dmb() ((void)0)

#endif
