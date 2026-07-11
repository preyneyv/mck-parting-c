#include <assert.h>

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/pll.h>
#include <hardware/structs/rosc.h>
#include <hardware/xosc.h>
#include <pico/runtime_init.h>
#include <pico/sync.h>
#include <pico/stdlib.h>
#include <tusb.h>

#include <platform/input.h>
#include <platform/peripheral.h>
#include <platform/platform.h>
#include <platform/sleep.h>
#include <platform/time.h>

#include "config.h"
#include "management.h"

static critical_section_t g_sleep_critical_section;
static bool g_sleep_init = false;

static void platform_sleep_init(void)
{
  if (!g_sleep_init)
  {
    critical_section_init(&g_sleep_critical_section);
    g_sleep_init = true;
  }
}

inline static void rosc_write(io_rw_32 *addr, uint32_t value)
{
  hw_clear_bits(&rosc_hw->status, ROSC_STATUS_BADWRITE_BITS);
  assert(!(rosc_hw->status & ROSC_STATUS_BADWRITE_BITS));
  *addr = value;
  assert(!(rosc_hw->status & ROSC_STATUS_BADWRITE_BITS));
}

static void platform_sleep_until_interrupt(void)
{
  platform_sleep_init();
  critical_section_enter_blocking(&g_sleep_critical_section);
  sleep_ms(10);

  // rosc or xosc can be used. rosc is lower power.
  const bool use_xosc = false;

  // configure clocks
  uint src_hz;
  uint clk_ref_src;

  if (use_xosc)
  {
    src_hz = XOSC_HZ;
    clk_ref_src = CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC;
  }
  else
  {
    src_hz = 6500 * KHZ;
    clk_ref_src = CLOCKS_CLK_REF_CTRL_SRC_VALUE_ROSC_CLKSRC_PH;
  }
  clock_configure(clk_ref, clk_ref_src, 0, src_hz, src_hz);
  clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF, 0, src_hz,
                  src_hz);
  clock_stop(clk_adc);
  clock_stop(clk_usb);

  uint clk_rtc_src = use_xosc ? CLOCKS_CLK_RTC_CTRL_AUXSRC_VALUE_XOSC_CLKSRC
                              : CLOCKS_CLK_RTC_CTRL_AUXSRC_VALUE_ROSC_CLKSRC_PH;
  clock_configure(clk_rtc, 0, clk_rtc_src, src_hz, 46875);
  clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                  src_hz, src_hz);
  pll_deinit(pll_sys);
  pll_deinit(pll_usb);

  // kill the unused oscillator
  if (use_xosc)
  {
    // disable rosc
    uint32_t tmp = rosc_hw->ctrl;
    tmp &= (~ROSC_CTRL_ENABLE_BITS);
    tmp |= (ROSC_CTRL_ENABLE_VALUE_DISABLE << ROSC_CTRL_ENABLE_LSB);
    rosc_write(&rosc_hw->ctrl, tmp);
    // Wait for stable to go away
    while (rosc_hw->status & ROSC_STATUS_STABLE_BITS)
      ;
  }
  else
  {
    xosc_disable();
  }

  // enter sleep
  uint32_t event = IO_BANK0_DORMANT_WAKE_INTE0_GPIO0_EDGE_LOW_BITS;

  gpio_set_dormant_irq_enabled(BUTTON_PIN_L, event, true);
  gpio_set_dormant_irq_enabled(BUTTON_PIN_R, event, true);
  gpio_set_dormant_irq_enabled(BUTTON_PIN_M, event, true);
  // wake on USB power present (active-low PGOOD)
  gpio_set_dormant_irq_enabled(PERIPH_VSYS_PGOOD_N, event, true);

  if (use_xosc)
  {
    xosc_dormant();
  }
  else
  {
    rosc_write(&rosc_hw->dormant, ROSC_DORMANT_VALUE_DORMANT);
    while (!(rosc_hw->status & ROSC_STATUS_STABLE_BITS))
      ;
  }

  gpio_acknowledge_irq(BUTTON_PIN_L, event);
  gpio_acknowledge_irq(BUTTON_PIN_R, event);
  gpio_acknowledge_irq(BUTTON_PIN_M, event);
  gpio_acknowledge_irq(PERIPH_VSYS_PGOOD_N, event);

  // wake from sleep
  // - enable rosc
  rosc_write(&rosc_hw->ctrl, ROSC_CTRL_ENABLE_BITS);
  rosc_hw->ctrl = ROSC_CTRL_ENABLE_BITS;
  while (!(rosc_hw->status & ROSC_STATUS_STABLE_BITS))
    ;

  // - reset clock flags
  clocks_hw->sleep_en0 |= ~(0u);
  clocks_hw->sleep_en1 |= ~(0u);

  // - reset clock speeds (this will wake xosc)
  clocks_init();
  set_sys_clock_hz(SYS_CLOCK_HZ, true);

  sleep_ms(10);
  critical_section_exit(&g_sleep_critical_section);
}

static platform_input_mask_t first_button(platform_input_mask_t mask)
{
  if (mask & PLATFORM_INPUT_LEFT)
    return PLATFORM_INPUT_LEFT;
  if (mask & PLATFORM_INPUT_RIGHT)
    return PLATFORM_INPUT_RIGHT;
  if (mask & PLATFORM_INPUT_MENU)
    return PLATFORM_INPUT_MENU;
  return 0;
}

platform_wake_result_t platform_sleep_enter(uint32_t quick_wake_ms)
{
  while (platform_input_read_mask() & (PLATFORM_INPUT_LEFT | PLATFORM_INPUT_RIGHT | PLATFORM_INPUT_MENU))
  {
    platform_task();
    /* A remote button may be the input keeping this loop alive. Consume
     * WebUSB releases here instead of waiting for the sleep loop below. */
    management_sleep_task();
    platform_sleep_ms(5);
  }

  platform_peripheral_read_inputs();
  platform_power_state_t power = platform_peripheral_get_power_state();

  /* Keep clocks running briefly after an automatic sleep. This gives an
   * immediate "oops" press a cheap fast path and avoids needing an RTC to
   * measure elapsed time while the RP2040 is dormant. */
  platform_time_t quick_wake_until =
      platform_now_us() + (platform_time_t)quick_wake_ms * 1000u;
  while (quick_wake_ms != 0 && !platform_time_reached(quick_wake_until))
  {
    platform_task();
    if (tud_midi_available() || tud_cdc_available() ||
        management_sleep_task())
      return (platform_wake_result_t){0};
    platform_input_mask_t mask = platform_input_read_mask();
    if (mask != 0)
      return (platform_wake_result_t){
          .confirmation_required = false,
          .wake_button = first_button(mask),
      };
    platform_sleep_ms(5);
  }
  platform_peripheral_read_inputs();
  power = platform_peripheral_get_power_state();

  if (power.plugged_in)
  {
    while (1)
    {
      platform_task();
      if (tud_midi_available() || tud_cdc_available() ||
          management_sleep_task())
        return (platform_wake_result_t){0};

      platform_input_mask_t mask = platform_input_read_mask();
      if (mask & (PLATFORM_INPUT_LEFT | PLATFORM_INPUT_RIGHT |
                  PLATFORM_INPUT_MENU))
        return (platform_wake_result_t){
            .confirmation_required = true,
            .wake_button = first_button(mask),
        };

      power = platform_peripheral_get_power_state();
      if (!power.plugged_in)
      {
        break;
      }

      /* MIDI and plaintext serial remain explicit wake sources. WebUSB is
       * serviced in place so its heartbeat and mirror bookkeeping cannot
       * produce a spurious wake cycle. */
      platform_sleep_ms(5);
    }
    return (platform_wake_result_t){0};
  }

  platform_sleep_until_interrupt();
  platform_peripheral_read_inputs();
  power = platform_peripheral_get_power_state();
  platform_input_mask_t mask = platform_input_read_mask();
  return (platform_wake_result_t){
      .confirmation_required = !power.plugged_in && mask != 0,
      .wake_button = first_button(mask),
  };
}
