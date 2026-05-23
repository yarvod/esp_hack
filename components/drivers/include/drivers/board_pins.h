#pragma once

#include "driver/gpio.h"

#define BOARD_PIN_OLED_SDA      GPIO_NUM_7
#define BOARD_PIN_OLED_SCL      GPIO_NUM_6

#define BOARD_PIN_JOY_X         GPIO_NUM_0
#define BOARD_PIN_JOY_Y         GPIO_NUM_1
#define BOARD_PIN_JOY_SW        GPIO_NUM_2

#define BOARD_PIN_BATTERY_ADC   GPIO_NUM_3

#define BOARD_PIN_SD_CS         GPIO_NUM_4
#define BOARD_PIN_SD_MOSI       GPIO_NUM_5
#define BOARD_PIN_SD_CLK        GPIO_NUM_18
#define BOARD_PIN_SD_MISO       GPIO_NUM_19

#define BOARD_BATTERY_DIVIDER_MULTIPLIER 2.0f
