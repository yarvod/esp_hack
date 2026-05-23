#include "system/power_manager.h"

#include <stdbool.h>
#include "esp_timer.h"

typedef struct {
    power_manager_config_t config;
    int64_t last_activity_us;
    bool initialized;
} power_state_t;

static power_state_t s_power;

esp_err_t power_manager_init(const power_manager_config_t *config)
{
    s_power.config.display_timeout_ms = config != NULL ? config->display_timeout_ms : 30000;
    s_power.config.low_battery_percent = config != NULL ? config->low_battery_percent : 10;
    s_power.last_activity_us = esp_timer_get_time();
    s_power.initialized = true;
    return ESP_OK;
}

void power_manager_notify_activity(void)
{
    s_power.last_activity_us = esp_timer_get_time();
}

bool power_manager_should_dim_display(void)
{
    if (!s_power.initialized || s_power.config.display_timeout_ms == 0) {
        return false;
    }
    int64_t elapsed_us = esp_timer_get_time() - s_power.last_activity_us;
    return elapsed_us > (int64_t)s_power.config.display_timeout_ms * 1000;
}
