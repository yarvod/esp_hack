#include "core/navigation.h"

#include <string.h>
#include "core/context.h"
#include "esp_check.h"

static const char *TAG = "nav";

void core_nav_init(core_nav_t *nav)
{
    if (nav != NULL) {
        memset(nav, 0, sizeof(*nav));
        nav->dirty = true;
    }
}

static void enter_screen(struct core_context *ctx, core_screen_t *screen)
{
    if (screen != NULL) {
        screen->dirty = true;
        if (screen->on_enter != NULL) {
            screen->on_enter(ctx, screen);
        }
    }
}

static void exit_screen(struct core_context *ctx, core_screen_t *screen)
{
    if (screen != NULL && screen->on_exit != NULL) {
        screen->on_exit(ctx, screen);
    }
}

esp_err_t core_nav_push(struct core_context *ctx, core_nav_t *nav, core_screen_t *screen)
{
    ESP_RETURN_ON_FALSE(nav != NULL && screen != NULL, ESP_ERR_INVALID_ARG, TAG, "bad args");
    ESP_RETURN_ON_FALSE(nav->depth < CORE_NAV_MAX_DEPTH, ESP_ERR_NO_MEM, TAG, "stack full");
    nav->stack[nav->depth++] = screen;
    nav->dirty = true;
    enter_screen(ctx, screen);
    return ESP_OK;
}

esp_err_t core_nav_replace(struct core_context *ctx, core_nav_t *nav, core_screen_t *screen)
{
    ESP_RETURN_ON_FALSE(nav != NULL && screen != NULL, ESP_ERR_INVALID_ARG, TAG, "bad args");
    if (nav->depth > 0) {
        exit_screen(ctx, nav->stack[nav->depth - 1]);
        nav->stack[nav->depth - 1] = screen;
    } else {
        nav->stack[nav->depth++] = screen;
    }
    nav->dirty = true;
    enter_screen(ctx, screen);
    return ESP_OK;
}

esp_err_t core_nav_pop(struct core_context *ctx, core_nav_t *nav)
{
    ESP_RETURN_ON_FALSE(nav != NULL, ESP_ERR_INVALID_ARG, TAG, "nav is null");
    if (nav->depth <= 1) {
        return ESP_ERR_INVALID_STATE;
    }
    exit_screen(ctx, nav->stack[nav->depth - 1]);
    nav->stack[--nav->depth] = NULL;
    nav->dirty = true;
    core_screen_t *current = core_nav_current(nav);
    if (current != NULL) {
        current->dirty = true;
    }
    return ESP_OK;
}

core_screen_t *core_nav_current(core_nav_t *nav)
{
    if (nav == NULL || nav->depth == 0) {
        return NULL;
    }
    return nav->stack[nav->depth - 1];
}

size_t core_nav_depth(const core_nav_t *nav)
{
    return nav != NULL ? nav->depth : 0;
}

void core_nav_mark_dirty(core_nav_t *nav)
{
    if (nav != NULL) {
        nav->dirty = true;
        core_screen_t *screen = core_nav_current(nav);
        if (screen != NULL) {
            screen->dirty = true;
        }
    }
}
