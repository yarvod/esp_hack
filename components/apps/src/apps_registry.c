#include "apps/apps_registry.h"

#include "core/app_manager.h"

extern const core_app_descriptor_t g_wifi_app;
extern const core_app_descriptor_t g_bluetooth_app;
extern const core_app_descriptor_t g_flashka_app;

esp_err_t apps_register_all(core_context_t *ctx)
{
    esp_err_t err = core_app_manager_register(&ctx->apps, &g_wifi_app);
    if (err != ESP_OK) {
        return err;
    }
    err = core_app_manager_register(&ctx->apps, &g_bluetooth_app);
    if (err != ESP_OK) {
        return err;
    }
    return core_app_manager_register(&ctx->apps, &g_flashka_app);
}
