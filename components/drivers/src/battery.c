#include "drivers/battery.h"

#include <string.h>
#include "drivers/analog.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "battery";

#define BATTERY_FILTER_ALPHA 0.03f
#define BATTERY_LOG_EVERY_SAMPLES 30U

typedef struct {
    battery_config_t config;
    battery_status_t status;
    TaskHandle_t task;
    bool initialized;
    uint32_t sample_count;
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

static esp_err_t sample_battery(uint16_t *pack_mv, int *raw_avg)
{
    int64_t raw_sum = 0;
    int64_t pin_mv_sum = 0;
    for (int i = 0; i < 16; ++i) {
        int raw = 0;
        uint32_t pin_mv = 0;
        ESP_RETURN_ON_ERROR(analog_read_raw_mv(s_battery.config.adc_gpio, &raw, &pin_mv), TAG, "battery read failed");
        raw_sum += raw;
        pin_mv_sum += pin_mv;
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    int raw = (int)(raw_sum / 16);
    uint32_t pin_mv = (uint32_t)(pin_mv_sum / 16);
    *pack_mv = (uint16_t)((float)pin_mv * s_battery.config.divider_multiplier);
    if (raw_avg != NULL) {
        *raw_avg = raw;
    }
    return ESP_OK;
}

static void battery_task(void *arg)
{
    (void)arg;
    float filtered_mv = (float)s_battery.status.millivolts;

    while (true) {
        uint16_t pack_mv = 0;
        int raw = 0;
        battery_status_t previous = s_battery.status;
        if (sample_battery(&pack_mv, &raw) == ESP_OK) {
            filtered_mv = filtered_mv * (1.0f - BATTERY_FILTER_ALPHA) + (float)pack_mv * BATTERY_FILTER_ALPHA;

            s_battery.status.millivolts = (uint16_t)filtered_mv;
            s_battery.status.percentage = percent_from_mv(s_battery.status.millivolts,
                                                          s_battery.config.empty_mv,
                                                          s_battery.config.full_mv);
            s_battery.status.low = s_battery.status.percentage <= s_battery.config.low_threshold_percent;
            if ((s_battery.sample_count++ % BATTERY_LOG_EVERY_SAMPLES) == 0U) {
                ESP_LOGI(TAG, "adc_raw=%d pack=%umV filtered=%umV percent=%u%s",
                         raw, pack_mv, s_battery.status.millivolts, s_battery.status.percentage,
                         pack_mv > s_battery.config.full_mv + 300 ? " input_above_liion_range_check_divider" :
                         (pack_mv < 2500 ? " input_below_liion_range" : ""));
            }
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

    uint16_t initial_mv = 0;
    int raw = 0;
    if (sample_battery(&initial_mv, &raw) == ESP_OK) {
        s_battery.status.millivolts = initial_mv;
        s_battery.status.percentage = percent_from_mv(initial_mv,
                                                      s_battery.config.empty_mv,
                                                      s_battery.config.full_mv);
        s_battery.status.low = s_battery.status.percentage <= s_battery.config.low_threshold_percent;
        ESP_LOGI(TAG, "initial adc_raw=%d pack=%umV percent=%u", raw, initial_mv, s_battery.status.percentage);
    } else {
        s_battery.status.millivolts = s_battery.config.empty_mv;
        s_battery.status.percentage = 0;
        s_battery.status.low = true;
    }
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
    if (s_battery.config.callback != NULL) {
        s_battery.config.callback(s_battery.config.callback_ctx, &s_battery.status);
    }
    BaseType_t ok = xTaskCreate(battery_task, "battery", 3072, NULL, 5, &s_battery.task);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

battery_status_t battery_get_status(void)
{
    return s_battery.status;
}
