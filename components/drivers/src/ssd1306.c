#include "drivers/ssd1306.h"

#include <string.h>
#include "drivers/i2c_bus.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "ssd1306";

#define SSD1306_I2C_ADDR 0x3C
#define SSD1306_CONTROL_CMD 0x00
#define SSD1306_CONTROL_DATA 0x40
#define SSD1306_I2C_CLOCK_HZ 1000000
#define SSD1306_I2C_CHUNK_SIZE 256

static esp_err_t write_bytes(ssd1306_t *display, uint8_t control, const uint8_t *data, size_t len)
{
    uint8_t packet[SSD1306_I2C_CHUNK_SIZE + 1];
    ESP_RETURN_ON_FALSE(len <= SSD1306_I2C_CHUNK_SIZE, ESP_ERR_INVALID_SIZE, TAG, "packet too large");
    packet[0] = control;
    memcpy(&packet[1], data, len);
    esp_err_t err = i2c_master_transmit(display->device, packet, len + 1, 100);
    if (err != ESP_OK) {
        err = i2c_master_transmit(display->device, packet, len + 1, 100);
    }
    return err;
}

static esp_err_t command(ssd1306_t *display, uint8_t cmd)
{
    return write_bytes(display, SSD1306_CONTROL_CMD, &cmd, 1);
}

esp_err_t ssd1306_init(ssd1306_t *display, int i2c_port, gpio_num_t sda, gpio_num_t scl)
{
    ESP_RETURN_ON_FALSE(display != NULL, ESP_ERR_INVALID_ARG, TAG, "display is null");
    memset(display, 0, sizeof(*display));
    display->i2c_port = i2c_port;
    display->sda = sda;
    display->scl = scl;
    display->address = SSD1306_I2C_ADDR;
    display->clock_hz = SSD1306_I2C_CLOCK_HZ;

    ESP_RETURN_ON_ERROR(i2c_bus_get_master(i2c_port, sda, scl, &display->bus), TAG, "i2c bus init failed");

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = display->address,
        .scl_speed_hz = display->clock_hz,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(display->bus, &dev_config, &display->device), TAG, "i2c device add failed");

    const uint8_t init[] = {
        0xAE, 0x20, 0x00, 0xB0, 0xC8, 0x00, 0x10, 0x40,
        0x81, 0x7F, 0xA1, 0xA6, 0xA8, 0x3F, 0xA4, 0xD3,
        0x00, 0xD5, 0x80, 0xD9, 0xF1, 0xDA, 0x12, 0xDB,
        0x40, 0x8D, 0x14, 0xAF,
    };
    for (size_t i = 0; i < sizeof(init); ++i) {
        ESP_RETURN_ON_ERROR(command(display, init[i]), TAG, "init command failed");
    }
    display->initialized = true;
    ESP_RETURN_ON_ERROR(ssd1306_clear(display), TAG, "clear failed");
    ESP_RETURN_ON_ERROR(ssd1306_flush(display), TAG, "flush failed");
    ESP_LOGI(TAG, "initialized on i2c%d sda=%d scl=%d addr=0x%02x", i2c_port, sda, scl, display->address);
    return ESP_OK;
}

esp_err_t ssd1306_clear(ssd1306_t *display)
{
    ESP_RETURN_ON_FALSE(display != NULL, ESP_ERR_INVALID_ARG, TAG, "display is null");
    memset(display->buffer, 0, sizeof(display->buffer));
    return ESP_OK;
}

esp_err_t ssd1306_flush(ssd1306_t *display)
{
    ESP_RETURN_ON_FALSE(display != NULL && display->initialized, ESP_ERR_INVALID_STATE, TAG, "display not initialized");

    ESP_RETURN_ON_ERROR(command(display, 0x21), TAG, "set col cmd failed");
    ESP_RETURN_ON_ERROR(command(display, 0), TAG, "set col start failed");
    ESP_RETURN_ON_ERROR(command(display, SSD1306_WIDTH - 1), TAG, "set col end failed");
    ESP_RETURN_ON_ERROR(command(display, 0x22), TAG, "set page cmd failed");
    ESP_RETURN_ON_ERROR(command(display, 0), TAG, "set page start failed");
    ESP_RETURN_ON_ERROR(command(display, 7), TAG, "set page end failed");

    for (size_t offset = 0; offset < SSD1306_BUFFER_SIZE; offset += SSD1306_I2C_CHUNK_SIZE) {
        ESP_RETURN_ON_ERROR(write_bytes(display, SSD1306_CONTROL_DATA, &display->buffer[offset], SSD1306_I2C_CHUNK_SIZE),
                            TAG, "data write failed");
    }
    return ESP_OK;
}

esp_err_t ssd1306_set_display_on(ssd1306_t *display, bool on)
{
    ESP_RETURN_ON_FALSE(display != NULL && display->initialized, ESP_ERR_INVALID_STATE, TAG, "display not initialized");
    return command(display, on ? 0xAF : 0xAE);
}

void ssd1306_draw_pixel(ssd1306_t *display, int x, int y, bool color)
{
    if (display == NULL || x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) {
        return;
    }
    uint16_t index = (uint16_t)x + (uint16_t)(y / 8) * SSD1306_WIDTH;
    uint8_t mask = 1U << (y & 7);
    if (color) {
        display->buffer[index] |= mask;
    } else {
        display->buffer[index] &= (uint8_t)~mask;
    }
}

void ssd1306_invert_region(ssd1306_t *display, int x, int y, int w, int h)
{
    if (display == NULL) {
        return;
    }
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            if (xx >= 0 && xx < SSD1306_WIDTH && yy >= 0 && yy < SSD1306_HEIGHT) {
                uint16_t index = (uint16_t)xx + (uint16_t)(yy / 8) * SSD1306_WIDTH;
                display->buffer[index] ^= (uint8_t)(1U << (yy & 7));
            }
        }
    }
}
