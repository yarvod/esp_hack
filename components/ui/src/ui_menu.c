#include "ui/menu.h"

#include "ui/status_bar.h"

void ui_menu_render(ui_t *ui, const ui_menu_item_t *items, size_t count, size_t selected, const char *title)
{
    ui_status_bar_render(ui, title);
    if (items == NULL || count == 0) {
        return;
    }

    int start_y = UI_CONTENT_Y + 4;
    for (size_t i = 0; i < count && i < 5; ++i) {
        int y = start_y + (int)i * 10;
        bool is_selected = i == selected;
        if (is_selected) {
            ui_fill_rect(ui, 2, y - 1, UI_WIDTH - 4, 9, true);
        }
        ui_draw_text(ui, 6, y, items[i].label, !is_selected);
        if (items[i].hint != NULL) {
            ui_draw_text_aligned(ui, 60, y, 62, items[i].hint, UI_ALIGN_RIGHT, !is_selected);
        }
    }
}
