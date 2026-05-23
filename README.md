# ESP32-C6 SuperMini Handheld Framework

ESP-IDF firmware skeleton for a small OLED handheld device in the spirit of a Flipper-style tool. The project targets ESP32-C6 SuperMini with SSD1306 128x64 I2C display, analog joystick, SPI microSD and Li-Ion battery monitoring.

## Hardware Pins

| Peripheral | Signal | GPIO |
| --- | --- | --- |
| OLED SSD1306 | SDA | 7 |
| OLED SSD1306 | SCL | 6 |
| Joystick | VRX ADC | 0 |
| Joystick | VRY ADC | 1 |
| Joystick | SW pull-up | 2 |
| Battery divider | ADC | 3 |
| microSD SPI | CS | 4 |
| microSD SPI | MOSI | 5 |
| microSD SPI | CLK | 18 |
| microSD SPI | MISO | 19 |

Reserved pins intentionally not used: GPIO8, GPIO9, GPIO10, GPIO11, GPIO12, GPIO13, GPIO21, GPIO22, GPIO23.

## Project Tree

```text
.
├── CMakeLists.txt
├── main/
│   ├── CMakeLists.txt
│   └── main.c
└── components/
    ├── apps/
    │   ├── include/apps/apps_registry.h
    │   └── src/
    │       ├── apps_registry.c
    │       ├── boot_screen.c
    │       ├── home_screen.c
    │       ├── wifi_app.c
    │       ├── bluetooth_app.c
    │       ├── sd_flash_app.c
    │       └── simple_app_screen.c
    ├── core/
    │   ├── include/core/
    │   │   ├── animation.h
    │   │   ├── app_manager.h
    │   │   ├── context.h
    │   │   ├── events.h
    │   │   ├── input.h
    │   │   ├── input_dispatcher.h
    │   │   ├── navigation.h
    │   │   ├── screen.h
    │   │   └── screen_manager.h
    │   └── src/
    ├── drivers/
    │   ├── include/drivers/
    │   │   ├── analog.h
    │   │   ├── battery.h
    │   │   ├── board_pins.h
    │   │   ├── joystick.h
    │   │   ├── sdcard.h
    │   │   └── ssd1306.h
    │   └── src/
    ├── system/
    │   ├── include/system/
    │   └── src/
    └── ui/
        ├── include/ui/
        └── src/
```

## Architecture

- `drivers`: hardware-facing code only. SSD1306 framebuffer, shared ADC service, joystick polling with deadzone/repeat/long-press, battery smoothing, SD mount via `esp_vfs_fat_sdspi_mount`.
- `core`: event queue, input actions, app registry, navigation stack, screen lifecycle and animation state.
- `ui`: lightweight monochrome UI primitives, font, status bar, widgets, menu and carousel renderers.
- `apps`: boot screen, main carousel and initial WiFi/Bluetooth/SD flash app placeholders.
- `system`: logger, NVS-backed settings, power manager stub and storage abstraction.

Every screen uses the same lifecycle:

```c
on_enter -> on_input -> on_update -> on_render -> on_exit
```

Input is normalized to `UP`, `DOWN`, `LEFT`, `RIGHT`, `SELECT`, `BACK`, so the physical joystick can be replaced without changing application code.
Current joystick mapping: short press is `SELECT`, long press is system `BACK`.

## Build

```bash
. ~/esp/esp-idf/export.sh
idf.py set-target esp32c6
idf.py build
```

Flash:

```bash
idf.py -p PORT flash monitor
```

The current skeleton builds successfully with ESP-IDF 5.5.1.
