#include "drivers/sdcard.h"

#include <string.h>
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"

static const char *TAG = "sdcard";

typedef struct {
    sdcard_config_t config;
    sdmmc_card_t *card;
    bool mounted;
    bool bus_initialized;
} sdcard_state_t;

static sdcard_state_t s_sd;

esp_err_t sdcard_mount(const sdcard_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is null");
    if (s_sd.mounted) {
        return ESP_OK;
    }

    memset(&s_sd, 0, sizeof(s_sd));
    s_sd.config = *config;
    if (s_sd.config.mount_point == NULL) {
        s_sd.config.mount_point = "/sdcard";
    }
    if (s_sd.config.max_freq_khz == 0) {
        s_sd.config.max_freq_khz = 4000;
    }

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = s_sd.config.mosi,
        .miso_io_num = s_sd.config.miso,
        .sclk_io_num = s_sd.config.clk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi bus init failed: %s", esp_err_to_name(err));
        return err;
    }
    s_sd.bus_initialized = true;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = s_sd.config.max_freq_khz;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = s_sd.config.cs;
    slot_config.host_id = host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = s_sd.config.format_if_mount_failed,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    err = esp_vfs_fat_sdspi_mount(s_sd.config.mount_point, &host, &slot_config, &mount_config, &s_sd.card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mount failed at %s: %s", s_sd.config.mount_point, esp_err_to_name(err));
        return err;
    }

    s_sd.mounted = true;
    sdmmc_card_print_info(stdout, s_sd.card);
    ESP_LOGI(TAG, "mounted at %s", s_sd.config.mount_point);
    return ESP_OK;
}

esp_err_t sdcard_unmount(void)
{
    if (!s_sd.mounted) {
        return ESP_OK;
    }
    esp_err_t err = esp_vfs_fat_sdcard_unmount(s_sd.config.mount_point, s_sd.card);
    if (err == ESP_OK) {
        s_sd.card = NULL;
        s_sd.mounted = false;
        if (s_sd.bus_initialized) {
            spi_bus_free(SPI2_HOST);
            s_sd.bus_initialized = false;
        }
    }
    return err;
}

bool sdcard_is_mounted(void)
{
    return s_sd.mounted;
}

const char *sdcard_mount_point(void)
{
    return s_sd.config.mount_point != NULL ? s_sd.config.mount_point : "/sdcard";
}

sdmmc_card_t *sdcard_get_card(void)
{
    return s_sd.card;
}
