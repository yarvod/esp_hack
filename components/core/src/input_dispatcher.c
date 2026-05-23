#include "core/input_dispatcher.h"

#include "core/navigation.h"

bool core_input_dispatch(core_context_t *ctx, const core_input_event_t *event)
{
    if (ctx == NULL || event == NULL) {
        return false;
    }

    core_screen_t *screen = core_nav_current(&ctx->nav);
    if (screen != NULL && screen->on_input != NULL && screen->on_input(ctx, screen, event)) {
        core_nav_mark_dirty(&ctx->nav);
        return true;
    }

    if (event->action == CORE_INPUT_BACK && event->phase == CORE_INPUT_PHASE_PRESS) {
        if (core_nav_pop(ctx, &ctx->nav) == ESP_OK) {
            return true;
        }
    }
    return false;
}
