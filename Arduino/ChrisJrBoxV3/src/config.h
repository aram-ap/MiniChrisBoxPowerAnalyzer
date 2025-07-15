/**
 * @file config.h
 * @brief System configuration and constants
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// System information - use macro instead of const char*
#define SOFTWARE_VERSION "Mini Chris Box V5.1 - Network Enabled"

// EEPROM configuration
#define EEPROM_MAGIC_NUMBER 0xDEADBEEF
#define EEPROM_MAGIC_ADDR 0
#define EEPROM_VERSION_ADDR 4
#define EEPROM_VERSION_NUMBER 2

// Performance timing
#define SENSOR_UPDATE_INTERVAL 50
#define DISPLAY_UPDATE_INTERVAL 200
#define LOG_WRITE_INTERVAL 50
#define NETWORK_CHECK_INTERVAL 5000
#define HEARTBEAT_INTERVAL 10000
#define SD_CHECK_INTERVAL 2000

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
#define COLOR_GRAY      0x8410
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

// Keypad configuration
const byte ROW_PINS[4] = {28, 27, 26, 25};
const byte COL_PINS[4] = {32, 31, 30, 29};
const char KEYPAD_KEYS[4][4] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

// T9 character mapping - declare as extern here
extern const char* T9_LETTERS[];

// EEPROM memory map
#define EEPROM_FAN_ON_ADDR     8
#define EEPROM_FAN_SPEED_ADDR  12
#define EEPROM_UPDATE_RATE_ADDR 16
#define EEPROM_TIME_FORMAT_ADDR 20
#define EEPROM_DARK_MODE_ADDR  24
#define EEPROM_NETWORK_CONFIG_ADDR 28
#define EEPROM_SORT_MODE_ADDR  60

#endif // CONFIG_H
