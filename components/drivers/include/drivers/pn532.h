#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#define PN532_UID_MAX_LEN 10

typedef struct {
    int i2c_port;
    gpio_num_t sda;
    gpio_num_t scl;
    uint8_t address;
    uint32_t clock_hz;
    i2c_master_dev_handle_t device;
    bool initialized;
} pn532_t;

typedef struct {
    uint8_t uid[PN532_UID_MAX_LEN];
    uint8_t uid_len;
    uint16_t atqa;
    uint8_t sak;
} pn532_target_t;

esp_err_t pn532_init_i2c(pn532_t *pn532, int i2c_port, gpio_num_t sda, gpio_num_t scl);
esp_err_t pn532_read_passive_target(pn532_t *pn532, pn532_target_t *target, uint32_t timeout_ms);
esp_err_t pn532_emulate_uid(pn532_t *pn532, const uint8_t *uid, size_t uid_len);
