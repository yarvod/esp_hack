#include "drivers/nfc.h"

#include <stdlib.h>
#include <string.h>
#include "drivers/board_pins.h"
#include "esp_log.h"
#include "pn532.h"

static const char *TAG = "nfc";

#define NFC_SPI_CLOCK_HZ 100000

static pn532_t *s_pn532;
static bool s_ready;

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
    if (s_pn532 != NULL) {
        pn532_deinit(s_pn532, true);
    }
    s_pn532 = NULL;
    s_ready = false;
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

esp_err_t nfc_emulate_uid(const uint8_t *uid, size_t uid_len)
{
    (void)uid;
    (void)uid_len;
    return ESP_ERR_NOT_SUPPORTED;
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
