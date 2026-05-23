#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

#define SSD1306_WIDTH 128
#define SSD1306_HEIGHT 64
#define SSD1306_BUFFER_SIZE (SSD1306_WIDTH * SSD1306_HEIGHT / 8)

typedef struct {
    int i2c_port;
    gpio_num_t sda;
    gpio_num_t scl;
    uint8_t address;
    uint32_t clock_hz;
    uint8_t buffer[SSD1306_BUFFER_SIZE];
    bool initialized;
} ssd1306_t;

esp_err_t ssd1306_init(ssd1306_t *display, int i2c_port, gpio_num_t sda, gpio_num_t scl);
esp_err_t ssd1306_clear(ssd1306_t *display);
esp_err_t ssd1306_flush(ssd1306_t *display);
void ssd1306_draw_pixel(ssd1306_t *display, int x, int y, bool color);
void ssd1306_invert_region(ssd1306_t *display, int x, int y, int w, int h);
