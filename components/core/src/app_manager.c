#include "core/app_manager.h"

#include <string.h>
#include "esp_check.h"

static const char *TAG = "app_manager";

void core_app_manager_init(core_app_manager_t *manager)
{
    if (manager != NULL) {
        memset(manager, 0, sizeof(*manager));
    }
}

esp_err_t core_app_manager_register(core_app_manager_t *manager, const core_app_descriptor_t *app)
{
    ESP_RETURN_ON_FALSE(manager != NULL && app != NULL && app->id != NULL && app->launch != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "bad args");
    ESP_RETURN_ON_FALSE(manager->count < CORE_APP_MANAGER_MAX_APPS, ESP_ERR_NO_MEM, TAG, "registry full");
    manager->apps[manager->count++] = app;
    return ESP_OK;
}

const core_app_descriptor_t *core_app_manager_get(const core_app_manager_t *manager, size_t index)
{
    if (manager == NULL || index >= manager->count) {
        return NULL;
    }
    return manager->apps[index];
}

const core_app_descriptor_t *core_app_manager_find(const core_app_manager_t *manager, const char *id)
{
    if (manager == NULL || id == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < manager->count; ++i) {
        if (strcmp(manager->apps[i]->id, id) == 0) {
            return manager->apps[i];
        }
    }
    return NULL;
}

size_t core_app_manager_count(const core_app_manager_t *manager)
{
    return manager != NULL ? manager->count : 0;
}
