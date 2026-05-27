#pragma once

#include <stdint.h>
#include "core/animation.h"
#include "core/app_manager.h"
#include "core/events.h"
#include "core/navigation.h"
#include "ui/ui.h"

typedef struct core_context {
    core_event_bus_t events;
    core_nav_t nav;
    core_app_manager_t apps;
    core_animation_manager_t animations;
    ui_t *ui;
    uint8_t battery_percent;
    uint16_t battery_mv;
    bool battery_low;
    bool show_fps;
    uint32_t fps_frame_count;
    uint32_t fps_elapsed_ms;
} core_context_t;

void core_context_init(core_context_t *ctx, ui_t *ui);
