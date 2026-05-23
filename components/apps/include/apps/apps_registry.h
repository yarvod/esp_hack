#pragma once

#include "esp_err.h"
#include "core/context.h"

esp_err_t apps_register_all(core_context_t *ctx);
esp_err_t apps_show_boot(core_context_t *ctx);
esp_err_t apps_show_home(core_context_t *ctx);
