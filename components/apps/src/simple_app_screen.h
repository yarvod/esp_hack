#pragma once

#include <stddef.h>
#include "core/screen.h"

typedef struct {
    core_screen_t screen;
    const char *title;
    const char *lines[5];
    size_t line_count;
} simple_app_screen_t;

void simple_app_screen_init(simple_app_screen_t *app, const char *id, const char *title,
                            const char *const *lines, size_t line_count);
