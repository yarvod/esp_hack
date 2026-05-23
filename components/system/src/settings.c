#include "system/settings.h"

#include "esp_check.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "settings";
static const char *NS = "settings";

esp_err_t system_settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase failed");
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t system_settings_get_u8(const char *key, uint8_t default_value, uint8_t *value)
{
    ESP_RETURN_ON_FALSE(key != NULL && value != NULL, ESP_ERR_INVALID_ARG, TAG, "bad args");
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        *value = default_value;
        return ESP_OK;
    }
    err = nvs_get_u8(handle, key, value);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *value = default_value;
        return ESP_OK;
    }
    return err;
}

esp_err_t system_settings_set_u8(const char *key, uint8_t value)
{
    ESP_RETURN_ON_FALSE(key != NULL, ESP_ERR_INVALID_ARG, TAG, "key is null");
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &handle), TAG, "nvs open failed");
    esp_err_t err = nvs_set_u8(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t system_settings_get_bool(const char *key, bool default_value, bool *value)
{
    uint8_t tmp = default_value ? 1 : 0;
    esp_err_t err = system_settings_get_u8(key, tmp, &tmp);
    *value = tmp != 0;
    return err;
}

esp_err_t system_settings_set_bool(const char *key, bool value)
{
    return system_settings_set_u8(key, value ? 1 : 0);
}
