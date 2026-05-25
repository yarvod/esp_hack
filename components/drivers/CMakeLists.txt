idf_component_register(
    SRCS
        "src/analog.c"
        "src/battery.c"
        "src/joystick.c"
        "src/rgb_led.c"
        "src/sdcard.c"
        "src/ssd1306.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_adc fatfs sdmmc esp_timer esp_driver_i2c esp_driver_rmt
)
