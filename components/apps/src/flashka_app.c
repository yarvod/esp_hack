#include "core/app_manager.h"
#include "core/context.h"
#include "simple_app_screen.h"
#include "system/storage.h"

static simple_app_screen_t s_flash_screen;

static esp_err_t flashka_launch(core_context_t *ctx)
{
    storage_state_t state = storage_get_state();
    const char *mounted = state.mounted ? "SD: MOUNTED" : "SD: NOT MOUNTED";
    const char *lines[] = {
        "FILE MANAGER",
        mounted,
        "/sdcard",
        "BACK: HOLD SW",
    };
    simple_app_screen_init(&s_flash_screen, "flashka.screen", "FLASHKA", lines, 4);
    return core_nav_push(ctx, &ctx->nav, &s_flash_screen.screen);
}

const core_app_descriptor_t g_flashka_app = {
    .id = "flashka",
    .name = "Flashka",
    .icon = "F",
    .launch = flashka_launch,
};
