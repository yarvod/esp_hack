#include "ui/ui.h"

#include <string.h>
#include "esp_check.h"

static const char *TAG = "ui";

const uint8_t *ui_font5x7_glyph(char c);

esp_err_t ui_init(ui_t *ui, ssd1306_t *display)
{
    ESP_RETURN_ON_FALSE(ui != NULL && display != NULL, ESP_ERR_INVALID_ARG, TAG, "bad args");
    memset(ui, 0, sizeof(*ui));
    ui->display = display;
    ui->dirty = true;
    return ESP_OK;
}

void ui_begin(ui_t *ui)
{
    if (ui == NULL || ui->display == NULL) {
        return;
    }
    ssd1306_clear(ui->display);
}

esp_err_t ui_present(ui_t *ui)
{
    ESP_RETURN_ON_FALSE(ui != NULL && ui->display != NULL, ESP_ERR_INVALID_ARG, TAG, "bad args");
    esp_err_t err = ssd1306_flush(ui->display);
    if (err == ESP_OK) {
        ui->dirty = false;
    }
    return err;
}

void ui_mark_dirty(ui_t *ui)
{
    if (ui != NULL) {
        ui->dirty = true;
    }
}

void ui_set_battery(ui_t *ui, uint8_t percent, bool low)
{
    if (ui == NULL) {
        return;
    }
    if (ui->battery_percent != percent || ui->battery_low != low) {
        ui->battery_percent = percent;
        ui->battery_low = low;
        ui_mark_dirty(ui);
    }
}

void ui_draw_pixel(ui_t *ui, int x, int y, bool color)
{
    if (ui == NULL || ui->display == NULL) {
        return;
    }
    ssd1306_draw_pixel(ui->display, x, y, color);
}

void ui_draw_hline(ui_t *ui, int x, int y, int w, bool color)
{
    for (int i = 0; i < w; ++i) {
        ui_draw_pixel(ui, x + i, y, color);
    }
}

void ui_draw_vline(ui_t *ui, int x, int y, int h, bool color)
{
    for (int i = 0; i < h; ++i) {
        ui_draw_pixel(ui, x, y + i, color);
    }
}

void ui_draw_rect(ui_t *ui, int x, int y, int w, int h, bool color)
{
    ui_draw_hline(ui, x, y, w, color);
    ui_draw_hline(ui, x, y + h - 1, w, color);
    ui_draw_vline(ui, x, y, h, color);
    ui_draw_vline(ui, x + w - 1, y, h, color);
}

void ui_fill_rect(ui_t *ui, int x, int y, int w, int h, bool color)
{
    for (int yy = 0; yy < h; ++yy) {
        ui_draw_hline(ui, x, y + yy, w, color);
    }
}

void ui_draw_text(ui_t *ui, int x, int y, const char *text, bool color)
{
    if (text == NULL) {
        return;
    }
    int cursor = x;
    while (*text != '\0') {
        const uint8_t *glyph = ui_font5x7_glyph(*text++);
        for (int col = 0; col < 5; ++col) {
            for (int row = 0; row < 7; ++row) {
                if ((glyph[col] >> row) & 0x01) {
                    ui_draw_pixel(ui, cursor + col, y + row, color);
                }
            }
        }
        cursor += 6;
    }
}

int ui_text_width(const char *text)
{
    size_t len = text == NULL ? 0 : strlen(text);
    return len == 0 ? 0 : (int)len * 6 - 1;
}

void ui_draw_text_aligned(ui_t *ui, int x, int y, int w, const char *text, ui_align_t align, bool color)
{
    int tw = ui_text_width(text);
    int tx = x;
    if (align == UI_ALIGN_CENTER) {
        tx = x + (w - tw) / 2;
    } else if (align == UI_ALIGN_RIGHT) {
        tx = x + w - tw;
    }
    ui_draw_text(ui, tx, y, text, color);
}
