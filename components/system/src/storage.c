#include "system/storage.h"

#include "drivers/board_pins.h"
#include "drivers/sdcard.h"
#include "system/key_store.h"

esp_err_t storage_init(void)
{
    return key_store_init();
}

esp_err_t storage_mount_sd(void)
{
    sdcard_config_t cfg = {
        .cs = BOARD_PIN_SD_CS,
        .mosi = BOARD_PIN_SD_MOSI,
        .clk = BOARD_PIN_SD_CLK,
        .miso = BOARD_PIN_SD_MISO,
        .mount_point = "/sdcard",
        .max_freq_khz = 400,
        .format_if_mount_failed = false,
    };
    return sdcard_mount(&cfg);
}

esp_err_t storage_unmount_sd(void)
{
    return sdcard_unmount();
}

storage_state_t storage_get_state(void)
{
    storage_state_t state = {
        .mount_point = sdcard_mount_point(),
        .mounted = sdcard_is_mounted(),
    };
    return state;
}
