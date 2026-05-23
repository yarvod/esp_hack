#include "apps/apps_registry.h"

#include <string.h>
#include "core/animation.h"
#include "core/context.h"
#include "ui/carousel.h"

typedef struct {
    core_screen_t screen;
    size_t selected;
} home_screen_state_t;

static home_screen_state_t s_home;

static void move_selection(core_context_t *ctx, home_screen_state_t *home, int delta)
{
    size_t count = core_app_manager_count(&ctx->apps);
    if (count == 0) {
        return;
    }
    if (delta > 0) {
        home->selected = (home->selected + 1) % count;
        core_animation_start(&ctx->animations.transition, 42, 0, 160, CORE_EASE_OUT_CUBIC);
    } else {
        home->selected = (home->selected + count - 1) % count;
        core_animation_start(&ctx->animations.transition, -42, 0, 160, CORE_EASE_OUT_CUBIC);
    }
    core_nav_mark_dirty(&ctx->nav);
}

static bool home_on_input(core_context_t *ctx, core_screen_t *screen, const core_input_event_t *event)
{
    home_screen_state_t *home = (home_screen_state_t *)screen->user_data;
    if (event->phase != CORE_INPUT_PHASE_PRESS && event->phase != CORE_INPUT_PHASE_REPEAT) {
        return false;
    }

    switch (event->action) {
    case CORE_INPUT_UP:
    case CORE_INPUT_LEFT:
        move_selection(ctx, home, -1);
        return true;
    case CORE_INPUT_DOWN:
    case CORE_INPUT_RIGHT:
        move_selection(ctx, home, 1);
        return true;
    case CORE_INPUT_SELECT: {
        const core_app_descriptor_t *app = core_app_manager_get(&ctx->apps, home->selected);
        if (app != NULL && app->launch != NULL) {
            app->launch(ctx);
        }
        return true;
    }
    default:
        return false;
    }
}

static void home_on_render(core_context_t *ctx, core_screen_t *screen, ui_t *ui)
{
    home_screen_state_t *home = (home_screen_state_t *)screen->user_data;
    ui_carousel_item_t items[CORE_APP_MANAGER_MAX_APPS];
    size_t count = core_app_manager_count(&ctx->apps);
    for (size_t i = 0; i < count; ++i) {
        const core_app_descriptor_t *app = core_app_manager_get(&ctx->apps, i);
        items[i].label = app->name;
        items[i].icon = app->icon;
    }
    ui_carousel_render(ui, items, count, home->selected, (int16_t)ctx->animations.transition.value);
}

esp_err_t apps_show_home(core_context_t *ctx)
{
    memset(&s_home, 0, sizeof(s_home));
    s_home.screen.id = "home";
    s_home.screen.title = "MAIN";
    s_home.screen.user_data = &s_home;
    s_home.screen.on_input = home_on_input;
    s_home.screen.on_render = home_on_render;
    return core_nav_replace(ctx, &ctx->nav, &s_home.screen);
}
