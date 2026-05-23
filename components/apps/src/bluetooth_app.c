#include "core/app_manager.h"
#include "core/context.h"
#include "simple_app_screen.h"

static simple_app_screen_t s_bt_screen;

static esp_err_t bluetooth_launch(core_context_t *ctx)
{
    const char *lines[] = {
        "BLE SCANNER",
        "GATT TOOLS",
        "PAIRING LAB",
        "BACK: HOLD SW",
    };
    simple_app_screen_init(&s_bt_screen, "bt.screen", "BT", lines, 4);
    return core_nav_push(ctx, &ctx->nav, &s_bt_screen.screen);
}

const core_app_descriptor_t g_bluetooth_app = {
    .id = "bluetooth",
    .name = "Bluetooth",
    .icon = "B",
    .launch = bluetooth_launch,
};
