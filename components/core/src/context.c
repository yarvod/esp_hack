#include "core/context.h"

#include <string.h>

void core_context_init(core_context_t *ctx, ui_t *ui)
{
    if (ctx == NULL) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->ui = ui;
    ctx->battery_percent = 0;
    ctx->battery_mv = 0;
    ctx->battery_low = true;
    core_nav_init(&ctx->nav);
    core_app_manager_init(&ctx->apps);
    core_animation_manager_init(&ctx->animations);
}
