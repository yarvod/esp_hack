idf_component_register(
    SRCS
        "src/apps_registry.c"
        "src/boot_screen.c"
        "src/sd_flash_app.c"
        "src/home_screen.c"
        "src/simple_app_screen.c"
        "src/wifi_app.c"
        "src/bluetooth_app.c"
    INCLUDE_DIRS "include"
    REQUIRES core ui system
)
