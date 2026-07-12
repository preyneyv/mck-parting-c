#include "power_indicator.h"

#include "battery_charging.h"

enum
{
  BATTERY_BODY_WIDTH = 11,
  BATTERY_HEIGHT = 5,
  BATTERY_TERMINAL_WIDTH = 1,
  BATTERY_WIDTH = BATTERY_BODY_WIDTH + BATTERY_TERMINAL_WIDTH,
  BATTERY_FILL_WIDTH = BATTERY_BODY_WIDTH - 2,
};

void power_indicator_draw(u8g2_t *u8g2, int16_t right_x, int16_t top_y,
                          platform_power_state_t power)
{
  top_y++;
  int16_t x = right_x - BATTERY_WIDTH;

  if (power.plugged_in || power.charging)
  {
    uint8_t bitmap_mode = u8g2->bitmap_transparency;
    u8g2_SetDrawColor(u8g2, 1);
    u8g2_SetBitmapMode(u8g2, 0);
    u8g2_DrawXBM(u8g2, x, top_y - 1, BATTERY_CHARGING_WIDTH,
                 BATTERY_CHARGING_HEIGHT, BATTERY_CHARGING_BITS);
    u8g2_SetBitmapMode(u8g2, bitmap_mode);
    return;
  }

  uint8_t fill =
      (uint8_t)(((uint16_t)power.battery_level * BATTERY_FILL_WIDTH) / 255u);

  u8g2_SetDrawColor(u8g2, 1);
  u8g2_DrawFrame(u8g2, x, top_y, BATTERY_BODY_WIDTH, BATTERY_HEIGHT);
  u8g2_DrawBox(u8g2, x + BATTERY_BODY_WIDTH, top_y + 1,
               BATTERY_TERMINAL_WIDTH, BATTERY_HEIGHT - 2);
  if (fill != 0)
    u8g2_DrawBox(u8g2, x + 1, top_y + 1, fill, BATTERY_HEIGHT - 2);
}
