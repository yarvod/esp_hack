#include "simple_app_screen.h"

#include <string.h>
#include "core/context.h"
#include "ui/status_bar.h"
#include "ui/widgets.h"

static bool simple_on_input(core_context_t *ctx, core_screen_t *screen, const core_input_event_t *event)
{
    simple_app_screen_t *app = (simple_app_screen_t *)screen->user_data;
    if (event->phase != CORE_INPUT_PHASE_PRESS && event->phase != CORE_INPUT_PHASE_REPEAT) {
        return false;
    }

    if ((event->action == CORE_INPUT_UP || event->action == CORE_INPUT_LEFT) && app->line_count > 0) {
        app->selected = (app->selected + app->line_count - 1) % app->line_count;
        core_nav_mark_dirty(&ctx->nav);
        return true;
    }
    if ((event->action == CORE_INPUT_DOWN || event->action == CORE_INPUT_RIGHT) && app->line_count > 0) {
        app->selected = (app->selected + 1) % app->line_count;
        core_nav_mark_dirty(&ctx->nav);
        return true;
    }
    if (event->action == CORE_INPUT_SELECT) {
        return true;
    }
    return false;
}

static void simple_on_render(core_context_t *ctx, core_screen_t *screen, ui_t *ui)
{
    (void)ctx;
    simple_app_screen_t *app = (simple_app_screen_t *)screen->user_data;
    ui_status_bar_render(ui, app->title);
    ui_widget_panel(ui, 3, UI_CONTENT_Y + 2, UI_WIDTH - 6, UI_CONTENT_HEIGHT - 5, app->title);
    for (size_t i = 0; i < app->line_count && i < 5; ++i) {
        int y = UI_CONTENT_Y + 13 + (int)i * 8;
        bool selected = i == app->selected;
        if (selected) {
            ui_fill_rect(ui, 7, y - 1, UI_WIDTH - 14, 8, true);
        }
        ui_draw_text(ui, 10, y, app->lines[i], !selected);
    }
}

void simple_app_screen_init(simple_app_screen_t *app, const char *id, const char *title,
                            const char *const *lines, size_t line_count)
{
    memset(app, 0, sizeof(*app));
    app->title = title;
    app->line_count = line_count > 5 ? 5 : line_count;
    for (size_t i = 0; i < app->line_count; ++i) {
        app->lines[i] = lines[i];
    }
    app->screen.id = id;
    app->screen.title = title;
    app->screen.user_data = app;
    app->screen.on_input = simple_on_input;
    app->screen.on_render = simple_on_render;
}
