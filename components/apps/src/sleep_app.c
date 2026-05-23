#include "core/app_manager.h"
#include "core/context.h"

#include <string.h>
#include "drivers/board_pins.h"
#include "drivers/ssd1306.h"
#include "system/power_manager.h"
#include "ui/status_bar.h"
#include "ui/widgets.h"

typedef struct {
    core_screen_t screen;
    uint32_t elapsed_ms;
    bool sleep_started;
} sleep_screen_t;

static sleep_screen_t s_sleep;

static void sleep_on_enter(core_context_t *ctx, core_screen_t *screen)
{
    (void)ctx;
    sleep_screen_t *state = (sleep_screen_t *)screen->user_data;
    state->elapsed_ms = 0;
    state->sleep_started = false;
}

static bool sleep_on_input(core_context_t *ctx, core_screen_t *screen, const core_input_event_t *event)
{
    sleep_screen_t *state = (sleep_screen_t *)screen->user_data;
    if (state->sleep_started || event->phase != CORE_INPUT_PHASE_PRESS) {
        return false;
    }

    if (event->action == CORE_INPUT_BACK) {
        (void)core_nav_pop(ctx, &ctx->nav);
        return true;
    }
    if (event->action == CORE_INPUT_SELECT) {
        state->elapsed_ms = 700;
        core_nav_mark_dirty(&ctx->nav);
        return true;
    }
    return false;
}

static void sleep_on_update(core_context_t *ctx, core_screen_t *screen, uint32_t dt_ms)
{
    sleep_screen_t *state = (sleep_screen_t *)screen->user_data;
    if (state->sleep_started) {
        return;
    }

    state->elapsed_ms += dt_ms;
    if (state->elapsed_ms < 700) {
        return;
    }

    state->sleep_started = true;
    core_nav_mark_dirty(&ctx->nav);
    (void)ssd1306_set_display_on(ctx->ui->display, false);
    (void)power_manager_request_deep_sleep(BOARD_PIN_JOY_SW);
}

static void sleep_on_render(core_context_t *ctx, core_screen_t *screen, ui_t *ui)
{
    (void)ctx;
    sleep_screen_t *state = (sleep_screen_t *)screen->user_data;

    ui_status_bar_render(ui, "SLEEP");
    ui_widget_panel(ui, 3, UI_CONTENT_Y + 2, UI_WIDTH - 6, UI_CONTENT_HEIGHT - 5, "DEEP SLEEP");
    ui_draw_text_aligned(ui, 0, UI_CONTENT_Y + 18, UI_WIDTH, "LOW POWER MODE", UI_ALIGN_CENTER, true);
    ui_draw_text_aligned(ui, 0, UI_CONTENT_Y + 31, UI_WIDTH, "WAKE: HOLD", UI_ALIGN_CENTER, true);
    ui_draw_text_aligned(ui, 0, UI_CONTENT_Y + 42, UI_WIDTH, "CENTER 3 SEC", UI_ALIGN_CENTER, true);
    if (!state->sleep_started) {
        ui_widget_progress_bar(ui, 18, 56, 92, 6, (uint8_t)((state->elapsed_ms * 100) / 700));
    }
}

static esp_err_t sleep_launch(core_context_t *ctx)
{
    memset(&s_sleep, 0, sizeof(s_sleep));
    s_sleep.screen.id = "sleep.screen";
    s_sleep.screen.title = "SLEEP";
    s_sleep.screen.user_data = &s_sleep;
    s_sleep.screen.on_enter = sleep_on_enter;
    s_sleep.screen.on_input = sleep_on_input;
    s_sleep.screen.on_update = sleep_on_update;
    s_sleep.screen.on_render = sleep_on_render;
    return core_nav_push(ctx, &ctx->nav, &s_sleep.screen);
}

const core_app_descriptor_t g_sleep_app = {
    .id = "sleep",
    .name = "Sleep",
    .icon = "Z",
    .launch = sleep_launch,
};
