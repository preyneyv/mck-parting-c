#include <prism/audio.h>
#include <prism/cartridge.h>
#include <prism/graphics.h>
#include <prism/leaderboard.h>
#include <prism/sdk.h>
#include <prism/types.h>

_Static_assert(PRISM_ENGINE_TICK_RATE == 960u,
               "public engine tick rate changed");

int prism_sdk_public_headers_compile(void)
{
  return PRISM_DISPLAY_WIDTH + PRISM_DISPLAY_HEIGHT + PRISM_LED_COUNT;
}
