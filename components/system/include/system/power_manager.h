#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    uint32_t display_timeout_ms;
    uint8_t low_battery_percent;
} power_manager_config_t;

esp_err_t power_manager_init(const power_manager_config_t *config);
void power_manager_notify_activity(void);
bool power_manager_should_dim_display(void);
void power_manager_handle_deep_sleep_wakeup_gate(gpio_num_t wake_gpio, uint32_t hold_ms);
esp_err_t power_manager_request_deep_sleep(gpio_num_t wake_gpio);
void power_manager_enter_deep_sleep(gpio_num_t wake_gpio) __attribute__((noreturn));
