#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    uint16_t millivolts;
    uint8_t percentage;
    bool low;
} battery_status_t;

typedef void (*battery_status_cb_t)(void *ctx, const battery_status_t *status);

typedef struct {
    gpio_num_t adc_gpio;
    float divider_multiplier;
    uint16_t empty_mv;
    uint16_t full_mv;
    uint8_t low_threshold_percent;
    uint32_t sample_period_ms;
    battery_status_cb_t callback;
    void *callback_ctx;
} battery_config_t;

esp_err_t battery_init(const battery_config_t *config);
esp_err_t battery_start(void);
battery_status_t battery_get_status(void);
