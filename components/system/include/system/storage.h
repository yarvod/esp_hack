#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    const char *mount_point;
    bool mounted;
} storage_state_t;

esp_err_t storage_init(void);
esp_err_t storage_mount_sd(void);
esp_err_t storage_unmount_sd(void);
storage_state_t storage_get_state(void);
