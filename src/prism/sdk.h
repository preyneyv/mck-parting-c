#pragma once

/* Public include for cartridge authors. The raw u8g2 handle deliberately stays
 * available: wrappers cover OS-owned services, while rendering retains the
 * full fidelity of upstream u8g2 and elm. */
#include <prism/cartridge.h>
#include <prism/audio.h>
#include <prism/graphics.h>
#include <prism/leaderboard.h>
