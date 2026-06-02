#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define NFC_UID_MAX_LEN 10

typedef enum {
    NFC_TAG_UNKNOWN = 0,
    NFC_TAG_MIFARE_CLASSIC_1K,
    NFC_TAG_MIFARE_CLASSIC_MINI,
    NFC_TAG_MIFARE_CLASSIC_4K,
    NFC_TAG_MIFARE_ULTRALIGHT,
    NFC_TAG_MIFARE_ULTRALIGHT_C,
    NFC_TAG_MIFARE_ULTRALIGHT_EV1,
    NFC_TAG_MIFARE_NTAG213,
    NFC_TAG_MIFARE_NTAG215,
    NFC_TAG_MIFARE_NTAG216,
    NFC_TAG_MIFARE_PLUS_2K,
    NFC_TAG_MIFARE_PLUS_4K,
    NFC_TAG_MIFARE_DESFIRE,
} nfc_tag_subtype_t;

typedef struct {
    uint8_t uid[NFC_UID_MAX_LEN];
    uint8_t uid_len;
    uint16_t atqa;
    uint8_t sak;
    nfc_tag_subtype_t subtype;
    uint16_t blocks_count;
    uint16_t block_size;
} nfc_tag_t;

esp_err_t nfc_init(void);
void nfc_deinit(void);
bool nfc_is_ready(void);
esp_err_t nfc_get_firmware(uint32_t *firmware);
esp_err_t nfc_scan(nfc_tag_t *tag);
esp_err_t nfc_emulate_uid(const uint8_t *uid, size_t uid_len);
const char *nfc_subtype_name(nfc_tag_subtype_t subtype);
