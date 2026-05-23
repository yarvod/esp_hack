#pragma once

#include <stdint.h>
#include "core/context.h"

void core_screen_manager_update(core_context_t *ctx, uint32_t dt_ms);
void core_screen_manager_render(core_context_t *ctx);
