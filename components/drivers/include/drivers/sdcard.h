#pragma once

#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "sdmmc_cmd.h"

typedef struct {
    gpio_num_t cs;
    gpio_num_t mosi;
    gpio_num_t clk;
    gpio_num_t miso;
    const char *mount_point;
    uint32_t max_freq_khz;
    bool format_if_mount_failed;
} sdcard_config_t;

esp_err_t sdcard_mount(const sdcard_config_t *config);
esp_err_t sdcard_unmount(void);
bool sdcard_is_mounted(void);
const char *sdcard_mount_point(void);
sdmmc_card_t *sdcard_get_card(void);
