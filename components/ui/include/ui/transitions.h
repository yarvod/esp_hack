#pragma once

#include <stdint.h>

typedef enum {
    UI_TRANSITION_NONE = 0,
    UI_TRANSITION_SLIDE_LEFT,
    UI_TRANSITION_SLIDE_RIGHT,
} ui_transition_t;

typedef struct {
    ui_transition_t type;
    uint32_t duration_ms;
} ui_transition_config_t;
