#pragma once

#include <stdbool.h>

#include <prism/graphics/color.h>
#include <prism/management_protocol.h>

void prism_settings_init(void);
const prism_management_settings_t *prism_settings_get(void);
color_t prism_settings_led_color(uint8_t led);
bool prism_settings_preview(const prism_management_settings_t *settings);
void prism_settings_frame(void);
void prism_settings_task(void);
void prism_settings_flush(void);
void prism_settings_volume_changed(uint8_t volume);
void prism_settings_brightness_changed(uint8_t brightness);
void prism_settings_set_save_deferred(bool deferred);
void prism_settings_mark_saved(void);
bool prism_settings_is_dirty(void);
bool prism_settings_first_interaction_complete(void);
bool prism_settings_guide_pending(void);
void prism_settings_complete_first_interaction(void);
void prism_settings_dismiss_guide(void);
