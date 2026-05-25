#include "core/app_manager.h"
#include "core/context.h"

#include <stdio.h>
#include <string.h>
#include "drivers/rgb_led.h"
#include "system/settings.h"
#include "ui/status_bar.h"

typedef struct {
    core_screen_t screen;
    size_t selected;
    bool enabled;
    rgb_led_mode_t mode;
} rgb_light_screen_t;

static rgb_light_screen_t s_rgb_light;

static rgb_led_mode_t next_mode(rgb_led_mode_t mode, int delta)
{
    int value = (int)mode + delta;
    while (value < 0) {
        value += RGB_LED_MODE_COUNT;
    }
    return (rgb_led_mode_t)(value % RGB_LED_MODE_COUNT);
}

static void save_enabled(rgb_light_screen_t *state)
{
    (void)rgb_led_set_enabled(state->enabled);
    (void)system_settings_set_bool(RGB_LED_SETTING_ENABLED, state->enabled);
}

static void save_mode(rgb_light_screen_t *state)
{
    (void)rgb_led_set_mode(state->mode);
    (void)system_settings_set_u8(RGB_LED_SETTING_MODE, (uint8_t)state->mode);
}

static void toggle_power(core_context_t *ctx, rgb_light_screen_t *state)
{
    state->enabled = !state->enabled;
    save_enabled(state);
    core_nav_mark_dirty(&ctx->nav);
}

static void change_mode(core_context_t *ctx, rgb_light_screen_t *state, int delta)
{
    state->mode = next_mode(state->mode, delta);
    save_mode(state);
    core_nav_mark_dirty(&ctx->nav);
}

static bool rgb_light_on_input(core_context_t *ctx, core_screen_t *screen, const core_input_event_t *event)
{
    rgb_light_screen_t *state = (rgb_light_screen_t *)screen->user_data;
    if (event->phase != CORE_INPUT_PHASE_PRESS && event->phase != CORE_INPUT_PHASE_REPEAT) {
        return false;
    }

    switch (event->action) {
    case CORE_INPUT_BACK:
        (void)core_nav_pop(ctx, &ctx->nav);
        return true;
    case CORE_INPUT_UP:
    case CORE_INPUT_DOWN:
        state->selected = 1U - state->selected;
        core_nav_mark_dirty(&ctx->nav);
        return true;
    case CORE_INPUT_LEFT:
        if (state->selected == 1) {
            change_mode(ctx, state, -1);
        }
        return true;
    case CORE_INPUT_RIGHT:
        if (state->selected == 1) {
            change_mode(ctx, state, 1);
        }
        return true;
    case CORE_INPUT_SELECT:
        if (state->selected == 0) {
            toggle_power(ctx, state);
        } else {
            change_mode(ctx, state, 1);
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
    ui_draw_text_aligned(ui, 58, y, 66, value, UI_ALIGN_RIGHT, !selected);
}

static void rgb_light_on_render(core_context_t *ctx, core_screen_t *screen, ui_t *ui)
{
    (void)ctx;
    rgb_light_screen_t *state = (rgb_light_screen_t *)screen->user_data;
    char mode[12];
    snprintf(mode, sizeof(mode), "%s", rgb_led_mode_name(state->mode));

    ui_status_bar_render(ui, "RGB");
    draw_row(ui, 22, "POWER", state->enabled ? "ON" : "OFF", state->selected == 0);
    draw_row(ui, 34, "MODE", mode, state->selected == 1);
    ui_draw_text_aligned(ui, 0, 55, UI_WIDTH, "<  OK  >", UI_ALIGN_CENTER, true);
}

static esp_err_t rgb_light_launch(core_context_t *ctx)
{
    memset(&s_rgb_light, 0, sizeof(s_rgb_light));
    rgb_led_status_t status = rgb_led_get_status();
    s_rgb_light.enabled = status.enabled;
    s_rgb_light.mode = status.mode;
    s_rgb_light.screen.id = "rgb_light.screen";
    s_rgb_light.screen.title = "RGB";
    s_rgb_light.screen.user_data = &s_rgb_light;
    s_rgb_light.screen.on_input = rgb_light_on_input;
    s_rgb_light.screen.on_render = rgb_light_on_render;
    return core_nav_push(ctx, &ctx->nav, &s_rgb_light.screen);
}

const core_app_descriptor_t g_rgb_light_app = {
    .id = "rgb_light",
    .name = "RGB",
    .icon = "*",
    .launch = rgb_light_launch,
};
