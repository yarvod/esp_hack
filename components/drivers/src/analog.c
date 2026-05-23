#include "drivers/analog.h"

#include <string.h>
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "analog";

typedef struct {
    bool initialized;
    adc_oneshot_unit_handle_t unit[ADC_UNIT_2 + 1];
    adc_cali_handle_t cali[ADC_UNIT_2 + 1][ADC_CHANNEL_9 + 1];
    bool unit_ready[ADC_UNIT_2 + 1];
    bool channel_ready[ADC_UNIT_2 + 1][ADC_CHANNEL_9 + 1];
    bool cali_ready[ADC_UNIT_2 + 1][ADC_CHANNEL_9 + 1];
    bool cali_unsupported[ADC_UNIT_2 + 1][ADC_CHANNEL_9 + 1];
} analog_state_t;

static analog_state_t s_state;

esp_err_t analog_init(void)
{
    if (s_state.initialized) {
        return ESP_OK;
    }
    memset(&s_state, 0, sizeof(s_state));
    s_state.initialized = true;
    return ESP_OK;
}

static esp_err_t ensure_unit(adc_unit_t unit)
{
    if (unit < ADC_UNIT_1 || unit > ADC_UNIT_2) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_state.unit_ready[unit]) {
        return ESP_OK;
    }

    adc_oneshot_unit_init_cfg_t cfg = {
        .unit_id = unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&cfg, &s_state.unit[unit]);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc unit %d init failed: %s", unit, esp_err_to_name(err));
        return err;
    }
    s_state.unit_ready[unit] = true;
    return ESP_OK;
}

static esp_err_t ensure_calibration(adc_unit_t unit, adc_channel_t channel)
{
    if (s_state.cali_ready[unit][channel]) {
        return ESP_OK;
    }
    if (s_state.cali_unsupported[unit][channel]) {
        return ESP_ERR_NOT_SUPPORTED;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = unit,
        .chan = channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_state.cali[unit][channel]);
    if (err == ESP_OK) {
        s_state.cali_ready[unit][channel] = true;
        ESP_LOGI(TAG, "calibration ready adc_unit=%d channel=%d", unit, channel);
        return ESP_OK;
    }
    s_state.cali_unsupported[unit][channel] = true;
    ESP_LOGW(TAG, "adc calibration unavailable unit=%d channel=%d: %s", unit, channel, esp_err_to_name(err));
    return err;
#else
    s_state.cali_unsupported[unit][channel] = true;
    ESP_LOGW(TAG, "adc calibration scheme is not supported");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t analog_configure_gpio(gpio_num_t gpio)
{
    ESP_RETURN_ON_FALSE(s_state.initialized, ESP_ERR_INVALID_STATE, TAG, "analog not initialized");

    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_0;
    esp_err_t err = adc_oneshot_io_to_channel(gpio, &unit, &channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio %d is not ADC capable: %s", gpio, esp_err_to_name(err));
        return err;
    }
    ESP_RETURN_ON_ERROR(ensure_unit(unit), TAG, "ensure unit failed");
    if (s_state.channel_ready[unit][channel]) {
        return ESP_OK;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_state.unit[unit], channel, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc gpio %d channel config failed: %s", gpio, esp_err_to_name(err));
        return err;
    }
    s_state.channel_ready[unit][channel] = true;
    ESP_LOGI(TAG, "configured gpio=%d adc_unit=%d channel=%d", gpio, unit, channel);
    return ESP_OK;
}

esp_err_t analog_read_raw(gpio_num_t gpio, int *raw)
{
    ESP_RETURN_ON_FALSE(raw != NULL, ESP_ERR_INVALID_ARG, TAG, "raw is null");
    ESP_RETURN_ON_ERROR(analog_configure_gpio(gpio), TAG, "configure gpio failed");

    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_0;
    ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(gpio, &unit, &channel), TAG, "io to channel failed");
    return adc_oneshot_read(s_state.unit[unit], channel, raw);
}

esp_err_t analog_read_mv(gpio_num_t gpio, uint32_t *millivolts)
{
    int raw = 0;
    return analog_read_raw_mv(gpio, &raw, millivolts);
}

esp_err_t analog_read_raw_mv(gpio_num_t gpio, int *raw, uint32_t *millivolts)
{
    ESP_RETURN_ON_FALSE(raw != NULL && millivolts != NULL, ESP_ERR_INVALID_ARG, TAG, "bad args");
    ESP_RETURN_ON_ERROR(analog_read_raw(gpio, raw), TAG, "raw read failed");

    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_0;
    ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(gpio, &unit, &channel), TAG, "io to channel failed");

    int calibrated_mv = 0;
    if (ensure_calibration(unit, channel) == ESP_OK &&
        adc_cali_raw_to_voltage(s_state.cali[unit][channel], *raw, &calibrated_mv) == ESP_OK) {
        *millivolts = (uint32_t)calibrated_mv;
        return ESP_OK;
    }

    *millivolts = (uint32_t)((*raw * 3300U) / 4095U);
    return ESP_OK;
}
