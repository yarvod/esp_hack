#include "simple_app_screen.h"

#include <string.h>
#include "core/context.h"
#include "ui/status_bar.h"
#include "ui/widgets.h"

static bool simple_on_input(core_context_t *ctx, core_screen_t *screen, const core_input_event_t *event)
{
    (void)ctx;
    (void)screen;
    if (event->action == CORE_INPUT_SELECT && event->phase == CORE_INPUT_PHASE_PRESS) {
        return true;
    }
    return false;
}

static void simple_on_render(core_context_t *ctx, core_screen_t *screen, ui_t *ui)
{
    (void)ctx;
    simple_app_screen_t *app = (simple_app_screen_t *)screen->user_data;
    ui_status_bar_render(ui, app->title);
    ui_widget_panel(ui, 3, UI_CONTENT_Y + 3, UI_WIDTH - 6, UI_CONTENT_HEIGHT - 6, app->title);
    for (size_t i = 0; i < app->line_count && i < 5; ++i) {
        ui_draw_text(ui, 9, UI_CONTENT_Y + 15 + (int)i * 9, app->lines[i], true);
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
