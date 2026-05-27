#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t i2c_bus_get_master(int i2c_port, gpio_num_t sda, gpio_num_t scl, i2c_master_bus_handle_t *bus);
