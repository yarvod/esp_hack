#include "core/app_manager.h"
#include "core/context.h"

#include <string.h>
#include "system/settings.h"
#include "ui/status_bar.h"

typedef struct {
    core_screen_t screen;
    size_t selected;
    bool show_fps;
} settings_app_state_t;

static settings_app_state_t s_settings;

static void apply_show_fps(core_context_t *ctx, bool show)
{
    ctx->show_fps = show;
    ctx->fps_frame_count = 0;
    ctx->fps_elapsed_ms = 0;
    ui_set_show_fps(ctx->ui, show);
    (void)system_settings_set_bool(SYSTEM_SETTING_SHOW_FPS, show);
    core_nav_mark_dirty(&ctx->nav);
}

static bool settings_on_input(core_context_t *ctx, core_screen_t *screen, const core_input_event_t *event)
{
    settings_app_state_t *state = (settings_app_state_t *)screen->user_data;
    if (event->phase != CORE_INPUT_PHASE_PRESS && event->phase != CORE_INPUT_PHASE_REPEAT) {
        return false;
    }

    switch (event->action) {
    case CORE_INPUT_BACK:
        (void)core_nav_pop(ctx, &ctx->nav);
        return true;
    case CORE_INPUT_UP:
    case CORE_INPUT_DOWN:
        state->selected = 0;
        core_nav_mark_dirty(&ctx->nav);
        return true;
    case CORE_INPUT_LEFT:
    case CORE_INPUT_RIGHT:
    case CORE_INPUT_SELECT:
        state->show_fps = !state->show_fps;
        apply_show_fps(ctx, state->show_fps);
        return true;
    default:
        return false;
    }
}

static void draw_row(ui_t *ui, int y, const char *label, const char *value, bool selected)
{
    if (selected) {
        ui_fill_rect(ui, 2, y - 2, 124, 11, true);
    }
    ui_draw_text(ui, 5, y, label, !selected);
    ui_draw_text_aligned(ui, 72, y, 52, value, UI_ALIGN_RIGHT, !selected);
}

static void settings_on_render(core_context_t *ctx, core_screen_t *screen, ui_t *ui)
{
    (void)ctx;
    settings_app_state_t *state = (settings_app_state_t *)screen->user_data;
    ui_status_bar_render(ui, "SETTINGS");
    draw_row(ui, 22, "SHOW FPS", state->show_fps ? "ON" : "OFF", state->selected == 0);
    ui_draw_text_aligned(ui, 0, 55, UI_WIDTH, "<  OK  >", UI_ALIGN_CENTER, true);
}

static esp_err_t settings_launch(core_context_t *ctx)
{
    memset(&s_settings, 0, sizeof(s_settings));
    (void)system_settings_get_bool(SYSTEM_SETTING_SHOW_FPS, false, &s_settings.show_fps);
    ctx->show_fps = s_settings.show_fps;
    ui_set_show_fps(ctx->ui, s_settings.show_fps);
    s_settings.screen.id = "settings.screen";
    s_settings.screen.title = "SETTINGS";
    s_settings.screen.user_data = &s_settings;
    s_settings.screen.on_input = settings_on_input;
    s_settings.screen.on_render = settings_on_render;
    return core_nav_push(ctx, &ctx->nav, &s_settings.screen);
}

const core_app_descriptor_t g_settings_app = {
    .id = "settings",
    .name = "Settings",
    .icon = "S",
    .launch = settings_launch,
};
