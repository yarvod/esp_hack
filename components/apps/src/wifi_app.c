#include "core/app_manager.h"
#include "core/context.h"
#include "simple_app_screen.h"

static simple_app_screen_t s_wifi_screen;

static esp_err_t wifi_launch(core_context_t *ctx)
{
    const char *lines[] = {
        "SCAN NETWORKS",
        "AP/STA TOOLS",
        "PACKET TOOLS",
        "BACK: HOLD SW",
    };
    simple_app_screen_init(&s_wifi_screen, "wifi.screen", "WIFI", lines, 4);
    return core_nav_push(ctx, &ctx->nav, &s_wifi_screen.screen);
}

const core_app_descriptor_t g_wifi_app = {
    .id = "wifi",
    .name = "WiFi",
    .icon = "W",
    .launch = wifi_launch,
};
