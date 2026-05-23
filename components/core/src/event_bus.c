#include "core/events.h"

#include "esp_check.h"

static const char *TAG = "event_bus";

esp_err_t core_event_bus_init(core_event_bus_t *bus, uint32_t depth)
{
    ESP_RETURN_ON_FALSE(bus != NULL && depth > 0, ESP_ERR_INVALID_ARG, TAG, "bad args");
    bus->queue = xQueueCreate(depth, sizeof(core_event_t));
    return bus->queue != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t core_event_bus_publish(core_event_bus_t *bus, const core_event_t *event, TickType_t timeout)
{
    ESP_RETURN_ON_FALSE(bus != NULL && bus->queue != NULL && event != NULL, ESP_ERR_INVALID_ARG, TAG, "bad args");
    return xQueueSend(bus->queue, event, timeout) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool core_event_bus_receive(core_event_bus_t *bus, core_event_t *event, TickType_t timeout)
{
    if (bus == NULL || bus->queue == NULL || event == NULL) {
        return false;
    }
    return xQueueReceive(bus->queue, event, timeout) == pdTRUE;
}
