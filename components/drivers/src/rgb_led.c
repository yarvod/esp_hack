#include "drivers/rgb_led.h"

#include <stdbool.h>
#include <string.h>
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "rgb_led";

#define RGB_LED_RESOLUTION_HZ 10000000
#define RGB_LED_STACK_SIZE 3072

typedef struct {
    rgb_led_config_t config;
    rmt_channel_handle_t channel;
    rmt_encoder_handle_t encoder;
    TaskHandle_t task;
    bool started;
    bool off_sent;
    uint32_t tick;
} rgb_led_state_t;

static rgb_led_state_t s_rgb;

static const rmt_symbol_word_t s_ws2812_zero = {
    .level0 = 1,
    .duration0 = 0.3 * RGB_LED_RESOLUTION_HZ / 1000000,
    .level1 = 0,
    .duration1 = 0.9 * RGB_LED_RESOLUTION_HZ / 1000000,
};

static const rmt_symbol_word_t s_ws2812_one = {
    .level0 = 1,
    .duration0 = 0.9 * RGB_LED_RESOLUTION_HZ / 1000000,
    .level1 = 0,
    .duration1 = 0.3 * RGB_LED_RESOLUTION_HZ / 1000000,
};

static const rmt_symbol_word_t s_ws2812_reset = {
    .level0 = 0,
    .duration0 = RGB_LED_RESOLUTION_HZ / 1000000 * 50 / 2,
    .level1 = 0,
    .duration1 = RGB_LED_RESOLUTION_HZ / 1000000 * 50 / 2,
};

static size_t ws2812_encoder_callback(const void *data, size_t data_size,
                                      size_t symbols_written, size_t symbols_free,
                                      rmt_symbol_word_t *symbols, bool *done, void *arg)
{
    (void)arg;
    if (symbols_free < 8) {
        return 0;
    }

    size_t data_pos = symbols_written / 8;
    const uint8_t *bytes = (const uint8_t *)data;
    if (data_pos < data_size) {
        size_t symbol_pos = 0;
        for (uint8_t bitmask = 0x80; bitmask != 0; bitmask >>= 1) {
            symbols[symbol_pos++] = (bytes[data_pos] & bitmask) ? s_ws2812_one : s_ws2812_zero;
        }
        return symbol_pos;
    }

    symbols[0] = s_ws2812_reset;
    *done = true;
    return 1;
}

static void hsv_to_rgb(uint16_t hue, uint8_t sat, uint8_t val, uint8_t *red, uint8_t *green, uint8_t *blue)
{
    hue %= 360;
    uint8_t region = hue / 60;
    uint16_t remainder = (hue - (region * 60)) * 255 / 60;

    uint8_t p = (uint8_t)(((uint16_t)val * (255 - sat)) / 255);
    uint8_t q = (uint8_t)(((uint16_t)val * (255 - (((uint16_t)sat * remainder) / 255))) / 255);
    uint8_t t = (uint8_t)(((uint16_t)val * (255 - (((uint16_t)sat * (255 - remainder)) / 255))) / 255);

    switch (region) {
    case 0:
        *red = val;
        *green = t;
        *blue = p;
        break;
    case 1:
        *red = q;
        *green = val;
        *blue = p;
        break;
    case 2:
        *red = p;
        *green = val;
        *blue = t;
        break;
    case 3:
        *red = p;
        *green = q;
        *blue = val;
        break;
    case 4:
        *red = t;
        *green = p;
        *blue = val;
        break;
    default:
        *red = val;
        *green = p;
        *blue = q;
        break;
    }
}

static uint8_t eased_brightness(uint8_t breath, uint8_t max_brightness)
{
    uint8_t wave = breath < 128 ? breath * 2 : (uint8_t)((255 - breath) * 2);
    uint32_t eased = (uint32_t)wave * wave;
    uint8_t floor = max_brightness >= 10 ? 3 : 1;
    return (uint8_t)(floor + (eased * (max_brightness - floor)) / (255U * 255U));
}

static uint8_t triangle_wave(uint32_t phase)
{
    uint8_t value = (uint8_t)phase;
    return value < 128 ? value * 2 : (uint8_t)((255 - value) * 2);
}

static uint8_t soft_wave(uint32_t phase, uint8_t max_brightness)
{
    return eased_brightness((uint8_t)phase, max_brightness);
}

static uint32_t lcg_next(uint32_t *seed)
{
    *seed = *seed * 1664525U + 1013904223U;
    return *seed;
}

static void render_effect(rgb_led_mode_t mode, uint32_t tick, uint8_t max_brightness,
                          uint8_t *red, uint8_t *green, uint8_t *blue)
{
    switch (mode) {
    case RGB_LED_MODE_AURORA: {
        uint16_t hue = (uint16_t)(145 + ((uint32_t)triangle_wave(tick) * 130U) / 255U);
        uint8_t value = soft_wave(tick * 2U, max_brightness);
        hsv_to_rgb(hue, 210, value, red, green, blue);
        break;
    }
    case RGB_LED_MODE_NEON: {
        uint16_t hue = (uint16_t)(194 + ((uint32_t)triangle_wave(tick * 2U) * 112U) / 255U);
        uint8_t value = soft_wave(tick * 4U, max_brightness);
        hsv_to_rgb(hue, 255, value, red, green, blue);
        break;
    }
    case RGB_LED_MODE_EMBER: {
        uint32_t seed = tick * 1103515245U + 12345U;
        uint8_t base = max_brightness > 12 ? 10 : (max_brightness > 2 ? 2 : 1);
        uint8_t flicker = (uint8_t)(base + (lcg_next(&seed) % (max_brightness - base + 1U)));
        uint8_t value = flicker;
        hsv_to_rgb((uint16_t)(18 + (lcg_next(&seed) % 18U)), 245, value, red, green, blue);
        break;
    }
    case RGB_LED_MODE_SPARK: {
        uint8_t base = max_brightness > 5 ? 4 : 1;
        uint8_t value = base;
        uint32_t phase = tick % 96U;
        if (phase < 10U) {
            value = (uint8_t)(max_brightness - ((max_brightness - base) * phase) / 10U);
        }
        hsv_to_rgb((uint16_t)((tick * 7U) % 360U), 80, value, red, green, blue);
        break;
    }
    case RGB_LED_MODE_WHITE: {
        *red = max_brightness;
        *green = max_brightness;
        *blue = max_brightness;
        break;
    }
    case RGB_LED_MODE_RAINBOW:
    default: {
        uint16_t hue = (uint16_t)((tick * 2U) % 360U);
        uint8_t value = soft_wave(tick * 2U, max_brightness);
        hsv_to_rgb(hue, 255, value, red, green, blue);
        break;
    }
    }
}

static esp_err_t write_pixel(uint8_t red, uint8_t green, uint8_t blue)
{
    uint8_t pixels[3] = {
        green,
        red,
        blue,
    };
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };

    esp_err_t err = rmt_transmit(s_rgb.channel, s_rgb.encoder, pixels, sizeof(pixels), &tx_config);
    if (err == ESP_OK) {
        err = rmt_tx_wait_all_done(s_rgb.channel, pdMS_TO_TICKS(100));
    }
    return err;
}

static void rgb_led_task(void *arg)
{
    (void)arg;

    while (true) {
        esp_err_t err = ESP_OK;
        if (!s_rgb.config.enabled) {
            if (!s_rgb.off_sent) {
                err = write_pixel(0, 0, 0);
                s_rgb.off_sent = err == ESP_OK;
            }
            vTaskDelay(pdMS_TO_TICKS(120));
            continue;
        }

        s_rgb.off_sent = false;
        uint8_t red = 0;
        uint8_t green = 0;
        uint8_t blue = 0;
        render_effect(s_rgb.config.mode, s_rgb.tick, s_rgb.config.max_brightness, &red, &green, &blue);
        err = write_pixel(red, green, blue);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "rgb update failed: %s", esp_err_to_name(err));
        }

        s_rgb.tick++;
        vTaskDelay(pdMS_TO_TICKS(s_rgb.config.frame_period_ms));
    }
}

static rgb_led_mode_t clamp_mode(rgb_led_mode_t mode)
{
    return mode < RGB_LED_MODE_COUNT ? mode : RGB_LED_MODE_RAINBOW;
}

esp_err_t rgb_led_start(const rgb_led_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");
    if (s_rgb.started) {
        return ESP_OK;
    }

    memset(&s_rgb, 0, sizeof(s_rgb));
    s_rgb.config = *config;
    s_rgb.config.mode = clamp_mode(s_rgb.config.mode);
    if (s_rgb.config.max_brightness == 0) {
        s_rgb.config.max_brightness = 28;
    }
    if (s_rgb.config.frame_period_ms == 0) {
        s_rgb.config.frame_period_ms = 30;
    }

    rmt_tx_channel_config_t tx_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = s_rgb.config.gpio,
        .mem_block_symbols = 64,
        .resolution_hz = RGB_LED_RESOLUTION_HZ,
        .trans_queue_depth = 2,
    };
    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_config, &s_rgb.channel), TAG, "create rmt channel failed");

    rmt_simple_encoder_config_t encoder_config = {
        .callback = ws2812_encoder_callback,
    };
    ESP_RETURN_ON_ERROR(rmt_new_simple_encoder(&encoder_config, &s_rgb.encoder), TAG, "create rmt encoder failed");
    ESP_RETURN_ON_ERROR(rmt_enable(s_rgb.channel), TAG, "enable rmt channel failed");

    BaseType_t ok = xTaskCreate(rgb_led_task, "rgb_led", RGB_LED_STACK_SIZE, NULL, 2, &s_rgb.task);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "create task failed");

    s_rgb.started = true;
    ESP_LOGI(TAG, "started gpio=%d enabled=%d mode=%s max_brightness=%u frame=%lums",
             s_rgb.config.gpio, s_rgb.config.enabled, rgb_led_mode_name(s_rgb.config.mode),
             s_rgb.config.max_brightness, (unsigned long)s_rgb.config.frame_period_ms);
    return ESP_OK;
}

esp_err_t rgb_led_set_enabled(bool enabled)
{
    ESP_RETURN_ON_FALSE(s_rgb.started, ESP_ERR_INVALID_STATE, TAG, "not started");
    s_rgb.config.enabled = enabled;
    s_rgb.off_sent = false;
    return ESP_OK;
}

esp_err_t rgb_led_set_mode(rgb_led_mode_t mode)
{
    ESP_RETURN_ON_FALSE(s_rgb.started, ESP_ERR_INVALID_STATE, TAG, "not started");
    s_rgb.config.mode = clamp_mode(mode);
    s_rgb.tick = 0;
    return ESP_OK;
}

rgb_led_status_t rgb_led_get_status(void)
{
    return (rgb_led_status_t) {
        .enabled = s_rgb.config.enabled,
        .mode = clamp_mode(s_rgb.config.mode),
    };
}

const char *rgb_led_mode_name(rgb_led_mode_t mode)
{
    switch (clamp_mode(mode)) {
    case RGB_LED_MODE_AURORA: return "AURORA";
    case RGB_LED_MODE_NEON: return "NEON";
    case RGB_LED_MODE_EMBER: return "EMBER";
    case RGB_LED_MODE_SPARK: return "SPARK";
    case RGB_LED_MODE_WHITE: return "WHITE";
    case RGB_LED_MODE_RAINBOW:
    default:
        return "RAINBOW";
    }
}
