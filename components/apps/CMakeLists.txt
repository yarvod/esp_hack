idf_component_register(
    SRCS
        "src/apps_registry.c"
        "src/boot_screen.c"
        "src/rgb_light_app.c"
        "src/snake_app.c"
        "src/tetris_app.c"
        "src/breakout_app.c"
        "src/invaders_app.c"
        "src/sleep_app.c"
        "src/home_screen.c"
        "src/keys_app.c"
        "src/settings_app.c"
        "src/simple_app_screen.c"
        "src/wifi_app.c"
        "src/wifi_tools.c"
        "src/bluetooth_app.c"
    INCLUDE_DIRS "include"
    REQUIRES core ui system drivers nvs_flash esp_wifi esp_event bt esp_http_server json
)
