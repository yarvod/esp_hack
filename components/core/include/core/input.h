#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CORE_INPUT_NONE = 0,
    CORE_INPUT_UP,
    CORE_INPUT_DOWN,
    CORE_INPUT_LEFT,
    CORE_INPUT_RIGHT,
    CORE_INPUT_SELECT,
    CORE_INPUT_BACK,
} core_input_action_t;

typedef enum {
    CORE_INPUT_PHASE_PRESS = 0,
    CORE_INPUT_PHASE_REPEAT,
    CORE_INPUT_PHASE_RELEASE,
} core_input_phase_t;

typedef struct {
    core_input_action_t action;
    core_input_phase_t phase;
    int64_t timestamp_us;
} core_input_event_t;

static inline bool core_input_is_navigation(core_input_action_t action)
{
    return action == CORE_INPUT_UP || action == CORE_INPUT_DOWN ||
           action == CORE_INPUT_LEFT || action == CORE_INPUT_RIGHT;
}
