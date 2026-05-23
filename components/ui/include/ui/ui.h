#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "drivers/ssd1306.h"

#define UI_WIDTH SSD1306_WIDTH
#define UI_HEIGHT SSD1306_HEIGHT
#define UI_STATUS_BAR_HEIGHT 10
#define UI_CONTENT_Y UI_STATUS_BAR_HEIGHT
#define UI_CONTENT_HEIGHT (UI_HEIGHT - UI_STATUS_BAR_HEIGHT)

typedef struct ui {
    ssd1306_t *display;
    bool dirty;
    uint8_t battery_percent;
    bool battery_low;
} ui_t;

typedef enum {
    UI_ALIGN_LEFT = 0,
    UI_ALIGN_CENTER,
    UI_ALIGN_RIGHT,
} ui_align_t;

esp_err_t ui_init(ui_t *ui, ssd1306_t *display);
void ui_begin(ui_t *ui);
esp_err_t ui_present(ui_t *ui);
void ui_mark_dirty(ui_t *ui);
void ui_set_battery(ui_t *ui, uint8_t percent, bool low);

void ui_draw_pixel(ui_t *ui, int x, int y, bool color);
void ui_draw_hline(ui_t *ui, int x, int y, int w, bool color);
void ui_draw_vline(ui_t *ui, int x, int y, int h, bool color);
void ui_draw_rect(ui_t *ui, int x, int y, int w, int h, bool color);
void ui_fill_rect(ui_t *ui, int x, int y, int w, int h, bool color);
void ui_draw_text(ui_t *ui, int x, int y, const char *text, bool color);
void ui_draw_text_aligned(ui_t *ui, int x, int y, int w, const char *text, ui_align_t align, bool color);
int ui_text_width(const char *text);
