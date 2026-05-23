#pragma once

#include <stdint.h>
#include "ui/ui.h"

void ui_widget_progress_bar(ui_t *ui, int x, int y, int w, int h, uint8_t percent);
void ui_widget_soft_button(ui_t *ui, int x, int y, int w, const char *label, bool selected);
void ui_widget_panel(ui_t *ui, int x, int y, int w, int h, const char *title);
void ui_widget_empty_state(ui_t *ui, const char *title, const char *subtitle);
