#include "ui/widgets.h"

void ui_widget_progress_bar(ui_t *ui, int x, int y, int w, int h, uint8_t percent)
{
    ui_draw_rect(ui, x, y, w, h, true);
    int fill = (w - 4) * (percent > 100 ? 100 : percent) / 100;
    if (fill > 0) {
        ui_fill_rect(ui, x + 2, y + 2, fill, h - 4, true);
    }
}

void ui_widget_soft_button(ui_t *ui, int x, int y, int w, const char *label, bool selected)
{
    ui_draw_rect(ui, x, y, w, 12, true);
    if (selected) {
        ui_fill_rect(ui, x + 1, y + 1, w - 2, 10, true);
        ui_draw_text_aligned(ui, x, y + 3, w, label, UI_ALIGN_CENTER, false);
    } else {
        ui_draw_text_aligned(ui, x, y + 3, w, label, UI_ALIGN_CENTER, true);
    }
}

void ui_widget_panel(ui_t *ui, int x, int y, int w, int h, const char *title)
{
    ui_draw_rect(ui, x, y, w, h, true);
    if (title != NULL) {
        ui_fill_rect(ui, x + 2, y, ui_text_width(title) + 4, 9, false);
        ui_draw_text(ui, x + 4, y + 1, title, true);
    }
}

void ui_widget_empty_state(ui_t *ui, const char *title, const char *subtitle)
{
    ui_draw_text_aligned(ui, 0, 26, UI_WIDTH, title, UI_ALIGN_CENTER, true);
    ui_draw_text_aligned(ui, 0, 38, UI_WIDTH, subtitle, UI_ALIGN_CENTER, true);
}
