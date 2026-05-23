#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "core/screen.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CORE_NAV_MAX_DEPTH 8

typedef struct {
    core_screen_t *stack[CORE_NAV_MAX_DEPTH];
    size_t depth;
    bool dirty;
} core_nav_t;

void core_nav_init(core_nav_t *nav);
esp_err_t core_nav_push(struct core_context *ctx, core_nav_t *nav, core_screen_t *screen);
esp_err_t core_nav_replace(struct core_context *ctx, core_nav_t *nav, core_screen_t *screen);
esp_err_t core_nav_pop(struct core_context *ctx, core_nav_t *nav);
core_screen_t *core_nav_current(core_nav_t *nav);
size_t core_nav_depth(const core_nav_t *nav);
void core_nav_mark_dirty(core_nav_t *nav);

#ifdef __cplusplus
}
#endif
