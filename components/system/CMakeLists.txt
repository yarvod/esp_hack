idf_component_register(
    SRCS
        "src/logger.c"
        "src/key_store.c"
        "src/power_manager.c"
        "src/settings.c"
        "src/storage.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash drivers esp_driver_gpio esp_hw_support
)
