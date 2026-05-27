#include "core/screen_manager.h"

#include "ui/status_bar.h"

void core_screen_manager_update(core_context_t *ctx, uint32_t dt_ms)
{
    if (ctx == NULL) {
        return;
    }
    core_animation_update(&ctx->animations.transition, dt_ms);
    core_screen_t *screen = core_nav_current(&ctx->nav);
    if (screen != NULL && screen->on_update != NULL) {
        screen->on_update(ctx, screen, dt_ms);
    }
    if (ctx->animations.transition.active) {
        core_nav_mark_dirty(&ctx->nav);
    }
}

void core_screen_manager_render(core_context_t *ctx)
{
    if (ctx == NULL || ctx->ui == NULL) {
        return;
    }
    core_screen_t *screen = core_nav_current(&ctx->nav);
    if (screen == NULL) {
        return;
    }

    if (ctx->show_fps) {
        ctx->nav.dirty = true;
        screen->dirty = true;
    }

    if (!ctx->nav.dirty && !screen->dirty && !ctx->ui->dirty) {
        return;
    }

    ui_begin(ctx->ui);
    if (screen->on_render != NULL) {
        screen->on_render(ctx, screen, ctx->ui);
    } else {
        ui_status_bar_render(ctx->ui, screen->title);
    }
    ui_present(ctx->ui);
    ctx->fps_frame_count++;
    ctx->nav.dirty = false;
    screen->dirty = false;
}
