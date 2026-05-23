#include "drivers/battery.h"

#include <string.h>
#include "drivers/analog.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "battery";

typedef struct {
    battery_config_t config;
    battery_status_t status;
    TaskHandle_t task;
    bool initialized;
} battery_state_t;

static battery_state_t s_battery;

static uint8_t percent_from_mv(uint16_t mv, uint16_t empty_mv, uint16_t full_mv)
{
    if (mv <= empty_mv) {
        return 0;
    }
    if (mv >= full_mv) {
        return 100;
    }
    return (uint8_t)(((uint32_t)(mv - empty_mv) * 100U) / (uint32_t)(full_mv - empty_mv));
}

static void publish_if_changed(const battery_status_t *previous)
{
    if (s_battery.config.callback == NULL) {
        return;
    }
    if (previous->percentage != s_battery.status.percentage ||
        previous->low != s_battery.status.low ||
        (previous->millivolts > s_battery.status.millivolts + 20) ||
        (s_battery.status.millivolts > previous->millivolts + 20)) {
        s_battery.config.callback(s_battery.config.callback_ctx, &s_battery.status);
    }
}

static void battery_task(void *arg)
{
    (void)arg;
    float filtered_mv = 0.0f;
    bool first = true;

    while (true) {
        uint32_t pin_mv = 0;
        battery_status_t previous = s_battery.status;
        if (analog_read_mv(s_battery.config.adc_gpio, &pin_mv) == ESP_OK) {
            float pack_mv = (float)pin_mv * s_battery.config.divider_multiplier;
            if (first) {
                filtered_mv = pack_mv;
                first = false;
            } else {
                filtered_mv = filtered_mv * 0.88f + pack_mv * 0.12f;
            }

            s_battery.status.millivolts = (uint16_t)filtered_mv;
            s_battery.status.percentage = percent_from_mv(s_battery.status.millivolts,
                                                          s_battery.config.empty_mv,
                                                          s_battery.config.full_mv);
            s_battery.status.low = s_battery.status.percentage <= s_battery.config.low_threshold_percent;
            publish_if_changed(&previous);
        } else {
            ESP_LOGW(TAG, "battery adc read failed");
        }
        vTaskDelay(pdMS_TO_TICKS(s_battery.config.sample_period_ms));
    }
}

esp_err_t battery_init(const battery_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");
    memset(&s_battery, 0, sizeof(s_battery));
    s_battery.config = *config;
    if (s_battery.config.sample_period_ms == 0) {
        s_battery.config.sample_period_ms = 1000;
    }
    if (s_battery.config.empty_mv == 0) {
        s_battery.config.empty_mv = 3200;
    }
    if (s_battery.config.full_mv == 0) {
        s_battery.config.full_mv = 4200;
    }
    if (s_battery.config.divider_multiplier <= 0.1f) {
        s_battery.config.divider_multiplier = 2.0f;
    }
    ESP_RETURN_ON_ERROR(analog_configure_gpio(config->adc_gpio), TAG, "battery adc configure failed");

    s_battery.status.millivolts = s_battery.config.empty_mv;
    s_battery.status.percentage = 0;
    s_battery.status.low = true;
    s_battery.initialized = true;
    ESP_LOGI(TAG, "initialized adc_gpio=%d multiplier=%.2f", config->adc_gpio, s_battery.config.divider_multiplier);
    return ESP_OK;
}

esp_err_t battery_start(void)
{
    ESP_RETURN_ON_FALSE(s_battery.initialized, ESP_ERR_INVALID_STATE, TAG, "battery not initialized");
    if (s_battery.task != NULL) {
        return ESP_OK;
    }
    BaseType_t ok = xTaskCreate(battery_task, "battery", 3072, NULL, 5, &s_battery.task);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

battery_status_t battery_get_status(void)
{
    return s_battery.status;
}
