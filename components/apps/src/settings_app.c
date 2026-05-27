#include "core/app_manager.h"
#include "core/context.h"

#include <string.h>
#include "system/settings.h"
#include "ui/status_bar.h"

typedef struct {
    core_screen_t screen;
    size_t selected;
    bool show_fps;
    bool low_power;
} settings_app_state_t;

static settings_app_state_t s_settings;

typedef enum {
    SETTINGS_ITEM_SHOW_FPS = 0,
    SETTINGS_ITEM_LOW_POWER,
    SETTINGS_ITEM_COUNT,
} settings_item_t;

static void apply_show_fps(core_context_t *ctx, bool show)
{
    ctx->show_fps = show;
    ctx->fps_frame_count = 0;
    ctx->fps_elapsed_ms = 0;
    ui_set_show_fps(ctx->ui, show);
    (void)system_settings_set_bool(SYSTEM_SETTING_SHOW_FPS, show);
    core_nav_mark_dirty(&ctx->nav);
}

static void apply_low_power(core_context_t *ctx, bool enabled)
{
    ctx->low_power_mode = enabled;
    ctx->render_elapsed_ms = enabled ? 50 : 0;
    ctx->fps_frame_count = 0;
    ctx->fps_elapsed_ms = 0;
    (void)system_settings_set_bool(SYSTEM_SETTING_LOW_POWER, enabled);
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
        state->selected = (state->selected + SETTINGS_ITEM_COUNT - 1) % SETTINGS_ITEM_COUNT;
        core_nav_mark_dirty(&ctx->nav);
        return true;
    case CORE_INPUT_DOWN:
        state->selected = (state->selected + 1) % SETTINGS_ITEM_COUNT;
        core_nav_mark_dirty(&ctx->nav);
        return true;
    case CORE_INPUT_LEFT:
    case CORE_INPUT_RIGHT:
    case CORE_INPUT_SELECT:
        if (state->selected == SETTINGS_ITEM_SHOW_FPS) {
            state->show_fps = !state->show_fps;
            apply_show_fps(ctx, state->show_fps);
        } else if (state->selected == SETTINGS_ITEM_LOW_POWER) {
            state->low_power = !state->low_power;
            apply_low_power(ctx, state->low_power);
        }
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
    draw_row(ui, 20, "SHOW FPS", state->show_fps ? "ON" : "OFF", state->selected == SETTINGS_ITEM_SHOW_FPS);
    draw_row(ui, 32, "LOW POWER", state->low_power ? "ON" : "OFF", state->selected == SETTINGS_ITEM_LOW_POWER);
    ui_draw_text_aligned(ui, 0, 55, UI_WIDTH, "<  OK  >", UI_ALIGN_CENTER, true);
}

static esp_err_t settings_launch(core_context_t *ctx)
{
    memset(&s_settings, 0, sizeof(s_settings));
    (void)system_settings_get_bool(SYSTEM_SETTING_SHOW_FPS, false, &s_settings.show_fps);
    (void)system_settings_get_bool(SYSTEM_SETTING_LOW_POWER, false, &s_settings.low_power);
    ctx->show_fps = s_settings.show_fps;
    ctx->low_power_mode = s_settings.low_power;
    ctx->render_elapsed_ms = s_settings.low_power ? 50 : 0;
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
