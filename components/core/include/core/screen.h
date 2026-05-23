#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "core/input.h"

struct core_context;
struct ui;

typedef struct core_screen core_screen_t;

typedef void (*core_screen_lifecycle_fn)(struct core_context *ctx, core_screen_t *screen);
typedef bool (*core_screen_input_fn)(struct core_context *ctx, core_screen_t *screen, const core_input_event_t *event);
typedef void (*core_screen_update_fn)(struct core_context *ctx, core_screen_t *screen, uint32_t dt_ms);
typedef void (*core_screen_render_fn)(struct core_context *ctx, core_screen_t *screen, struct ui *ui);

struct core_screen {
    const char *id;
    const char *title;
    void *user_data;
    bool modal;
    bool dirty;
    core_screen_lifecycle_fn on_enter;
    core_screen_lifecycle_fn on_exit;
    core_screen_input_fn on_input;
    core_screen_update_fn on_update;
    core_screen_render_fn on_render;
};
