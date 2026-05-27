#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define SYSTEM_SETTING_SHOW_FPS "show_fps"
#define SYSTEM_SETTING_LOW_POWER "low_power"

esp_err_t system_settings_init(void);
esp_err_t system_settings_get_u8(const char *key, uint8_t default_value, uint8_t *value);
esp_err_t system_settings_set_u8(const char *key, uint8_t value);
esp_err_t system_settings_get_bool(const char *key, bool default_value, bool *value);
esp_err_t system_settings_set_bool(const char *key, bool value);
