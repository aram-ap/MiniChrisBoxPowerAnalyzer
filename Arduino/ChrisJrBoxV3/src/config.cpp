/**
* @file config.cpp
 * @brief Configuration constant definitions
 */

#include "config.h"

// Define T9_LETTERS here, only once
const char* T9_LETTERS[] = {
    "-_ ", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz", ""
};

// Define keypad pin arrays here - FIXED: Moved from header
byte ROW_PINS[4] = {28, 27, 26, 25};
byte COL_PINS[4] = {32, 31, 30, 29};
const char KEYPAD_KEYS[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};
