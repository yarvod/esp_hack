#include "system/logger.h"

#include "esp_log.h"

esp_err_t system_logger_init(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("i2c", ESP_LOG_WARN);
    return ESP_OK;
}
