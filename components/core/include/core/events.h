#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "core/input.h"

typedef enum {
    CORE_EVENT_INPUT = 0,
    CORE_EVENT_BATTERY,
    CORE_EVENT_STORAGE,
    CORE_EVENT_APP,
    CORE_EVENT_TICK,
} core_event_type_t;

typedef struct {
    uint8_t percentage;
    uint16_t millivolts;
    bool low;
} core_battery_event_t;

typedef struct {
    core_event_type_t type;
    union {
        core_input_event_t input;
        core_battery_event_t battery;
        uint32_t app_code;
    };
} core_event_t;

typedef struct {
    QueueHandle_t queue;
} core_event_bus_t;

esp_err_t core_event_bus_init(core_event_bus_t *bus, uint32_t depth);
esp_err_t core_event_bus_publish(core_event_bus_t *bus, const core_event_t *event, TickType_t timeout);
bool core_event_bus_receive(core_event_bus_t *bus, core_event_t *event, TickType_t timeout);
