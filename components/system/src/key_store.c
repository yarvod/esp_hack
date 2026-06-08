#include "system/key_store.h"

#include <stdio.h>
#include <string.h>
#include "esp_check.h"
#include "nvs.h"

static const char *TAG = "key_store";
static const char *NS = "keys";
static const char *COUNT_KEY = "count";
static const uint32_t KEY_MAGIC = 0x4B455953U;
static const uint8_t KEY_VERSION = 1;

static void slot_key(size_t index, char *out, size_t out_size)
{
    snprintf(out, out_size, "key%02u", (unsigned)index);
}

static esp_err_t open_store(nvs_open_mode_t mode, nvs_handle_t *handle)
{
    return nvs_open(NS, mode, handle);
}

static esp_err_t get_count(nvs_handle_t handle, size_t *count)
{
    uint8_t stored = 0;
    esp_err_t err = nvs_get_u8(handle, COUNT_KEY, &stored);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        stored = 0;
        err = ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "count read failed");
    *count = stored > KEY_STORE_MAX_KEYS ? KEY_STORE_MAX_KEYS : stored;
    return ESP_OK;
}

static esp_err_t set_count(nvs_handle_t handle, size_t count)
{
    ESP_RETURN_ON_FALSE(count <= KEY_STORE_MAX_KEYS, ESP_ERR_INVALID_ARG, TAG, "count too large");
    return nvs_set_u8(handle, COUNT_KEY, (uint8_t)count);
}

static void normalize_record(key_store_record_t *record)
{
    record->magic = KEY_MAGIC;
    record->version = KEY_VERSION;
    record->type = KEY_STORE_TYPE_ISO14443A_UID;
    if (record->uid_len > KEY_STORE_UID_MAX_LEN) {
        record->uid_len = KEY_STORE_UID_MAX_LEN;
    }
    record->name[KEY_STORE_NAME_LEN - 1] = '\0';
}

static bool record_valid(const key_store_record_t *record)
{
    return record != NULL && record->magic == KEY_MAGIC && record->version == KEY_VERSION &&
           record->uid_len > 0 && record->uid_len <= KEY_STORE_UID_MAX_LEN;
}

esp_err_t key_store_init(void)
{
    nvs_handle_t handle;
    esp_err_t err = open_store(NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    size_t count = 0;
    err = get_count(handle, &count);
    if (err == ESP_OK) {
        err = set_count(handle, count);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t key_store_load_all(key_store_record_t *records, size_t capacity, size_t *count)
{
    ESP_RETURN_ON_FALSE(records != NULL && count != NULL, ESP_ERR_INVALID_ARG, TAG, "bad args");
    nvs_handle_t handle;
    esp_err_t err = open_store(NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *count = 0;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "open failed");

    size_t stored_count = 0;
    err = get_count(handle, &stored_count);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    size_t loaded = 0;
    for (size_t i = 0; i < stored_count && loaded < capacity; ++i) {
        char key[16];
        slot_key(i, key, sizeof(key));
        key_store_record_t record;
        size_t len = sizeof(record);
        err = nvs_get_blob(handle, key, &record, &len);
        if (err == ESP_OK && len == sizeof(record) && record_valid(&record)) {
            records[loaded++] = record;
        }
    }
    nvs_close(handle);
    *count = loaded;
    return ESP_OK;
}

esp_err_t key_store_add(const key_store_record_t *record)
{
    ESP_RETURN_ON_FALSE(record != NULL, ESP_ERR_INVALID_ARG, TAG, "record is null");
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(open_store(NVS_READWRITE, &handle), TAG, "open failed");

    size_t count = 0;
    esp_err_t ret = get_count(handle, &count);
    if (ret == ESP_OK) {
        ESP_GOTO_ON_FALSE(count < KEY_STORE_MAX_KEYS, ESP_ERR_NO_MEM, cleanup, TAG, "store full");
        key_store_record_t copy = *record;
        normalize_record(&copy);
        ESP_GOTO_ON_FALSE(record_valid(&copy), ESP_ERR_INVALID_ARG, cleanup, TAG, "invalid record");
        char key[16];
        slot_key(count, key, sizeof(key));
        ESP_GOTO_ON_ERROR(nvs_set_blob(handle, key, &copy, sizeof(copy)), cleanup, TAG, "blob write failed");
        ESP_GOTO_ON_ERROR(set_count(handle, count + 1), cleanup, TAG, "count write failed");
        ret = nvs_commit(handle);
    }

cleanup:
    nvs_close(handle);
    return ret;
}

esp_err_t key_store_update(size_t index, const key_store_record_t *record)
{
    ESP_RETURN_ON_FALSE(record != NULL, ESP_ERR_INVALID_ARG, TAG, "record is null");
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(open_store(NVS_READWRITE, &handle), TAG, "open failed");
    size_t count = 0;
    esp_err_t ret = get_count(handle, &count);
    if (ret == ESP_OK) {
        ESP_GOTO_ON_FALSE(index < count, ESP_ERR_NOT_FOUND, cleanup, TAG, "index out of range");
        key_store_record_t copy = *record;
        normalize_record(&copy);
        ESP_GOTO_ON_FALSE(record_valid(&copy), ESP_ERR_INVALID_ARG, cleanup, TAG, "invalid record");
        char key[16];
        slot_key(index, key, sizeof(key));
        ESP_GOTO_ON_ERROR(nvs_set_blob(handle, key, &copy, sizeof(copy)), cleanup, TAG, "blob write failed");
        ret = nvs_commit(handle);
    }

cleanup:
    nvs_close(handle);
    return ret;
}

esp_err_t key_store_delete(size_t index)
{
    key_store_record_t records[KEY_STORE_MAX_KEYS];
    size_t count = 0;
    ESP_RETURN_ON_ERROR(key_store_load_all(records, KEY_STORE_MAX_KEYS, &count), TAG, "load failed");
    ESP_RETURN_ON_FALSE(index < count, ESP_ERR_NOT_FOUND, TAG, "index out of range");

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(open_store(NVS_READWRITE, &handle), TAG, "open failed");
    esp_err_t ret = ESP_OK;
    for (size_t i = index; i + 1 < count; ++i) {
        char key[16];
        slot_key(i, key, sizeof(key));
        ESP_GOTO_ON_ERROR(nvs_set_blob(handle, key, &records[i + 1], sizeof(records[i + 1])),
                          cleanup, TAG, "compact write failed");
    }
    char last_key[16];
    slot_key(count - 1, last_key, sizeof(last_key));
    ret = nvs_erase_key(handle, last_key);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_OK;
    }
    if (ret == ESP_OK) {
        ret = set_count(handle, count - 1);
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

cleanup:
    nvs_close(handle);
    return ret;
}

esp_err_t key_store_rename(size_t index, const char *name)
{
    key_store_record_t records[KEY_STORE_MAX_KEYS];
    size_t count = 0;
    ESP_RETURN_ON_ERROR(key_store_load_all(records, KEY_STORE_MAX_KEYS, &count), TAG, "load failed");
    ESP_RETURN_ON_FALSE(index < count && name != NULL, ESP_ERR_INVALID_ARG, TAG, "bad args");
    snprintf(records[index].name, sizeof(records[index].name), "%s", name);
    return key_store_update(index, &records[index]);
}

const char *key_store_type_name(const key_store_record_t *record)
{
    if (record == NULL) {
        return "UNKNOWN";
    }
    switch (record->type) {
    case KEY_STORE_TYPE_ISO14443A_UID:
        return "ISO14443A UID";
    default:
        return "UNKNOWN";
    }
}

void key_store_format_uid(const key_store_record_t *record, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (record == NULL || record->uid_len == 0) {
        return;
    }
    size_t used = 0;
    for (uint8_t i = 0; i < record->uid_len; ++i) {
        int written = snprintf(out + used, out_size - used, "%s%02X", i == 0 ? "" : ":", record->uid[i]);
        if (written < 0 || (size_t)written >= out_size - used) {
            out[out_size - 1] = '\0';
            return;
        }
        used += (size_t)written;
    }
}
