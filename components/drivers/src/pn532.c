#include "drivers/pn532.h"

#include <string.h>
#include "drivers/i2c_bus.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "pn532";

#define PN532_I2C_ADDR 0x24
#define PN532_HOST_TO_PN532 0xD4
#define PN532_PN532_TO_HOST 0xD5
#define PN532_CMD_SAMCONFIGURATION 0x14
#define PN532_CMD_INLISTPASSIVETARGET 0x4A
#define PN532_STATUS_READY 0x01

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

static esp_err_t write_command(pn532_t *pn532, const uint8_t *cmd, size_t cmd_len)
{
    uint8_t frame[32];
    ESP_RETURN_ON_FALSE(pn532 != NULL && pn532->initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
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

    return i2c_master_transmit(pn532->device, frame, pos, 100);
}

static esp_err_t wait_ready(pn532_t *pn532, uint32_t timeout_ms)
{
    uint32_t waited = 0;
    while (waited <= timeout_ms) {
        uint8_t status = 0;
        esp_err_t err = i2c_master_receive(pn532->device, &status, 1, 20);
        if (err == ESP_OK && status == PN532_STATUS_READY) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        waited += 10;
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t read_frame(pn532_t *pn532, uint8_t *payload, size_t payload_size, size_t *payload_len,
                            uint32_t timeout_ms)
{
    uint8_t frame[48];
    ESP_RETURN_ON_ERROR(wait_ready(pn532, timeout_ms), TAG, "pn532 not ready");
    ESP_RETURN_ON_ERROR(i2c_master_receive(pn532->device, frame, sizeof(frame), 100), TAG, "read failed");
    ESP_RETURN_ON_FALSE(frame[0] == PN532_STATUS_READY, ESP_ERR_INVALID_RESPONSE, TAG, "bad status");
    ESP_RETURN_ON_FALSE(frame[1] == 0x00 && frame[2] == 0x00 && frame[3] == 0xFF,
                        ESP_ERR_INVALID_RESPONSE, TAG, "bad preamble");

    const uint8_t len = frame[4];
    ESP_RETURN_ON_FALSE((uint8_t)(len + frame[5]) == 0, ESP_ERR_INVALID_RESPONSE, TAG, "bad len checksum");
    ESP_RETURN_ON_FALSE(len >= 2 && (size_t)len <= sizeof(frame) - 7, ESP_ERR_INVALID_RESPONSE, TAG, "bad len");
    ESP_RETURN_ON_FALSE(frame[6] == PN532_PN532_TO_HOST, ESP_ERR_INVALID_RESPONSE, TAG, "bad tfi");

    const uint8_t dcs = frame[6 + len];
    ESP_RETURN_ON_FALSE((uint8_t)(sum_bytes(&frame[6], len) + dcs) == 0, ESP_ERR_INVALID_RESPONSE,
                        TAG, "bad data checksum");

    const size_t out_len = len - 1;
    ESP_RETURN_ON_FALSE(out_len <= payload_size, ESP_ERR_INVALID_SIZE, TAG, "payload too small");
    memcpy(payload, &frame[7], out_len);
    if (payload_len != NULL) {
        *payload_len = out_len;
    }
    return ESP_OK;
}

static esp_err_t read_ack(pn532_t *pn532)
{
    uint8_t ack[7];
    ESP_RETURN_ON_ERROR(wait_ready(pn532, 120), TAG, "ack not ready");
    ESP_RETURN_ON_ERROR(i2c_master_receive(pn532->device, ack, sizeof(ack), 100), TAG, "ack read failed");
    const uint8_t expected[] = { PN532_STATUS_READY, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00 };
    ESP_RETURN_ON_FALSE(memcmp(ack, expected, sizeof(expected)) == 0, ESP_ERR_INVALID_RESPONSE, TAG, "bad ack");
    return ESP_OK;
}

static esp_err_t command_response(pn532_t *pn532, const uint8_t *cmd, size_t cmd_len,
                                  uint8_t expected_response, uint8_t *payload,
                                  size_t payload_size, size_t *payload_len, uint32_t timeout_ms)
{
    ESP_RETURN_ON_ERROR(write_command(pn532, cmd, cmd_len), TAG, "command write failed");
    ESP_RETURN_ON_ERROR(read_ack(pn532), TAG, "ack failed");
    ESP_RETURN_ON_ERROR(read_frame(pn532, payload, payload_size, payload_len, timeout_ms), TAG, "response failed");
    ESP_RETURN_ON_FALSE(*payload_len >= 1 && payload[0] == expected_response, ESP_ERR_INVALID_RESPONSE,
                        TAG, "unexpected response");
    return ESP_OK;
}

esp_err_t pn532_init_i2c(pn532_t *pn532, int i2c_port, gpio_num_t sda, gpio_num_t scl)
{
    ESP_RETURN_ON_FALSE(pn532 != NULL, ESP_ERR_INVALID_ARG, TAG, "pn532 is null");
    memset(pn532, 0, sizeof(*pn532));
    pn532->i2c_port = i2c_port;
    pn532->sda = sda;
    pn532->scl = scl;
    pn532->address = PN532_I2C_ADDR;
    pn532->clock_hz = 100000;

    i2c_master_bus_handle_t bus = NULL;
    ESP_RETURN_ON_ERROR(i2c_bus_get_master(i2c_port, sda, scl, &bus), TAG, "bus get failed");

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = pn532->address,
        .scl_speed_hz = pn532->clock_hz,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_config, &pn532->device), TAG, "device add failed");
    pn532->initialized = true;

    const uint8_t sam[] = { PN532_CMD_SAMCONFIGURATION, 0x01, 0x14, 0x01 };
    uint8_t response[8];
    size_t response_len = 0;
    ESP_RETURN_ON_ERROR(command_response(pn532, sam, sizeof(sam), PN532_CMD_SAMCONFIGURATION + 1,
                                         response, sizeof(response), &response_len, 500),
                        TAG, "sam config failed");
    ESP_LOGI(TAG, "initialized on i2c%d sda=%d scl=%d addr=0x%02x", i2c_port, sda, scl, pn532->address);
    return ESP_OK;
}

esp_err_t pn532_read_passive_target(pn532_t *pn532, pn532_target_t *target, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(target != NULL, ESP_ERR_INVALID_ARG, TAG, "target is null");
    const uint8_t cmd[] = { PN532_CMD_INLISTPASSIVETARGET, 0x01, 0x00 };
    uint8_t response[32];
    size_t response_len = 0;
    ESP_RETURN_ON_ERROR(command_response(pn532, cmd, sizeof(cmd), PN532_CMD_INLISTPASSIVETARGET + 1,
                                         response, sizeof(response), &response_len, timeout_ms),
                        TAG, "inlist failed");

    ESP_RETURN_ON_FALSE(response_len >= 8 && response[1] >= 1, ESP_ERR_NOT_FOUND, TAG, "no target");
    memset(target, 0, sizeof(*target));
    target->atqa = ((uint16_t)response[3] << 8) | response[4];
    target->sak = response[5];
    target->uid_len = response[6];
    ESP_RETURN_ON_FALSE(target->uid_len <= PN532_UID_MAX_LEN && response_len >= (size_t)(7 + target->uid_len),
                        ESP_ERR_INVALID_RESPONSE, TAG, "bad uid len");
    memcpy(target->uid, &response[7], target->uid_len);
    return ESP_OK;
}

esp_err_t pn532_emulate_uid(pn532_t *pn532, const uint8_t *uid, size_t uid_len)
{
    (void)pn532;
    (void)uid;
    (void)uid_len;
    return ESP_ERR_NOT_SUPPORTED;
}
