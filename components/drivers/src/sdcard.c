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

#define SDCARD_POWERUP_DELAY_MS 500
#define SDCARD_STARTUP_CLOCK_BYTES 16
#define SDCARD_WIRE_TEST_ENABLED 1

typedef struct {
    sdcard_config_t config;
    sdmmc_card_t *card;
    bool mounted;
    bool bus_initialized;
} sdcard_state_t;

typedef struct {
    uint8_t cmd0;
    uint8_t cmd8;
    uint8_t r7[4];
    int miso_idle;
    uint8_t cmd0_attempts;
} sdcard_probe_result_t;

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

static sdcard_probe_result_t probe_card_spi(const sdcard_config_t *config)
{
    for (int i = 0; i < SDCARD_STARTUP_CLOCK_BYTES; ++i) {
        probe_transfer_byte(config, 0xFF);
    }

    sdcard_probe_result_t result = {
        .cmd0 = 0xFF,
        .cmd8 = 0xFF,
    };
    for (uint8_t attempt = 1; attempt <= 5; ++attempt) {
        result.cmd0_attempts = attempt;
        result.cmd0 = probe_command(config, 0, 0x00000000, 0x95, NULL, 0);
        if (result.cmd0 != 0xFF) {
            break;
        }
        for (int i = 0; i < SDCARD_STARTUP_CLOCK_BYTES; ++i) {
            probe_transfer_byte(config, 0xFF);
        }
        esp_rom_delay_us(20 * 1000);
    }
    result.cmd8 = probe_command(config, 8, 0x000001AA, 0x87, result.r7, sizeof(result.r7));
    result.miso_idle = gpio_get_level(config->miso);
    return result;
}

static void configure_line_pullups(const sdcard_config_t *config)
{
    const uint64_t out_mask = (1ULL << config->cs) | (1ULL << config->mosi) | (1ULL << config->clk);
    const uint64_t in_mask = 1ULL << config->miso;

    gpio_config_t out_cfg = {
        .pin_bit_mask = out_mask,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config_t in_cfg = {
        .pin_bit_mask = in_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&out_cfg);
    gpio_config(&in_cfg);

    gpio_set_pull_mode(config->cs, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(config->mosi, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(config->miso, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(config->clk, GPIO_PULLUP_ONLY);

    gpio_set_drive_capability(config->cs, GPIO_DRIVE_CAP_2);
    gpio_set_drive_capability(config->mosi, GPIO_DRIVE_CAP_2);
    gpio_set_drive_capability(config->clk, GPIO_DRIVE_CAP_2);

    gpio_set_level(config->cs, 1);
    gpio_set_level(config->mosi, 1);
    gpio_set_level(config->clk, 0);
}

static void log_line_levels(const sdcard_config_t *config, const char *stage)
{
    ESP_LOGW(TAG, "%s levels cs=%d mosi=%d miso=%d clk=%d",
             stage,
             gpio_get_level(config->cs),
             gpio_get_level(config->mosi),
             gpio_get_level(config->miso),
             gpio_get_level(config->clk));
}

static void log_miso_pull_probe(const sdcard_config_t *config)
{
    gpio_set_direction(config->miso, GPIO_MODE_INPUT);

    gpio_set_pull_mode(config->miso, GPIO_PULLDOWN_ONLY);
    esp_rom_delay_us(200);
    int pulldown = gpio_get_level(config->miso);

    gpio_set_pull_mode(config->miso, GPIO_PULLUP_ONLY);
    esp_rom_delay_us(200);
    int pullup = gpio_get_level(config->miso);

    ESP_LOGW(TAG, "miso pull probe pulldown=%d pullup=%d", pulldown, pullup);
    if (pulldown == 0 && pullup == 1) {
        ESP_LOGW(TAG, "miso looks floating; check MISO wire/card/module or add external 10k pull-up");
    }
}

static void run_wire_test(const sdcard_config_t *config)
{
#if SDCARD_WIRE_TEST_ENABLED
    ESP_LOGW(TAG, "wire test start: each output line toggles alone, 2 seconds per level");
    ESP_LOGW(TAG, "measure on SD module pads with black probe on GND");
    gpio_set_level(config->cs, 1);
    gpio_set_level(config->mosi, 1);
    gpio_set_level(config->clk, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));

    const struct {
        const char *name;
        gpio_num_t gpio;
        int idle_level;
    } lines[] = {
        {"CS", config->cs, 1},
        {"MOSI", config->mosi, 1},
        {"CLK", config->clk, 0},
    };

    for (size_t line = 0; line < sizeof(lines) / sizeof(lines[0]); ++line) {
        ESP_LOGW(TAG, "wire test %s: LOW for 2s", lines[line].name);
        gpio_set_level(lines[line].gpio, 0);
        log_line_levels(config, "wire test readback low");
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGW(TAG, "wire test %s: HIGH for 2s", lines[line].name);
        gpio_set_level(lines[line].gpio, 1);
        log_line_levels(config, "wire test readback high");
        vTaskDelay(pdMS_TO_TICKS(2000));

        gpio_set_level(lines[line].gpio, lines[line].idle_level);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    gpio_set_level(config->cs, 1);
    gpio_set_level(config->mosi, 1);
    gpio_set_level(config->clk, 0);
    ESP_LOGW(TAG, "wire test done");
#endif
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

    log_miso_pull_probe(&s_sd.config);
    configure_line_pullups(&s_sd.config);
    log_line_levels(&s_sd.config, "after pullups");
    run_wire_test(&s_sd.config);
    esp_rom_delay_us(SDCARD_POWERUP_DELAY_MS * 1000);

    ESP_LOGW(TAG, "mount start cs=%d mosi=%d miso=%d clk=%d freq=%luKHz",
             s_sd.config.cs, s_sd.config.mosi, s_sd.config.miso, s_sd.config.clk,
             (unsigned long)s_sd.config.max_freq_khz);
    sdcard_probe_result_t probe = probe_card_spi(&s_sd.config);
    ESP_LOGW(TAG, "spi probe cmd0=0x%02x attempts=%u cmd8=0x%02x r7=%02x %02x %02x %02x miso_idle=%d",
             probe.cmd0, probe.cmd0_attempts,
             probe.cmd8, probe.r7[0], probe.r7[1], probe.r7[2], probe.r7[3],
             probe.miso_idle);
    if (probe.cmd0 == 0xFF) {
        ESP_LOGW(TAG, "card did not answer CMD0; this is a physical/SPI wiring or module power-level issue, not FAT32");
        log_line_levels(&s_sd.config, "after cmd0 timeout");
    }
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
