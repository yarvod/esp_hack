#pragma once

#include <stddef.h>
#include "ui/ui.h"

typedef struct {
    const char *label;
    const char *hint;
} ui_menu_item_t;

void ui_menu_render(ui_t *ui, const ui_menu_item_t *items, size_t count, size_t selected, const char *title);
