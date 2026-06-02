#include "system/storage.h"
#include "system/key_store.h"

esp_err_t storage_init(void)
{
    return key_store_init();
}

esp_err_t storage_mount_sd(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t storage_unmount_sd(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

storage_state_t storage_get_state(void)
{
    storage_state_t state = {
        .mount_point = NULL,
        .mounted = false,
    };
    return state;
}
