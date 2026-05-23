#pragma once

#include <stddef.h>
#include "ui/ui.h"

typedef struct {
    const char *label;
    const char *icon;
} ui_carousel_item_t;

void ui_carousel_render(ui_t *ui, const ui_carousel_item_t *items, size_t count, size_t selected, int16_t offset_px);
