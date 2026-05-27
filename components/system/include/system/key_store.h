#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define KEY_STORE_MAX_KEYS 16
#define KEY_STORE_NAME_LEN 18
#define KEY_STORE_UID_MAX_LEN 10

typedef enum {
    KEY_STORE_TYPE_ISO14443A_UID = 0,
} key_store_type_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint8_t uid_len;
    uint8_t reserved;
    char name[KEY_STORE_NAME_LEN];
    uint8_t uid[KEY_STORE_UID_MAX_LEN];
    uint16_t atqa;
    uint8_t sak;
} key_store_record_t;

esp_err_t key_store_init(void);
esp_err_t key_store_load_all(key_store_record_t *records, size_t capacity, size_t *count);
esp_err_t key_store_add(const key_store_record_t *record);
esp_err_t key_store_update(size_t index, const key_store_record_t *record);
esp_err_t key_store_delete(size_t index);
esp_err_t key_store_rename(size_t index, const char *name);
const char *key_store_type_name(const key_store_record_t *record);
void key_store_format_uid(const key_store_record_t *record, char *out, size_t out_size);
