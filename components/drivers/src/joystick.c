#include "drivers/joystick.h"

#include <string.h>
#include "drivers/analog.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "joystick";

typedef struct {
    joystick_config_t config;
    TaskHandle_t task;
    int center_x;
    int center_y;
    joystick_event_type_t active_direction;
    bool has_direction;
    bool button_down;
    bool long_sent;
    int64_t button_changed_us;
    int64_t button_down_us;
    int64_t next_repeat_us;
    bool initialized;
} joystick_state_t;

static joystick_state_t s_joy;

static void emit(joystick_event_type_t type, joystick_event_phase_t phase)
{
    if (s_joy.config.callback == NULL) {
        return;
    }
    joystick_event_t event = {
        .type = type,
        .phase = phase,
        .timestamp_us = esp_timer_get_time(),
    };
    s_joy.config.callback(s_joy.config.callback_ctx, &event);
}

static joystick_event_type_t dominant_direction(int x, int y, bool *valid)
{
    int dx = x - s_joy.center_x;
    int dy = y - s_joy.center_y;
    *valid = false;

    int abs_x = dx >= 0 ? dx : -dx;
    int abs_y = dy >= 0 ? dy : -dy;
    if (abs_x < s_joy.config.deadzone_raw && abs_y < s_joy.config.deadzone_raw) {
        return JOYSTICK_EVENT_UP;
    }

    *valid = true;
    if (abs_x >= abs_y) {
        return dx > 0 ? JOYSTICK_EVENT_RIGHT : JOYSTICK_EVENT_LEFT;
    }
    return dy > 0 ? JOYSTICK_EVENT_DOWN : JOYSTICK_EVENT_UP;
}

static void poll_axis(void)
{
    int raw_x = 0;
    int raw_y = 0;
    if (analog_read_raw(s_joy.config.x_gpio, &raw_x) != ESP_OK ||
        analog_read_raw(s_joy.config.y_gpio, &raw_y) != ESP_OK) {
        return;
    }

    bool valid = false;
    joystick_event_type_t direction = dominant_direction(raw_x, raw_y, &valid);
    int64_t now = esp_timer_get_time();

    if (!valid) {
        if (s_joy.has_direction) {
            emit(s_joy.active_direction, JOYSTICK_EVENT_RELEASE);
        }
        s_joy.has_direction = false;
        return;
    }

    if (!s_joy.has_direction || s_joy.active_direction != direction) {
        if (s_joy.has_direction) {
            emit(s_joy.active_direction, JOYSTICK_EVENT_RELEASE);
        }
        s_joy.active_direction = direction;
        s_joy.has_direction = true;
        s_joy.next_repeat_us = now + (int64_t)s_joy.config.repeat_delay_ms * 1000;
        emit(direction, JOYSTICK_EVENT_PRESS);
        return;
    }

    if (now >= s_joy.next_repeat_us) {
        s_joy.next_repeat_us = now + (int64_t)s_joy.config.repeat_interval_ms * 1000;
        emit(direction, JOYSTICK_EVENT_REPEAT);
    }
}

static void poll_button(void)
{
    int level = gpio_get_level(s_joy.config.sw_gpio);
    bool pressed_now = level == 0;
    int64_t now = esp_timer_get_time();
    const int64_t debounce_us = 40000;

    if (pressed_now != s_joy.button_down && now - s_joy.button_changed_us < debounce_us) {
        return;
    }

    if (pressed_now != s_joy.button_down) {
        s_joy.button_changed_us = now;
        s_joy.button_down = pressed_now;
        if (pressed_now) {
            s_joy.long_sent = false;
            s_joy.button_down_us = now;
        } else if (!s_joy.long_sent) {
            emit(JOYSTICK_EVENT_SELECT, JOYSTICK_EVENT_PRESS);
            emit(JOYSTICK_EVENT_SELECT, JOYSTICK_EVENT_RELEASE);
        } else {
            emit(JOYSTICK_EVENT_BACK, JOYSTICK_EVENT_RELEASE);
        }
    }

    if (s_joy.button_down && !s_joy.long_sent &&
        now - s_joy.button_down_us >= (int64_t)s_joy.config.long_press_ms * 1000) {
        s_joy.long_sent = true;
        emit(JOYSTICK_EVENT_BACK, JOYSTICK_EVENT_PRESS);
    }
}

static void joystick_task(void *arg)
{
    (void)arg;
    while (true) {
        poll_axis();
        poll_button();
        vTaskDelay(pdMS_TO_TICKS(s_joy.config.poll_period_ms));
    }
}

static esp_err_t calibrate_center(void)
{
    int64_t sum_x = 0;
    int64_t sum_y = 0;
    for (int i = 0; i < 32; ++i) {
        int x = 0;
        int y = 0;
        ESP_RETURN_ON_ERROR(analog_read_raw(s_joy.config.x_gpio, &x), TAG, "read x failed");
        ESP_RETURN_ON_ERROR(analog_read_raw(s_joy.config.y_gpio, &y), TAG, "read y failed");
        sum_x += x;
        sum_y += y;
        vTaskDelay(pdMS_TO_TICKS(4));
    }
    s_joy.center_x = (int)(sum_x / 32);
    s_joy.center_y = (int)(sum_y / 32);
    ESP_LOGI(TAG, "center calibrated x=%d y=%d", s_joy.center_x, s_joy.center_y);
    return ESP_OK;
}

esp_err_t joystick_init(const joystick_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");
    memset(&s_joy, 0, sizeof(s_joy));
    s_joy.config = *config;
    if (s_joy.config.deadzone_raw == 0) {
        s_joy.config.deadzone_raw = 650;
    }
    if (s_joy.config.poll_period_ms == 0) {
        s_joy.config.poll_period_ms = 20;
    }
    if (s_joy.config.repeat_delay_ms == 0) {
        s_joy.config.repeat_delay_ms = 350;
    }
    if (s_joy.config.repeat_interval_ms == 0) {
        s_joy.config.repeat_interval_ms = 120;
    }
    if (s_joy.config.long_press_ms == 0) {
        s_joy.config.long_press_ms = 700;
    }

    gpio_config_t sw_cfg = {
        .pin_bit_mask = 1ULL << s_joy.config.sw_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&sw_cfg), TAG, "button gpio config failed");
    ESP_RETURN_ON_ERROR(analog_configure_gpio(s_joy.config.x_gpio), TAG, "x adc config failed");
    ESP_RETURN_ON_ERROR(analog_configure_gpio(s_joy.config.y_gpio), TAG, "y adc config failed");
    ESP_RETURN_ON_ERROR(calibrate_center(), TAG, "calibrate failed");

    s_joy.button_changed_us = esp_timer_get_time();
    s_joy.initialized = true;
    return ESP_OK;
}

esp_err_t joystick_start(void)
{
    ESP_RETURN_ON_FALSE(s_joy.initialized, ESP_ERR_INVALID_STATE, TAG, "joystick not initialized");
    if (s_joy.task != NULL) {
        return ESP_OK;
    }
    BaseType_t ok = xTaskCreate(joystick_task, "joystick", 3072, NULL, 6, &s_joy.task);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
