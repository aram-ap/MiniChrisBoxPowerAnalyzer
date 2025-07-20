/**
 * @file config.h
 * @brief System configuration and constants
 * 
 * MIT License
 * 
 * Copyright (c) 2025 Aram Aprahamian
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// System information - use #define to avoid multiple definitions
#define SOFTWARE_VERSION "Mini Chris Box V5.2 - Network and Graphs"

// EEPROM configuration
#define EEPROM_MAGIC_NUMBER 0xDEADBEEF
#define EEPROM_MAGIC_ADDR 0
#define EEPROM_VERSION_ADDR 4
#define EEPROM_VERSION_NUMBER 3  // Incremented for graph settings

// Performance timing
#define SENSOR_UPDATE_INTERVAL 50
#define DISPLAY_UPDATE_INTERVAL 170
#define LOG_WRITE_INTERVAL 50
#define NETWORK_CHECK_INTERVAL 5000
#define HEARTBEAT_INTERVAL 10000
#define SD_CHECK_INTERVAL 2000
#define GRAPH_UPDATE_INTERVAL 100

// Graph configuration - ADDED
#define GRAPH_MAX_POINTS 900

// Hardware pins
const int PWR_LED_PIN = 22;
const int LOCK_LED_PIN = 21;
const int STOP_LED_PIN = 20;
const int FAN_PWM_PIN = 33;

// Display pins
#define TFT_CS 10
#define TFT_DC 9
#define TFT_RST 7
#define TOUCH_CS 8
#define TOUCH_IRQ 14

// SD card pins
#define SD_CS 36
#define BUILTIN_SDCARD 254
#define SCRIPTS_DIR "/scripts"

// Screen dimensions
const int SCREEN_WIDTH = 480;
const int SCREEN_HEIGHT = 320;

// Main screen layout (modified for graph button)
#define MAIN_DATA_WIDTH 365
#define MAIN_BUTTON_COLUMN_X 375
#define MAIN_BUTTON_COLUMN_WIDTH 100

#define COLOR_BACKGROUND 0x0000
#define COLOR_LIGHT_BACKGROUND 0x8410
#define COLOR_PRIMARY_TEXT 0xFFFF
#define COLOR_SECONDARY_TEXT 0xC618
#define COLOR_PRIMARY_TEXT_DARK 0x3186
#define COLOR_SECONDARY_TEXT_DARK 0x8410
#define COLOR_PRIMARY 0xFFE0
#define COLOR_DISABLED 0x3186
#define COLOR_SECONDARY 0x780F
#define COLOR_TERTIARY 0x07FF
#define COLOR_QUATERNARY 0xF81F
#define COLOR_ALARM 0xF81F

// Color definitions
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_YELLOW    0xFFE0
#define COLOR_PURPLE    0x780F
#define COLOR_BTN_BG    0x2104
#define COLOR_BTN_DARK  0x18A3
#define COLOR_BTN_PRESS 0x3186
#define COLOR_RECORD    0x001F
#define COLOR_RECORDING 0xF800
#define COLOR_GRAY_DARK  0x3186
#define COLOR_GRAY      0x8410
#define COLOR_GRAY_LIGHT 0xC618
#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_CYAN      0x07FF
#define COLOR_BLUE      0x001F
#define COLOR_ORANGE    0xFD20
#define COLOR_DARK_ROW1 0x2104
#define COLOR_DARK_ROW2 0x18C3
#define COLOR_LIST_ROW1 0x1082
#define COLOR_LIST_ROW2 0x0841

// System limits
#define MAX_SCRIPTS 50
#define MAX_DEVICE_FIELDS 30
#define MAX_EDIT_FIELDS 10
#define MAX_NETWORK_FIELDS 10

// Keypad configuration - FIXED: Changed to extern declarations
extern byte ROW_PINS[4];
extern byte COL_PINS[4];
extern const char KEYPAD_KEYS[4][4];

// T9 character mapping
extern const char* T9_LETTERS[];  // Declare as extern to avoid redefinition

// EEPROM memory map
#define EEPROM_FAN_ON_ADDR     8
#define EEPROM_FAN_SPEED_ADDR  12
#define EEPROM_UPDATE_RATE_ADDR 16
#define EEPROM_TIME_FORMAT_ADDR 20
#define EEPROM_DARK_MODE_ADDR  24
#define EEPROM_NETWORK_CONFIG_ADDR 28
#define EEPROM_SORT_MODE_ADDR  60
#define EEPROM_GRAPH_SETTINGS_ADDR 64  // New address for graph settings
#define EEPROM_SNAKE_MAX_SCORE_ADDR 500  // Snake game max score

#endif // CONFIG_H
