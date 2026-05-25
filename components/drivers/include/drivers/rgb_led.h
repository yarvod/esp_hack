#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

#define RGB_LED_SETTING_ENABLED "rgb_on"
#define RGB_LED_SETTING_MODE "rgb_mode"

typedef enum {
    RGB_LED_MODE_RAINBOW = 0,
    RGB_LED_MODE_AURORA,
    RGB_LED_MODE_NEON,
    RGB_LED_MODE_EMBER,
    RGB_LED_MODE_SPARK,
    RGB_LED_MODE_WHITE,
    RGB_LED_MODE_COUNT,
} rgb_led_mode_t;

typedef struct {
    gpio_num_t gpio;
    bool enabled;
    rgb_led_mode_t mode;
    uint8_t max_brightness;
    uint32_t frame_period_ms;
} rgb_led_config_t;

typedef struct {
    bool enabled;
    rgb_led_mode_t mode;
} rgb_led_status_t;

esp_err_t rgb_led_start(const rgb_led_config_t *config);
esp_err_t rgb_led_set_enabled(bool enabled);
esp_err_t rgb_led_set_mode(rgb_led_mode_t mode);
rgb_led_status_t rgb_led_get_status(void);
const char *rgb_led_mode_name(rgb_led_mode_t mode);
