#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

esp_err_t analog_init(void);
esp_err_t analog_configure_gpio(gpio_num_t gpio);
esp_err_t analog_read_raw(gpio_num_t gpio, int *raw);
esp_err_t analog_read_mv(gpio_num_t gpio, uint32_t *millivolts);
esp_err_t analog_read_raw_mv(gpio_num_t gpio, int *raw, uint32_t *millivolts);
