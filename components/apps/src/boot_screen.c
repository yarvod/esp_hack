#include "apps/apps_registry.h"

#include <string.h>
#include "core/context.h"
#include "sdkconfig.h"
#include "ui/status_bar.h"
#include "ui/widgets.h"

typedef struct {
    core_screen_t screen;
    uint32_t elapsed_ms;
    uint8_t progress;
} boot_screen_state_t;

static boot_screen_state_t s_boot;

static void boot_on_enter(core_context_t *ctx, core_screen_t *screen)
{
    (void)ctx;
    boot_screen_state_t *boot = (boot_screen_state_t *)screen->user_data;
    boot->elapsed_ms = 0;
    boot->progress = 0;
}

static bool boot_on_input(core_context_t *ctx, core_screen_t *screen, const core_input_event_t *event)
{
    (void)ctx;
    (void)screen;
    (void)event;
    return true;
}

static void boot_on_update(core_context_t *ctx, core_screen_t *screen, uint32_t dt_ms)
{
    boot_screen_state_t *boot = (boot_screen_state_t *)screen->user_data;
    boot->elapsed_ms += dt_ms;
    uint32_t progress_ms = CONFIG_HANDHELD_BOOT_SCREEN_MS > 200 ? CONFIG_HANDHELD_BOOT_SCREEN_MS - 200 : CONFIG_HANDHELD_BOOT_SCREEN_MS;
    uint8_t progress = boot->elapsed_ms >= progress_ms ? 100 : (uint8_t)(boot->elapsed_ms * 100 / progress_ms);
    if (progress != boot->progress) {
        boot->progress = progress;
        core_nav_mark_dirty(&ctx->nav);
    }
    if (boot->elapsed_ms >= CONFIG_HANDHELD_BOOT_SCREEN_MS) {
        apps_show_home(ctx);
    }
}

static void boot_on_render(core_context_t *ctx, core_screen_t *screen, ui_t *ui)
{
    (void)ctx;
    boot_screen_state_t *boot = (boot_screen_state_t *)screen->user_data;
    ui_status_bar_render(ui, "BOOT");
    ui_draw_text_aligned(ui, 0, 22, UI_WIDTH, "ESP C6", UI_ALIGN_CENTER, true);
    ui_draw_text_aligned(ui, 0, 32, UI_WIDTH, "HANDHELD OS", UI_ALIGN_CENTER, true);
    ui_widget_progress_bar(ui, 20, 47, 88, 9, boot->progress);
    int dots = (boot->elapsed_ms / 250) % 4;
    ui_draw_text(ui, 52, 56, dots == 0 ? "   " : dots == 1 ? ".  " : dots == 2 ? ".. " : "...", true);
}

esp_err_t apps_show_boot(core_context_t *ctx)
{
    memset(&s_boot, 0, sizeof(s_boot));
    s_boot.screen.id = "boot";
    s_boot.screen.title = "BOOT";
    s_boot.screen.user_data = &s_boot;
    s_boot.screen.on_enter = boot_on_enter;
    s_boot.screen.on_input = boot_on_input;
    s_boot.screen.on_update = boot_on_update;
    s_boot.screen.on_render = boot_on_render;
    return core_nav_push(ctx, &ctx->nav, &s_boot.screen);
}
