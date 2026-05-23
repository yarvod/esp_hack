#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

typedef enum {
    JOYSTICK_EVENT_UP = 0,
    JOYSTICK_EVENT_DOWN,
    JOYSTICK_EVENT_LEFT,
    JOYSTICK_EVENT_RIGHT,
    JOYSTICK_EVENT_SELECT,
    JOYSTICK_EVENT_BACK,
} joystick_event_type_t;

typedef enum {
    JOYSTICK_EVENT_PRESS = 0,
    JOYSTICK_EVENT_REPEAT,
    JOYSTICK_EVENT_RELEASE,
} joystick_event_phase_t;

typedef struct {
    joystick_event_type_t type;
    joystick_event_phase_t phase;
    int64_t timestamp_us;
} joystick_event_t;

typedef void (*joystick_event_cb_t)(void *ctx, const joystick_event_t *event);

typedef struct {
    gpio_num_t x_gpio;
    gpio_num_t y_gpio;
    gpio_num_t sw_gpio;
    uint16_t deadzone_raw;
    uint32_t poll_period_ms;
    uint32_t repeat_delay_ms;
    uint32_t repeat_interval_ms;
    uint32_t long_press_ms;
    bool swap_xy;
    bool invert_x;
    bool invert_y;
    joystick_event_cb_t callback;
    void *callback_ctx;
} joystick_config_t;

esp_err_t joystick_init(const joystick_config_t *config);
esp_err_t joystick_start(void);
