#include "ui/carousel.h"

#include "ui/status_bar.h"

static size_t wrap_index(size_t count, int index)
{
    while (index < 0) {
        index += (int)count;
    }
    return (size_t)(index % (int)count);
}

static void draw_item(ui_t *ui, int center_x, const ui_carousel_item_t *item, bool selected)
{
    int w = selected ? 58 : 44;
    int h = selected ? 34 : 24;
    int x = center_x - w / 2;
    int y = selected ? 21 : 26;
    ui_draw_rect(ui, x, y, w, h, true);
    if (selected) {
        ui_fill_rect(ui, x + 2, y + 2, w - 4, 10, true);
        ui_draw_text_aligned(ui, x, y + 4, w, item->icon, UI_ALIGN_CENTER, false);
        ui_draw_text_aligned(ui, x + 2, y + 18, w - 4, item->label, UI_ALIGN_CENTER, true);
    } else {
        ui_draw_text_aligned(ui, x + 2, y + 4, w - 4, item->icon, UI_ALIGN_CENTER, true);
        ui_draw_text_aligned(ui, x + 2, y + 14, w - 4, item->label, UI_ALIGN_CENTER, true);
    }
}

void ui_carousel_render(ui_t *ui, const ui_carousel_item_t *items, size_t count, size_t selected, int16_t offset_px)
{
    ui_status_bar_render(ui, "MAIN");
    if (items == NULL || count == 0) {
        return;
    }

    int base = UI_WIDTH / 2 + offset_px;
    size_t left = wrap_index(count, (int)selected - 1);
    size_t right = wrap_index(count, (int)selected + 1);

    draw_item(ui, base - 64, &items[left], false);
    draw_item(ui, base + 64, &items[right], false);
    draw_item(ui, base, &items[selected], true);

    ui_draw_text_aligned(ui, 0, 56, UI_WIDTH, "<  OK  >", UI_ALIGN_CENTER, true);
}
