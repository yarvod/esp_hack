#include "drivers/sdcard.h"

#include <string.h>
#include <stdio.h>
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_vfs_fat.h"

static const char *TAG = "sdcard";

typedef struct {
    sdcard_config_t config;
    sdmmc_card_t *card;
    bool mounted;
    bool bus_initialized;
} sdcard_state_t;

static sdcard_state_t s_sd;

static void probe_delay(void)
{
    esp_rom_delay_us(3);
}

static uint8_t probe_transfer_byte(const sdcard_config_t *config, uint8_t out)
{
    uint8_t in = 0;
    for (int bit = 7; bit >= 0; --bit) {
        gpio_set_level(config->clk, 0);
        gpio_set_level(config->mosi, (out >> bit) & 0x01);
        probe_delay();
        gpio_set_level(config->clk, 1);
        probe_delay();
        in = (uint8_t)((in << 1) | (gpio_get_level(config->miso) & 0x01));
    }
    gpio_set_level(config->clk, 0);
    return in;
}

static uint8_t probe_command(const sdcard_config_t *config, uint8_t cmd, uint32_t arg, uint8_t crc,
                             uint8_t *extra, size_t extra_len)
{
    gpio_set_level(config->cs, 0);
    probe_transfer_byte(config, 0x40 | cmd);
    probe_transfer_byte(config, (uint8_t)(arg >> 24));
    probe_transfer_byte(config, (uint8_t)(arg >> 16));
    probe_transfer_byte(config, (uint8_t)(arg >> 8));
    probe_transfer_byte(config, (uint8_t)arg);
    probe_transfer_byte(config, crc);

    uint8_t response = 0xFF;
    for (int i = 0; i < 16; ++i) {
        response = probe_transfer_byte(config, 0xFF);
        if ((response & 0x80) == 0) {
            break;
        }
    }

    for (size_t i = 0; i < extra_len; ++i) {
        extra[i] = probe_transfer_byte(config, 0xFF);
    }
    gpio_set_level(config->cs, 1);
    probe_transfer_byte(config, 0xFF);
    return response;
}

static void probe_card_spi(const sdcard_config_t *config)
{
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << config->cs) | (1ULL << config->mosi) | (1ULL << config->clk),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config_t in_cfg = {
        .pin_bit_mask = 1ULL << config->miso,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_cfg);
    gpio_config(&in_cfg);

    gpio_set_level(config->cs, 1);
    gpio_set_level(config->mosi, 1);
    gpio_set_level(config->clk, 0);
    for (int i = 0; i < 10; ++i) {
        probe_transfer_byte(config, 0xFF);
    }

    uint8_t cmd0 = probe_command(config, 0, 0x00000000, 0x95, NULL, 0);
    uint8_t r7[4] = {0};
    uint8_t cmd8 = probe_command(config, 8, 0x000001AA, 0x87, r7, sizeof(r7));
    ESP_LOGI(TAG, "spi probe cmd0=0x%02x cmd8=0x%02x r7=%02x %02x %02x %02x miso_idle=%d",
             cmd0, cmd8, r7[0], r7[1], r7[2], r7[3], gpio_get_level(config->miso));
}

static void configure_line_pullups(const sdcard_config_t *config)
{
    const gpio_num_t pins[] = {
        config->cs,
        config->mosi,
        config->miso,
        config->clk,
    };

    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        gpio_reset_pin(pins[i]);
        gpio_set_pull_mode(pins[i], GPIO_PULLUP_ONLY);
    }
    gpio_set_direction(config->cs, GPIO_MODE_OUTPUT);
    gpio_set_level(config->cs, 1);
}

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
        s_sd.config.max_freq_khz = 400;
    }

    configure_line_pullups(&s_sd.config);
    ESP_LOGI(TAG, "mount start cs=%d mosi=%d miso=%d clk=%d freq=%luKHz",
             s_sd.config.cs, s_sd.config.mosi, s_sd.config.miso, s_sd.config.clk,
             (unsigned long)s_sd.config.max_freq_khz);
    probe_card_spi(&s_sd.config);
    configure_line_pullups(&s_sd.config);

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = s_sd.config.mosi,
        .miso_io_num = s_sd.config.miso,
        .sclk_io_num = s_sd.config.clk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    bool bus_started_here = err == ESP_OK;
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
        if (bus_started_here) {
            spi_bus_free(SPI2_HOST);
            s_sd.bus_initialized = false;
        }
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
