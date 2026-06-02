#include "drivers/nfc.h"

#include <stdlib.h>
#include <string.h>
#include "drivers/board_pins.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pn532.h"

static const char *TAG = "nfc";

#define NFC_SPI_CLOCK_HZ 100000
#define NFC_EMULATE_TIMEOUT_MS 3000
#define NFC_EMULATE_TASK_STACK  4096
#define NFC_EMULATE_TASK_PRIO   5

#define PN532_COMMAND_TGINITASTARGET 0x8C
#define PN532_COMMAND_TGGETDATA      0x86
#define PN532_COMMAND_TGSETDATA      0x8E

static pn532_t *s_pn532;
static bool s_ready;
static bool s_emulating;
static volatile bool s_emulation_stop_requested;
static TaskHandle_t s_emulation_task;
static uint8_t s_emulation_uid[NFC_UID_MAX_LEN];
static size_t s_emulation_uid_len;
static uint16_t s_emulation_atqa;
static uint8_t s_emulation_sak;

static nfc_tag_subtype_t map_subtype(pn532_nfc_type_t subtype)
{
    switch (subtype) {
    case PN532_MIFARE_CLASSIC_1K:
        return NFC_TAG_MIFARE_CLASSIC_1K;
    case PN532_MIFARE_CLASSIC_MINI:
        return NFC_TAG_MIFARE_CLASSIC_MINI;
    case PN532_MIFARE_CLASSIC_4K:
        return NFC_TAG_MIFARE_CLASSIC_4K;
    case PN532_MIFARE_ULTRALIGHT:
        return NFC_TAG_MIFARE_ULTRALIGHT;
    case PN532_MIFARE_ULTRALIGHT_C:
        return NFC_TAG_MIFARE_ULTRALIGHT_C;
    case PN532_MIFARE_ULTRALIGHT_EV1:
        return NFC_TAG_MIFARE_ULTRALIGHT_EV1;
    case PN532_MIFARE_NTAG213:
        return NFC_TAG_MIFARE_NTAG213;
    case PN532_MIFARE_NTAG215:
        return NFC_TAG_MIFARE_NTAG215;
    case PN532_MIFARE_NTAG216:
        return NFC_TAG_MIFARE_NTAG216;
    case PN532_MIFARE_PLUS_2K:
        return NFC_TAG_MIFARE_PLUS_2K;
    case PN532_MIFARE_PLUS_4K:
        return NFC_TAG_MIFARE_PLUS_4K;
    case PN532_MIFARE_DESFIRE:
        return NFC_TAG_MIFARE_DESFIRE;
    case PN532_MIFARE_UNKNOWN:
    default:
        return NFC_TAG_UNKNOWN;
    }
}

static void fill_tag(nfc_tag_t *tag, const pn532_uid_t *uid)
{
    memset(tag, 0, sizeof(*tag));
    if (uid == NULL) {
        return;
    }
    size_t uid_len = uid->uid_length > 0 ? (size_t)uid->uid_length : 0;
    if (uid_len > NFC_UID_MAX_LEN) {
        uid_len = NFC_UID_MAX_LEN;
    }
    tag->uid_len = (uint8_t)uid_len;
    if (uid_len > 0) {
        memcpy(tag->uid, uid->uid, uid_len);
    }
    tag->atqa = uid->atqa;
    tag->sak = uid->sak;
    tag->subtype = map_subtype(uid->subtype);
    tag->blocks_count = uid->blocks_count;
    tag->block_size = uid->block_size;
}

esp_err_t nfc_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "PN532 SPI init host=%d sck=%d miso=%d mosi=%d cs=%d clock=%dHz",
             SPI2_HOST, BOARD_PIN_NFC_SPI_SCK, BOARD_PIN_NFC_SPI_MISO,
             BOARD_PIN_NFC_SPI_MOSI, BOARD_PIN_NFC_SPI_CS, NFC_SPI_CLOCK_HZ);

    pn532_bus_t *bus = pn532_spi_init(SPI2_HOST, BOARD_PIN_NFC_SPI_SCK, BOARD_PIN_NFC_SPI_MISO,
                                      BOARD_PIN_NFC_SPI_MOSI, BOARD_PIN_NFC_SPI_CS, NFC_SPI_CLOCK_HZ);
    if (bus == NULL) {
        ESP_LOGW(TAG, "PN532 SPI bus init failed");
        return ESP_FAIL;
    }

    s_pn532 = pn532_init(bus, GPIO_NUM_NC, GPIO_NUM_NC);
    if (s_pn532 == NULL) {
        pn532_bus_destroy(bus);
        ESP_LOGW(TAG, "PN532 init failed");
        return ESP_FAIL;
    }

    uint32_t firmware = pn532_get_firmware_version(s_pn532);
    if (firmware == 0) {
        pn532_deinit(s_pn532, true);
        s_pn532 = NULL;
        ESP_LOGW(TAG, "PN532 firmware read failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "PN532 firmware=0x%08lx", (unsigned long)firmware);
    s_ready = true;
    return ESP_OK;
}

void nfc_deinit(void)
{
    nfc_emulation_stop();
    for (int i = 0; s_emulation_task != NULL && i < 40; ++i) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (s_pn532 != NULL) {
        pn532_deinit(s_pn532, true);
    }
    s_pn532 = NULL;
    s_ready = false;
    s_emulating = false;
}

bool nfc_is_ready(void)
{
    return s_ready;
}

esp_err_t nfc_get_firmware(uint32_t *firmware)
{
    if (firmware == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = nfc_init();
    if (err != ESP_OK) {
        return err;
    }
    *firmware = pn532_get_firmware_version(s_pn532);
    return *firmware != 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t nfc_scan(nfc_tag_t *tag)
{
    if (tag == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_emulation_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nfc_init();
    if (err != ESP_OK) {
        return err;
    }

    pn532_uids_array_t *uids = pn532_14443_get_all_uids(s_pn532);
    if (uids == NULL || uids->uids_count == 0) {
        free(uids);
        return ESP_ERR_NOT_FOUND;
    }

    pn532_uid_t *uid = &uids->uids[0];
    uint16_t blocks_count = 0;
    uint16_t block_size = 0;
    bool needs_reselect = false;
    if (pn532_14443_detect_selected_card_type_and_capacity(s_pn532, uid, &blocks_count, &block_size, &needs_reselect)) {
        uid->blocks_count = blocks_count;
        uid->block_size = block_size;
    }
    if (needs_reselect) {
        (void)pn532_14443_select_by_uid(s_pn532, uid);
    }

    fill_tag(tag, uid);
    free(uids);
    return ESP_OK;
}

static bool nfc_target_reply_once(void)
{
    uint8_t rx[80];
    size_t rx_len = sizeof(rx);
    if (!pn532_execute_command(s_pn532, PN532_COMMAND_TGGETDATA, NULL, 0, rx, &rx_len, 250)) {
        return false;
    }
    if (rx_len == 0 || rx[0] != 0x00) {
        return false;
    }

    ESP_LOGI(TAG, "target received %u byte(s)", (unsigned)(rx_len - 1));

    const uint8_t file_not_found[] = {0x6A, 0x82};
    uint8_t response[4];
    size_t response_len = sizeof(response);
    return pn532_execute_command(s_pn532, PN532_COMMAND_TGSETDATA, file_not_found, sizeof(file_not_found),
                                 response, &response_len, 250);
}

static esp_err_t nfc_emulate_uid_once(const uint8_t *uid, size_t uid_len, uint16_t atqa, uint8_t sak)
{
    // PN532 target mode for ISO14443A uses 3-byte NFCID1t.
    // For 4-byte UID, the first byte is often 0x08 (if random) or handled by PN532.
    // For 7-byte UID, it's definitely truncated.
    // We'll use the LAST 3 bytes of the UID which often works better for some readers.
    uint8_t target_uid[3];
    if (uid_len >= 3) {
        memcpy(target_uid, uid + (uid_len - 3), 3);
    } else {
        memset(target_uid, 0, 3);
        memcpy(target_uid, uid, uid_len);
    }

    static const uint8_t felica_params[18] = {
        0x01, 0xFE, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
        0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
        0xFF, 0xFF,
    };

    uint8_t nfcid3[10] = {0xAA, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
    for (size_t i = 0; i < uid_len && i < sizeof(nfcid3); ++i) {
        nfcid3[i] = uid[i];
    }

    uint8_t params[37];
    size_t offset = 0;
    params[offset++] = 0x00; // Mode: Passive only
    
    // MifareParams (6 bytes): ATQA (2), NFCID1t (3), SAK (1)
    params[offset++] = (uint8_t)(atqa & 0xFF);        // SENS_RES LSB
    params[offset++] = (uint8_t)((atqa >> 8) & 0xFF); // SENS_RES MSB
    memcpy(params + offset, target_uid, 3);           // NFCID1t
    offset += 3;
    params[offset++] = sak;                           // SEL_RES

    memcpy(params + offset, felica_params, sizeof(felica_params));
    offset += sizeof(felica_params);
    memcpy(params + offset, nfcid3, sizeof(nfcid3));
    offset += sizeof(nfcid3);
    params[offset++] = 0x00; // LenGt
    params[offset++] = 0x00; // LenTk

    uint8_t response[64];
    size_t response_len = sizeof(response);
    // Use smaller timeout to be more responsive to stop requests
    if (!pn532_execute_command(s_pn532, PN532_COMMAND_TGINITASTARGET, params, offset, response, &response_len,
                               1000)) {
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "PN532 target activated by reader");
    for (int i = 0; !s_emulation_stop_requested && i < 8; ++i) {
        if (!nfc_target_reply_once()) {
            break;
        }
    }
    return ESP_OK;
}

static void nfc_emulation_task(void *arg)
{
    (void)arg;
    uint8_t uid[NFC_UID_MAX_LEN];
    size_t uid_len = s_emulation_uid_len;
    uint16_t atqa = s_emulation_atqa;
    uint8_t sak = s_emulation_sak;
    memcpy(uid, s_emulation_uid, uid_len);

    ESP_LOGI(TAG, "emulation started: uid_len=%u atqa=%04X sak=%02X", (unsigned)uid_len, atqa, sak);

    while (!s_emulation_stop_requested) {
        esp_err_t err = nfc_emulate_uid_once(uid, uid_len, atqa, sak);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "target mode failed: %s", esp_err_to_name(err));
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (s_pn532 != NULL) {
        (void)pn532_set_rf_off(s_pn532);
    }
    s_emulating = false;
    s_emulation_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t nfc_emulate_uid(const uint8_t *uid, size_t uid_len, uint16_t atqa, uint8_t sak)
{
    if (uid == NULL || uid_len < 3 || uid_len > NFC_UID_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_emulation_task != NULL) {
        return ESP_OK;
    }

    esp_err_t err = nfc_init();
    if (err != ESP_OK) {
        return err;
    }

    memcpy(s_emulation_uid, uid, uid_len);
    s_emulation_uid_len = uid_len;
    s_emulation_atqa = atqa;
    s_emulation_sak = sak;
    s_emulation_stop_requested = false;
    s_emulating = true;

    BaseType_t task_ok = xTaskCreate(nfc_emulation_task, "nfc_emu", NFC_EMULATE_TASK_STACK, NULL,
                                     NFC_EMULATE_TASK_PRIO, &s_emulation_task);
    if (task_ok != pdPASS) {
        s_emulating = false;
        s_emulation_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void nfc_emulation_stop(void)
{
    s_emulation_stop_requested = true;
    if (s_emulation_task == NULL) {
        s_emulating = false;
    }
}

bool nfc_emulation_is_active(void)
{
    return s_emulating;
}

const char *nfc_subtype_name(nfc_tag_subtype_t subtype)
{
    switch (subtype) {
    case NFC_TAG_MIFARE_CLASSIC_1K:
        return "Classic 1K";
    case NFC_TAG_MIFARE_CLASSIC_MINI:
        return "Classic Mini";
    case NFC_TAG_MIFARE_CLASSIC_4K:
        return "Classic 4K";
    case NFC_TAG_MIFARE_ULTRALIGHT:
        return "Ultralight";
    case NFC_TAG_MIFARE_ULTRALIGHT_C:
        return "Ultralight C";
    case NFC_TAG_MIFARE_ULTRALIGHT_EV1:
        return "Ultralight EV1";
    case NFC_TAG_MIFARE_NTAG213:
        return "NTAG213";
    case NFC_TAG_MIFARE_NTAG215:
        return "NTAG215";
    case NFC_TAG_MIFARE_NTAG216:
        return "NTAG216";
    case NFC_TAG_MIFARE_PLUS_2K:
        return "Plus 2K";
    case NFC_TAG_MIFARE_PLUS_4K:
        return "Plus 4K";
    case NFC_TAG_MIFARE_DESFIRE:
        return "DESFire";
    case NFC_TAG_UNKNOWN:
    default:
        return "Unknown";
    }
}
