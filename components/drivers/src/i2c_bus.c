#include "drivers/i2c_bus.h"

#include <string.h>
#include "esp_check.h"

static const char *TAG = "i2c_bus";

typedef struct {
    bool initialized;
    int port;
    gpio_num_t sda;
    gpio_num_t scl;
    i2c_master_bus_handle_t bus;
} i2c_bus_slot_t;

static i2c_bus_slot_t s_buses[2];

esp_err_t i2c_bus_get_master(int i2c_port, gpio_num_t sda, gpio_num_t scl, i2c_master_bus_handle_t *bus)
{
    ESP_RETURN_ON_FALSE(bus != NULL, ESP_ERR_INVALID_ARG, TAG, "bus out is null");
    ESP_RETURN_ON_FALSE(i2c_port >= 0 && i2c_port < (int)(sizeof(s_buses) / sizeof(s_buses[0])),
                        ESP_ERR_INVALID_ARG, TAG, "invalid i2c port");

    i2c_bus_slot_t *slot = &s_buses[i2c_port];
    if (slot->initialized) {
        ESP_RETURN_ON_FALSE(slot->sda == sda && slot->scl == scl, ESP_ERR_INVALID_STATE,
                            TAG, "i2c port already uses different pins");
        *bus = slot->bus;
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = i2c_port,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &slot->bus), TAG, "i2c bus init failed");
    slot->initialized = true;
    slot->port = i2c_port;
    slot->sda = sda;
    slot->scl = scl;
    *bus = slot->bus;
    return ESP_OK;
}
