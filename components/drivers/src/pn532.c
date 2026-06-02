#include "drivers/pn532.h"

#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "pn532";

#define PN532_SPI_STATREAD  0x02
#define PN532_SPI_DATAWRITE 0x01
#define PN532_SPI_DATAREAD  0x03
#define PN532_SPI_READY     0x01

#define PN532_HOST_TO_PN532 0xD4
#define PN532_PN532_TO_HOST 0xD5

#define PN532_CMD_GETFIRMWAREVERSION 0x02
#define PN532_CMD_SAMCONFIGURATION    0x14
#define PN532_CMD_INLISTPASSIVETARGET 0x4A
#define PN532_CMD_TGINITASTARGET      0x8C

#define PN532_FRAME_MAX 96
#define PN532_SPI_CLOCK_HZ 100000

static uint8_t checksum(const uint8_t *data, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum = (uint8_t)(sum + data[i]);
    }
    return (uint8_t)(~sum + 1);
}

static uint8_t sum_bytes(const uint8_t *data, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum = (uint8_t)(sum + data[i]);
    }
    return sum;
}

static void select_device(pn532_t *pn532)
{
    gpio_set_level(pn532->spi.cs_pin, 0);
    esp_rom_delay_us(100);
}

static void deselect_device(pn532_t *pn532)
{
    gpio_set_level(pn532->spi.cs_pin, 1);
}

static void pn532_spi_pre_transfer(spi_transaction_t *transaction)
{
    pn532_t *pn532 = (pn532_t *)transaction->user;
    if (pn532 != NULL) {
        select_device(pn532);
    }
}

static void pn532_spi_post_transfer(spi_transaction_t *transaction)
{
    pn532_t *pn532 = (pn532_t *)transaction->user;
    if (pn532 != NULL) {
        deselect_device(pn532);
    }
}

static esp_err_t spi_command_write(pn532_t *pn532, uint8_t command, const uint8_t *data, size_t len)
{
    ESP_RETURN_ON_FALSE(len <= sizeof(pn532->spi.tx_buffer), ESP_ERR_INVALID_SIZE, TAG, "spi write too large");
    memcpy(pn532->spi.tx_buffer, data, len);

    memset(&pn532->spi.transaction, 0, sizeof(pn532->spi.transaction));
    pn532->spi.transaction.cmd = command;
    pn532->spi.transaction.length = len * 8;
    pn532->spi.transaction.tx_buffer = pn532->spi.tx_buffer;
    pn532->spi.transaction.user = pn532;

    return spi_device_transmit(pn532->spi.spi_dev, &pn532->spi.transaction);
}

static esp_err_t spi_command_read(pn532_t *pn532, uint8_t command, uint8_t *data, size_t len)
{
    ESP_RETURN_ON_FALSE(len <= sizeof(pn532->spi.rx_buffer), ESP_ERR_INVALID_SIZE, TAG, "spi read too large");

    memset(&pn532->spi.transaction, 0, sizeof(pn532->spi.transaction));
    pn532->spi.transaction.cmd = command;
    pn532->spi.transaction.rxlength = len * 8;
    pn532->spi.transaction.rx_buffer = pn532->spi.rx_buffer;
    pn532->spi.transaction.user = pn532;

    esp_err_t err = spi_device_transmit(pn532->spi.spi_dev, &pn532->spi.transaction);
    if (err == ESP_OK) {
        memcpy(data, pn532->spi.rx_buffer, len);
    }
    return err;
}

static esp_err_t cleanup_failed_init(pn532_t *pn532, bool bus_started_here, spi_host_device_t host, esp_err_t err)
{
    if (pn532->spi.spi_dev != NULL) {
        spi_bus_remove_device(pn532->spi.spi_dev);
    }
    if (bus_started_here) {
        spi_bus_free(host);
    }
    memset(pn532, 0, sizeof(*pn532));
    return err;
}

static esp_err_t write_command(pn532_t *pn532, const uint8_t *cmd, size_t cmd_len)
{
    uint8_t frame[PN532_FRAME_MAX];
    ESP_RETURN_ON_FALSE(pn532 != NULL && pn532->initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(cmd != NULL && cmd_len > 0, ESP_ERR_INVALID_ARG, TAG, "empty command");
    ESP_RETURN_ON_FALSE(cmd_len + 8 <= sizeof(frame), ESP_ERR_INVALID_SIZE, TAG, "command too large");

    const uint8_t len = (uint8_t)(cmd_len + 1);
    size_t pos = 0;
    frame[pos++] = 0x00;
    frame[pos++] = 0x00;
    frame[pos++] = 0xFF;
    frame[pos++] = len;
    frame[pos++] = (uint8_t)(~len + 1);
    frame[pos++] = PN532_HOST_TO_PN532;
    memcpy(&frame[pos], cmd, cmd_len);
    pos += cmd_len;
    frame[pos++] = checksum(&frame[5], len);
    frame[pos++] = 0x00;

    return spi_command_write(pn532, PN532_SPI_DATAWRITE, frame, pos);
}

static esp_err_t read_status(pn532_t *pn532, uint8_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG, "status is null");

    memset(&pn532->spi.transaction, 0, sizeof(pn532->spi.transaction));
    pn532->spi.transaction.cmd = PN532_SPI_STATREAD;
    pn532->spi.transaction.rxlength = 8;
    pn532->spi.transaction.flags = SPI_TRANS_USE_RXDATA;
    pn532->spi.transaction.user = pn532;

    esp_err_t err = spi_device_transmit(pn532->spi.spi_dev, &pn532->spi.transaction);
    if (err == ESP_OK) {
        *status = pn532->spi.transaction.rx_data[0];
    }
    return err;
}

static esp_err_t wait_ready(pn532_t *pn532, uint32_t timeout_ms)
{
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    const TickType_t start = xTaskGetTickCount();
    uint8_t last_status = 0;
    esp_err_t last_err = ESP_OK;

    do {
        uint8_t status = 0;
        esp_err_t err = read_status(pn532, &status);
        last_err = err;
        last_status = status;
        if (err == ESP_OK && status == PN532_SPI_READY) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    } while ((xTaskGetTickCount() - start) <= timeout_ticks);

    ESP_LOGW(TAG, "ready timeout after %lu ms last_status=0x%02x last_err=%s",
             (unsigned long)timeout_ms, last_status, esp_err_to_name(last_err));
    return ESP_ERR_TIMEOUT;
}

static esp_err_t read_ack(pn532_t *pn532)
{
    uint8_t ack[6] = {0};
    const uint8_t expected[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};

    ESP_RETURN_ON_ERROR(wait_ready(pn532, 500), TAG, "ack not ready");
    ESP_RETURN_ON_ERROR(spi_command_read(pn532, PN532_SPI_DATAREAD, ack, sizeof(ack)), TAG, "ack read failed");
    ESP_RETURN_ON_FALSE(memcmp(ack, expected, sizeof(expected)) == 0,
                        ESP_ERR_INVALID_RESPONSE, TAG, "bad ack");
    return ESP_OK;
}

static esp_err_t read_frame(pn532_t *pn532, uint8_t *payload, size_t payload_size, size_t *payload_len,
                            uint32_t timeout_ms)
{
    uint8_t frame[PN532_FRAME_MAX] = {0};

    ESP_RETURN_ON_FALSE(payload != NULL && payload_len != NULL, ESP_ERR_INVALID_ARG, TAG, "bad payload args");
    ESP_RETURN_ON_ERROR(wait_ready(pn532, timeout_ms), TAG, "response not ready");
    ESP_RETURN_ON_ERROR(spi_command_read(pn532, PN532_SPI_DATAREAD, frame, sizeof(frame)), TAG, "frame read failed");

    if (frame[0] != 0x00 || frame[1] != 0x00 || frame[2] != 0xFF ||
        (uint8_t)(frame[3] + frame[4]) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const uint8_t len = frame[3];
    const size_t dcs_index = 5U + len;
    const size_t postamble_index = dcs_index + 1U;
    if (len < 2 || postamble_index >= sizeof(frame)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_RETURN_ON_FALSE(frame[5] == PN532_PN532_TO_HOST, ESP_ERR_INVALID_RESPONSE, TAG, "bad tfi");
    ESP_RETURN_ON_FALSE((uint8_t)(sum_bytes(&frame[5], len) + frame[dcs_index]) == 0,
                        ESP_ERR_INVALID_RESPONSE, TAG, "bad data checksum");
    ESP_RETURN_ON_FALSE(frame[postamble_index] == 0x00, ESP_ERR_INVALID_RESPONSE, TAG, "bad postamble");

    const size_t out_len = len - 1;
    ESP_RETURN_ON_FALSE(out_len <= payload_size, ESP_ERR_INVALID_SIZE, TAG, "payload too small");
    memcpy(payload, &frame[6], out_len);
    *payload_len = out_len;
    return ESP_OK;
}

static esp_err_t command_response(pn532_t *pn532, const uint8_t *cmd, size_t cmd_len,
                                  uint8_t expected_response, uint8_t *payload,
                                  size_t payload_size, size_t *payload_len, uint32_t timeout_ms)
{
    ESP_RETURN_ON_ERROR(write_command(pn532, cmd, cmd_len), TAG, "command write failed");
    ESP_RETURN_ON_ERROR(read_ack(pn532), TAG, "ack failed");
    ESP_RETURN_ON_ERROR(read_frame(pn532, payload, payload_size, payload_len, timeout_ms), TAG, "response failed");
    ESP_RETURN_ON_FALSE(*payload_len >= 1 && payload[0] == expected_response,
                        ESP_ERR_INVALID_RESPONSE, TAG, "unexpected response");
    return ESP_OK;
}

esp_err_t pn532_init_spi(pn532_t *pn532, spi_host_device_t host, gpio_num_t cs, gpio_num_t sck,
                         gpio_num_t mosi, gpio_num_t miso)
{
    ESP_RETURN_ON_FALSE(pn532 != NULL, ESP_ERR_INVALID_ARG, TAG, "pn532 is null");
    if (pn532->initialized) {
        return ESP_OK;
    }

    memset(pn532, 0, sizeof(*pn532));
    pn532->bus_type = PN532_BUS_SPI;
    pn532->spi.cs_pin = cs;

    gpio_config_t cs_config = {
        .pin_bit_mask = 1ULL << cs,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cs_config), TAG, "cs gpio config failed");
    deselect_device(pn532);
    vTaskDelay(pdMS_TO_TICKS(2));
    select_device(pn532);
    vTaskDelay(pdMS_TO_TICKS(2));
    deselect_device(pn532);

    spi_bus_config_t bus_config = {
        .miso_io_num = miso,
        .mosi_io_num = mosi,
        .sclk_io_num = sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = PN532_FRAME_MAX,
    };
    esp_err_t err = spi_bus_initialize(host, &bus_config, SPI_DMA_DISABLED);
    const bool bus_started_here = err == ESP_OK;
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    spi_device_interface_config_t dev_config = {
        .command_bits = 8,
        .clock_speed_hz = PN532_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 3,
        .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_BIT_LSBFIRST,
        .pre_cb = pn532_spi_pre_transfer,
        .post_cb = pn532_spi_post_transfer,
    };
    err = spi_bus_add_device(host, &dev_config, &pn532->spi.spi_dev);
    if (err != ESP_OK) {
        return cleanup_failed_init(pn532, bus_started_here, host, err);
    }
    pn532->initialized = true;

    vTaskDelay(pdMS_TO_TICKS(20));

    uint32_t version = 0;
    err = pn532_get_firmware_version(pn532, &version);
    if (err != ESP_OK) {
        return cleanup_failed_init(pn532, bus_started_here, host, err);
    }

    const uint8_t sam[] = { PN532_CMD_SAMCONFIGURATION, 0x01, 0x14, 0x01 };
    uint8_t response[8] = {0};
    size_t response_len = 0;
    err = command_response(pn532, sam, sizeof(sam), PN532_CMD_SAMCONFIGURATION + 1,
                           response, sizeof(response), &response_len, 1000);
    if (err != ESP_OK) {
        return cleanup_failed_init(pn532, bus_started_here, host, err);
    }

    ESP_LOGI(TAG, "initialized on spi host=%d cs=%d sck=%d mosi=%d miso=%d clock=%dHz fw=0x%08lx",
             host, cs, sck, mosi, miso, PN532_SPI_CLOCK_HZ, (unsigned long)version);
    return ESP_OK;
}

esp_err_t pn532_get_firmware_version(pn532_t *pn532, uint32_t *version)
{
    ESP_RETURN_ON_FALSE(version != NULL, ESP_ERR_INVALID_ARG, TAG, "version is null");
    const uint8_t cmd[] = { PN532_CMD_GETFIRMWAREVERSION };
    uint8_t response[8] = {0};
    size_t response_len = 0;

    ESP_RETURN_ON_ERROR(command_response(pn532, cmd, sizeof(cmd), PN532_CMD_GETFIRMWAREVERSION + 1,
                                         response, sizeof(response), &response_len, 1000),
                        TAG, "firmware command failed");
    ESP_RETURN_ON_FALSE(response_len >= 5, ESP_ERR_INVALID_RESPONSE, TAG, "short firmware response");
    *version = ((uint32_t)response[1] << 24) | ((uint32_t)response[2] << 16) |
               ((uint32_t)response[3] << 8) | response[4];
    return ESP_OK;
}

esp_err_t pn532_read_passive_target(pn532_t *pn532, pn532_target_t *target, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(target != NULL, ESP_ERR_INVALID_ARG, TAG, "target is null");
    const uint8_t cmd[] = { PN532_CMD_INLISTPASSIVETARGET, 0x01, 0x00 };
    uint8_t response[32] = {0};
    size_t response_len = 0;

    esp_err_t err = command_response(pn532, cmd, sizeof(cmd), PN532_CMD_INLISTPASSIVETARGET + 1,
                                     response, sizeof(response), &response_len, timeout_ms);
    if (err == ESP_ERR_TIMEOUT) {
        return ESP_ERR_NOT_FOUND;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "inlist failed");

    ESP_RETURN_ON_FALSE(response_len >= 7 && response[1] >= 1, ESP_ERR_NOT_FOUND, TAG, "no target");
    ESP_RETURN_ON_FALSE(response[6] <= PN532_UID_MAX_LEN && response_len >= (size_t)(7 + response[6]),
                        ESP_ERR_INVALID_RESPONSE, TAG, "bad uid len");

    memset(target, 0, sizeof(*target));
    target->atqa = ((uint16_t)response[3] << 8) | response[4];
    target->sak = response[5];
    target->uid_len = response[6];
    memcpy(target->uid, &response[7], target->uid_len);
    return ESP_OK;
}

esp_err_t pn532_emulate_uid(pn532_t *pn532, const uint8_t *uid, size_t uid_len)
{
    ESP_RETURN_ON_FALSE(pn532 != NULL && pn532->initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(uid != NULL && uid_len >= 3, ESP_ERR_INVALID_ARG, TAG, "uid too short");

    uint8_t cmd[38] = {0};
    cmd[0] = PN532_CMD_TGINITASTARGET;
    cmd[1] = 0x00;
    cmd[2] = 0x04;
    cmd[3] = 0x00;
    cmd[4] = uid[0];
    cmd[5] = uid[1];
    cmd[6] = uid[2];
    cmd[7] = 0x20;

    ESP_RETURN_ON_ERROR(write_command(pn532, cmd, sizeof(cmd)), TAG, "tginit write failed");
    ESP_RETURN_ON_ERROR(read_ack(pn532), TAG, "tginit ack failed");
    return ESP_OK;
}
