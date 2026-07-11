#include <hardware/gpio.h>

#include <platform/input.h>

#include "config.h"

static platform_input_mask_t remote_mask;

void platform_input_init(void)
{
  gpio_init(BUTTON_PIN_L);
  gpio_set_dir(BUTTON_PIN_L, GPIO_IN);
  gpio_pull_up(BUTTON_PIN_L);

  gpio_init(BUTTON_PIN_R);
  gpio_set_dir(BUTTON_PIN_R, GPIO_IN);
  gpio_pull_up(BUTTON_PIN_R);

  gpio_init(BUTTON_PIN_M);
  gpio_set_dir(BUTTON_PIN_M, GPIO_IN);
  gpio_pull_up(BUTTON_PIN_M);
}

platform_input_mask_t platform_input_read_mask(void)
{
  platform_input_mask_t mask = 0;
  if (!gpio_get(BUTTON_PIN_L))
    mask |= PLATFORM_INPUT_LEFT;
  if (!gpio_get(BUTTON_PIN_R))
    mask |= PLATFORM_INPUT_RIGHT;
  if (!gpio_get(BUTTON_PIN_M))
    mask |= PLATFORM_INPUT_MENU;
  return mask | remote_mask;
}

void platform_input_set_remote_mask(platform_input_mask_t mask)
{
  remote_mask = mask &
                (PLATFORM_INPUT_LEFT | PLATFORM_INPUT_RIGHT | PLATFORM_INPUT_MENU);
}
