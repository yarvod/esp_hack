#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CORE_EASE_LINEAR = 0,
    CORE_EASE_OUT_CUBIC,
} core_easing_t;

typedef struct {
    bool active;
    int32_t from;
    int32_t to;
    int32_t value;
    uint32_t elapsed_ms;
    uint32_t duration_ms;
    core_easing_t easing;
} core_animation_t;

typedef struct {
    core_animation_t transition;
} core_animation_manager_t;

void core_animation_manager_init(core_animation_manager_t *manager);
void core_animation_start(core_animation_t *animation, int32_t from, int32_t to, uint32_t duration_ms, core_easing_t easing);
void core_animation_update(core_animation_t *animation, uint32_t dt_ms);
