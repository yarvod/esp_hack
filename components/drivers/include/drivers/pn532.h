#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#define PN532_UID_MAX_LEN 10

typedef enum {
    PN532_BUS_I2C,
    PN532_BUS_SPI
} pn532_bus_type_t;

typedef struct {
    pn532_bus_type_t bus_type;
    union {
        struct {
            int port;
            uint8_t address;
        } i2c;
        struct {
            spi_device_handle_t spi_dev;
            gpio_num_t cs_pin;
            spi_transaction_t transaction;
            uint8_t tx_buffer[128];
            uint8_t rx_buffer[128];
        } spi;
    };
    bool initialized;
} pn532_t;

typedef struct {
    uint8_t uid[PN532_UID_MAX_LEN];
    uint8_t uid_len;
    uint16_t atqa;
    uint8_t sak;
} pn532_target_t;

/**
 * @brief Initialize PN532 using SPI
 */
esp_err_t pn532_init_spi(pn532_t *pn532, spi_host_device_t host, gpio_num_t cs, gpio_num_t sck, gpio_num_t mosi, gpio_num_t miso);

/**
 * @brief Read a passive target (ISO14443A)
 */
esp_err_t pn532_read_passive_target(pn532_t *pn532, pn532_target_t *target, uint32_t timeout_ms);

/**
 * @brief Start emulating a UID as an ISO14443A tag
 */
esp_err_t pn532_emulate_uid(pn532_t *pn532, const uint8_t *uid, size_t uid_len);

/**
 * @brief Get PN532 firmware version
 */
esp_err_t pn532_get_firmware_version(pn532_t *pn532, uint32_t *version);
