#pragma once

#include <stddef.h>
#include "esp_err.h"

struct core_context;

typedef esp_err_t (*core_app_launch_fn)(struct core_context *ctx);

typedef struct {
    const char *id;
    const char *name;
    const char *icon;
    core_app_launch_fn launch;
} core_app_descriptor_t;

#define CORE_APP_MANAGER_MAX_APPS 16

typedef struct {
    const core_app_descriptor_t *apps[CORE_APP_MANAGER_MAX_APPS];
    size_t count;
} core_app_manager_t;

void core_app_manager_init(core_app_manager_t *manager);
esp_err_t core_app_manager_register(core_app_manager_t *manager, const core_app_descriptor_t *app);
const core_app_descriptor_t *core_app_manager_get(const core_app_manager_t *manager, size_t index);
const core_app_descriptor_t *core_app_manager_find(const core_app_manager_t *manager, const char *id);
size_t core_app_manager_count(const core_app_manager_t *manager);
