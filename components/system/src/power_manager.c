#include "system/power_manager.h"

#include <stdbool.h>
#include "esp_bit_defs.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"

static const char *TAG = "power";

#define WAKE_GATE_MAGIC 0xC6A55A3CU

typedef struct {
    power_manager_config_t config;
    int64_t last_activity_us;
    bool initialized;
    TaskHandle_t sleep_task;
} power_state_t;

static power_state_t s_power;
static RTC_DATA_ATTR uint32_t s_wake_gate_magic;
static RTC_DATA_ATTR uint8_t s_wake_gate_clicks;

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

static esp_err_t configure_wake_button(gpio_num_t wake_gpio)
{
    gpio_config_t cfg = {
        .pin_bit_mask = BIT64(wake_gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

static bool wake_button_pressed(gpio_num_t wake_gpio)
{
    return gpio_get_level(wake_gpio) == 0;
}

static const char *wakeup_cause_name(esp_sleep_wakeup_cause_t cause)
{
    switch (cause) {
    case ESP_SLEEP_WAKEUP_UNDEFINED: return "undefined";
    case ESP_SLEEP_WAKEUP_EXT0: return "ext0";
    case ESP_SLEEP_WAKEUP_EXT1: return "ext1";
    case ESP_SLEEP_WAKEUP_TIMER: return "timer";
    case ESP_SLEEP_WAKEUP_GPIO: return "gpio";
    case ESP_SLEEP_WAKEUP_UART: return "uart";
    default: return "other";
    }
}

static const char *reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_UNKNOWN: return "unknown";
    case ESP_RST_POWERON: return "poweron";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_USB: return "usb";
    case ESP_RST_JTAG: return "jtag";
    case ESP_RST_PWR_GLITCH: return "power_glitch";
    default: return "other";
    }
}

static void wait_wake_button_idle_before_sleep(gpio_num_t wake_gpio)
{
    if (!wake_button_pressed(wake_gpio)) {
        vTaskDelay(pdMS_TO_TICKS(300));
        return;
    }

    ESP_LOGI(TAG, "wake gpio is active, waiting for release before deep sleep");
    while (wake_button_pressed(wake_gpio)) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelay(pdMS_TO_TICKS(300));
}

static void wake_gate_clear(void)
{
    s_wake_gate_magic = 0;
    s_wake_gate_clicks = 0;
}

static void enter_deep_sleep_internal(gpio_num_t wake_gpio) __attribute__((noreturn));

void power_manager_handle_deep_sleep_wakeup_gate(gpio_num_t wake_gpio, uint8_t required_clicks,
                                                 uint32_t window_ms)
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    esp_reset_reason_t reset_reason = esp_reset_reason();
    (void)configure_wake_button(wake_gpio);
    ESP_LOGI(TAG, "reset=%s wakeup=%s ext1=0x%llx gpio=0x%llx level=%d",
             reset_reason_name(reset_reason),
             wakeup_cause_name(cause),
             (unsigned long long)esp_sleep_get_ext1_wakeup_status(),
             (unsigned long long)esp_sleep_get_gpio_wakeup_status(),
             gpio_get_level(wake_gpio));

    if (s_wake_gate_magic != WAKE_GATE_MAGIC) {
        return;
    }

    if (required_clicks < 2) {
        wake_gate_clear();
        return;
    }
    if (window_ms < 500) {
        window_ms = 500;
    }

    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        ESP_LOGI(TAG, "wake gate expired at %u/%u clicks", s_wake_gate_clicks, required_clicks);
        wake_gate_clear();
        enter_deep_sleep_internal(wake_gpio);
    }

    if (cause != ESP_SLEEP_WAKEUP_GPIO && cause != ESP_SLEEP_WAKEUP_EXT0 && cause != ESP_SLEEP_WAKEUP_EXT1) {
        wake_gate_clear();
        return;
    }

    if (configure_wake_button(wake_gpio) != ESP_OK) {
        ESP_LOGW(TAG, "wake gate gpio config failed");
        return;
    }

    wait_wake_button_idle_before_sleep(wake_gpio);
    if (s_wake_gate_clicks < required_clicks) {
        ++s_wake_gate_clicks;
    }

    ESP_LOGI(TAG, "deep sleep wake gate: click %u/%u", s_wake_gate_clicks, required_clicks);
    if (s_wake_gate_clicks >= required_clicks) {
        wake_gate_clear();
        ESP_LOGI(TAG, "wake gate accepted");
        return;
    }

    (void)window_ms;
    enter_deep_sleep_internal(wake_gpio);
}

static void deep_sleep_task(void *arg)
{
    gpio_num_t wake_gpio = (gpio_num_t)(intptr_t)arg;
    vTaskDelay(pdMS_TO_TICKS(250));
    power_manager_enter_deep_sleep(wake_gpio);
}

esp_err_t power_manager_request_deep_sleep(gpio_num_t wake_gpio)
{
    if (s_power.sleep_task != NULL) {
        return ESP_OK;
    }
    s_wake_gate_magic = WAKE_GATE_MAGIC;
    s_wake_gate_clicks = 0;
    BaseType_t ok = xTaskCreate(deep_sleep_task, "deep_sleep", 3072, (void *)(intptr_t)wake_gpio, 10,
                                &s_power.sleep_task);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void power_manager_enter_deep_sleep(gpio_num_t wake_gpio)
{
    enter_deep_sleep_internal(wake_gpio);
}

static void enter_deep_sleep_internal(gpio_num_t wake_gpio)
{
    ESP_LOGI(TAG, "entering deep sleep, wake gpio=%d active-low", wake_gpio);

    esp_err_t err = configure_wake_button(wake_gpio);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wake gpio config failed: %s", esp_err_to_name(err));
    }
    wait_wake_button_idle_before_sleep(wake_gpio);
    ESP_LOGI(TAG, "wake gpio idle level before sleep=%d", gpio_get_level(wake_gpio));

    (void)gpio_set_pull_mode(wake_gpio, GPIO_PULLUP_ONLY);
    (void)gpio_sleep_set_direction(wake_gpio, GPIO_MODE_INPUT);
    (void)gpio_sleep_set_pull_mode(wake_gpio, GPIO_PULLUP_ONLY);
    (void)gpio_sleep_sel_en(wake_gpio);

    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    err = esp_deep_sleep_enable_gpio_wakeup(BIT64(wake_gpio), ESP_GPIO_WAKEUP_GPIO_LOW);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "enable wakeup failed: %s", esp_err_to_name(err));
    }

    vTaskDelay(pdMS_TO_TICKS(30));
    esp_deep_sleep_start();
    __builtin_unreachable();
}
