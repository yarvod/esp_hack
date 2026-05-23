#include "ui/status_bar.h"

#include <stdio.h>

static void draw_battery_icon(ui_t *ui, int x, int y, uint8_t percent, bool low)
{
    ui_draw_rect(ui, x, y, 17, 8, true);
    ui_fill_rect(ui, x + 17, y + 2, 2, 4, true);
    int fill = (percent > 100 ? 100 : percent) * 13 / 100;
    if (fill > 0) {
        ui_fill_rect(ui, x + 2, y + 2, fill, 4, true);
    }
    if (low) {
        ui_draw_hline(ui, x + 4, y + 4, 8, false);
    }
}

void ui_status_bar_render(ui_t *ui, const char *title)
{
    char pct[8];
    snprintf(pct, sizeof(pct), "%u%%", ui->battery_percent);

    ui_fill_rect(ui, 0, 0, UI_WIDTH, UI_STATUS_BAR_HEIGHT, false);
    ui_draw_text(ui, 0, 1, title != NULL ? title : "HANDHELD", true);
    ui_draw_text(ui, 90, 1, pct, true);
    draw_battery_icon(ui, 109, 1, ui->battery_percent, ui->battery_low);
    ui_draw_hline(ui, 0, UI_STATUS_BAR_HEIGHT - 1, UI_WIDTH, true);
}
