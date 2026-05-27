#include <stdint.h>

#include "apps/apps_registry.h"
#include "core/context.h"
#include "core/input_dispatcher.h"
#include "core/screen_manager.h"
#include "drivers/analog.h"
#include "drivers/battery.h"
#include "drivers/board_pins.h"
#include "drivers/joystick.h"
#include "drivers/rgb_led.h"
#include "drivers/ssd1306.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "system/logger.h"
#include "system/power_manager.h"
#include "system/settings.h"
#include "system/storage.h"

#ifndef CONFIG_HANDHELD_JOYSTICK_SWAP_XY
#define CONFIG_HANDHELD_JOYSTICK_SWAP_XY 0
#endif

#ifndef CONFIG_HANDHELD_JOYSTICK_INVERT_X
#define CONFIG_HANDHELD_JOYSTICK_INVERT_X 0
#endif

#ifndef CONFIG_HANDHELD_JOYSTICK_INVERT_Y
#define CONFIG_HANDHELD_JOYSTICK_INVERT_Y 0
#endif

#ifndef CONFIG_HANDHELD_WAKE_HOLD_MS
#define CONFIG_HANDHELD_WAKE_HOLD_MS 3000
#endif

static const char *TAG = "handheld";
static core_context_t s_core;
static ssd1306_t s_display;
static ui_t s_ui;

static core_input_action_t map_joystick_action(joystick_event_type_t type)
{
    switch (type) {
    case JOYSTICK_EVENT_UP: return CORE_INPUT_UP;
    case JOYSTICK_EVENT_DOWN: return CORE_INPUT_DOWN;
    case JOYSTICK_EVENT_LEFT: return CORE_INPUT_LEFT;
    case JOYSTICK_EVENT_RIGHT: return CORE_INPUT_RIGHT;
    case JOYSTICK_EVENT_SELECT: return CORE_INPUT_SELECT;
    case JOYSTICK_EVENT_BACK: return CORE_INPUT_BACK;
    default: return CORE_INPUT_NONE;
    }
}

static core_input_phase_t map_joystick_phase(joystick_event_phase_t phase)
{
    switch (phase) {
    case JOYSTICK_EVENT_REPEAT: return CORE_INPUT_PHASE_REPEAT;
    case JOYSTICK_EVENT_RELEASE: return CORE_INPUT_PHASE_RELEASE;
    case JOYSTICK_EVENT_PRESS:
    default:
        return CORE_INPUT_PHASE_PRESS;
    }
}

static void joystick_callback(void *ctx, const joystick_event_t *event)
{
    core_context_t *core = (core_context_t *)ctx;
    core_event_t core_event = {
        .type = CORE_EVENT_INPUT,
        .input = {
            .action = map_joystick_action(event->type),
            .phase = map_joystick_phase(event->phase),
            .timestamp_us = event->timestamp_us,
        },
    };
    (void)core_event_bus_publish(&core->events, &core_event, 0);
}

static void battery_callback(void *ctx, const battery_status_t *status)
{
    core_context_t *core = (core_context_t *)ctx;
    core_event_t event = {
        .type = CORE_EVENT_BATTERY,
        .battery = {
            .percentage = status->percentage,
            .millivolts = status->millivolts,
            .low = status->low,
        },
    };
    (void)core_event_bus_publish(&core->events, &event, 0);
}

static esp_err_t init_hardware(void)
{
    bool rgb_enabled = true;
    uint8_t rgb_mode = RGB_LED_MODE_RAINBOW;
    bool show_fps = false;
    bool low_power = false;
    (void)system_settings_get_bool(RGB_LED_SETTING_ENABLED, true, &rgb_enabled);
    (void)system_settings_get_u8(RGB_LED_SETTING_MODE, RGB_LED_MODE_RAINBOW, &rgb_mode);
    (void)system_settings_get_bool(SYSTEM_SETTING_SHOW_FPS, false, &show_fps);
    (void)system_settings_get_bool(SYSTEM_SETTING_LOW_POWER, false, &low_power);

    ESP_RETURN_ON_ERROR(analog_init(), TAG, "analog init failed");
    ESP_RETURN_ON_ERROR(ssd1306_init(&s_display, 0, BOARD_PIN_OLED_SDA, BOARD_PIN_OLED_SCL), TAG, "display init failed");
    ESP_RETURN_ON_ERROR(ui_init(&s_ui, &s_display), TAG, "ui init failed");
    s_core.show_fps = show_fps;
    s_core.low_power_mode = low_power;
    s_core.render_elapsed_ms = low_power ? 50 : 0;
    ui_set_show_fps(&s_ui, show_fps);

    battery_config_t battery_cfg = {
        .adc_gpio = BOARD_PIN_BATTERY_ADC,
        .divider_multiplier = BOARD_BATTERY_DIVIDER_MULTIPLIER,
        .empty_mv = 3200,
        .full_mv = 4200,
        .low_threshold_percent = 10,
        .sample_period_ms = 1000,
        .callback = battery_callback,
        .callback_ctx = &s_core,
    };
    ESP_RETURN_ON_ERROR(battery_init(&battery_cfg), TAG, "battery init failed");

    joystick_config_t joystick_cfg = {
        .x_gpio = BOARD_PIN_JOY_X,
        .y_gpio = BOARD_PIN_JOY_Y,
        .sw_gpio = BOARD_PIN_JOY_SW,
        .deadzone_raw = CONFIG_HANDHELD_JOYSTICK_DEADZONE_RAW,
        .poll_period_ms = 20,
        .repeat_delay_ms = 650,
        .repeat_interval_ms = 250,
        .long_press_ms = 850,
        .swap_xy = CONFIG_HANDHELD_JOYSTICK_SWAP_XY,
        .invert_x = CONFIG_HANDHELD_JOYSTICK_INVERT_X,
        .invert_y = CONFIG_HANDHELD_JOYSTICK_INVERT_Y,
        .callback = joystick_callback,
        .callback_ctx = &s_core,
    };
    ESP_RETURN_ON_ERROR(joystick_init(&joystick_cfg), TAG, "joystick init failed");

    esp_err_t rgb_err = rgb_led_start(&(rgb_led_config_t) {
        .gpio = BOARD_PIN_RGB_LED,
        .enabled = rgb_enabled,
        .mode = (rgb_led_mode_t)rgb_mode,
        .max_brightness = 28,
        .frame_period_ms = 30,
    });
    if (rgb_err != ESP_OK) {
        ESP_LOGW(TAG, "rgb led start failed: %s", esp_err_to_name(rgb_err));
    }
    return ESP_OK;
}

static void handle_event(core_context_t *ctx, const core_event_t *event)
{
    switch (event->type) {
    case CORE_EVENT_INPUT:
        power_manager_notify_activity();
        core_input_dispatch(ctx, &event->input);
        break;
    case CORE_EVENT_BATTERY:
        ctx->battery_percent = event->battery.percentage;
        ctx->battery_mv = event->battery.millivolts;
        ctx->battery_low = event->battery.low;
        ui_set_battery(ctx->ui, ctx->battery_percent, ctx->battery_low);
        core_nav_mark_dirty(&ctx->nav);
        break;
    default:
        break;
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(system_logger_init());
    ESP_LOGI(TAG, "booting ESP32-C6 handheld framework");
    power_manager_handle_deep_sleep_wakeup_gate(BOARD_PIN_JOY_SW, CONFIG_HANDHELD_WAKE_HOLD_MS);
    ESP_ERROR_CHECK(system_settings_init());
    ESP_ERROR_CHECK(storage_init());
    ESP_ERROR_CHECK(power_manager_init(&(power_manager_config_t) {
        .display_timeout_ms = 30000,
        .low_battery_percent = 10,
    }));

    core_context_init(&s_core, &s_ui);
    ESP_ERROR_CHECK(core_event_bus_init(&s_core.events, 24));
    ESP_ERROR_CHECK(init_hardware());
#if CONFIG_HANDHELD_SD_MOUNT_ON_BOOT
    esp_err_t sd_err = storage_mount_sd();
    if (sd_err != ESP_OK) {
        ESP_LOGW(TAG, "SD mount on boot failed: %s", esp_err_to_name(sd_err));
    }
#endif
    ESP_ERROR_CHECK(apps_register_all(&s_core));
    ESP_ERROR_CHECK(apps_show_boot(&s_core));
    ESP_ERROR_CHECK(battery_start());
    ESP_ERROR_CHECK(joystick_start());

    int64_t last_us = esp_timer_get_time();
    while (true) {
        core_event_t event;
        uint32_t wait_ms = s_core.low_power_mode ? 50 : 16;
        if (core_event_bus_receive(&s_core.events, &event, pdMS_TO_TICKS(wait_ms))) {
            handle_event(&s_core, &event);
        }

        int64_t now_us = esp_timer_get_time();
        uint32_t dt_ms = (uint32_t)((now_us - last_us) / 1000);
        if (dt_ms == 0) {
            dt_ms = 1;
        }
        last_us = now_us;

        core_screen_manager_update(&s_core, dt_ms);
        core_screen_manager_render(&s_core);
        s_core.fps_elapsed_ms += dt_ms;
        if (s_core.fps_elapsed_ms >= 1000) {
            uint16_t fps = (uint16_t)((s_core.fps_frame_count * 1000U + s_core.fps_elapsed_ms / 2U) /
                                      s_core.fps_elapsed_ms);
            s_core.fps_frame_count = 0;
            s_core.fps_elapsed_ms = 0;
            ui_set_fps(&s_ui, fps);
        }
    }
}
