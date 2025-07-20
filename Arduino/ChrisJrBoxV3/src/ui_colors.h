/**
 * @file ui_colors.h
 * @brief Simple UI Color Definitions
 * 
 * This file contains hardcoded color definitions for the UI.
 * Colors are organized by function and made easily accessible via macros.
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

#ifndef UI_COLORS_H
#define UI_COLORS_H

#include "config.h"
#include "rgb565_colors.h"
#include <cstdint>

// ============================================================================
// BACKGROUND COLORS
// ============================================================================
struct BackgroundColors {
    uint16_t main_screen = RGB565_Black;
    uint16_t dialog_screen = RGB565_Black;
    uint16_t settings_screen = RGB565_Black;
    uint16_t graph_screen = RGB565_Black;
    uint16_t script_screen = RGB565_Black;
    uint16_t data_row_off = RGB565_Black;
    uint16_t data_row_on = RGB565_Palatinate_purple;
    uint16_t data_row_alt1 = COLOR_DARK_ROW1;  // 0x2104
    uint16_t data_row_alt2 = COLOR_DARK_ROW2;  // 0x18C3
    uint16_t list_row_alt1 = COLOR_LIST_ROW1;  // 0x1082
    uint16_t list_row_alt2 = COLOR_LIST_ROW2;  // 0x0841
};

// ============================================================================
// TEXT COLORS
// ============================================================================
struct TextColors {
    uint16_t primary = RGB565_Black;
    uint16_t secondary = RGB565_Cadmium_yellow;
    uint16_t accent = RGB565_Cyan;
    uint16_t warning = RGB565_Cadmium_yellow;
    uint16_t error = RGB565_Rojo_spanish_red;
    uint16_t success = RGB565_Forest_green;
    uint16_t info = RGB565_Resolution_blue;
    uint16_t muted = RGB565_Gray_web;
    uint16_t device_on = RGB565_Forest_green;
    uint16_t device_off = RGB565_Gray_web;
    uint16_t voltage = RGB565_Green;
    uint16_t current = RGB565_Cyan;
    uint16_t power = RGB565_Yellow;
    uint16_t total = RGB565_White;
    uint16_t row = RGB565_White;
};

// ============================================================================
// BUTTON COLORS
// ============================================================================
struct ButtonColors {
    // Standard buttons
    uint16_t primary = RGB565_Cadmium_yellow;
    uint16_t secondary = RGB565_Dark_slate_gray;
    uint16_t danger = RGB565_Rojo_spanish_red;
    uint16_t success = RGB565_Forest_green;
    uint16_t warning = RGB565_Cadmium_yellow;
    uint16_t info = RGB565_Resolution_blue;
    
    // Button states
    uint16_t disabled = RGB565_Gray_web;
    uint16_t pressed = COLOR_BTN_PRESS;  // 0x3186
    uint16_t border = RGB565_Black;
    
    // Special buttons
    uint16_t record = RGB565_Resolution_blue;
    uint16_t recording = RGB565_Rojo_spanish_red;
    uint16_t stop = RGB565_Cadmium_yellow;
    uint16_t lock = RGB565_Palatinate;
    uint16_t unlock = RGB565_Cadmium_yellow;
    uint16_t script_start = RGB565_Forest_green;
    uint16_t script_stop = RGB565_Rojo_spanish_red;
    uint16_t script_pause = RGB565_Orange;
    uint16_t graph = RGB565_Dark_slate_gray;
    uint16_t settings = RGB565_Cadmium_yellow;
    uint16_t network = RGB565_Cadmium_yellow;
    uint16_t about = RGB565_Cadmium_yellow;
    uint16_t back = RGB565_Cadmium_yellow;
    uint16_t save = RGB565_Forest_green;
    uint16_t delete_btn = RGB565_Rojo_spanish_red;
    uint16_t select = RGB565_Forest_green;
    uint16_t yes = RGB565_Rojo_spanish_red;
    uint16_t no = RGB565_Cadmium_yellow;
    uint16_t load = RGB565_Cadmium_yellow;
    uint16_t edit = RGB565_Cadmium_yellow;
    uint16_t new_item = RGB565_Cadmium_yellow;
    uint16_t clear = RGB565_Cadmium_yellow;
    uint16_t pause = RGB565_Cadmium_yellow;
    uint16_t resume = RGB565_Orange;
    uint16_t refresh = RGB565_Cyan;
    uint16_t toggle_on = RGB565_Forest_green;
    uint16_t toggle_off = RGB565_Gray_web;
};

// ============================================================================
// STATUS COLORS
// ============================================================================
struct StatusColors {
    uint16_t online = RGB565_Forest_green;
    uint16_t offline = RGB565_Rojo_spanish_red;
    uint16_t warning = RGB565_Cadmium_yellow;
    uint16_t error = RGB565_Rojo_spanish_red;
    uint16_t info = RGB565_Cyan;
    uint16_t processing = RGB565_Cadmium_yellow;
    uint16_t success = RGB565_Forest_green;
    uint16_t idle = RGB565_Gray_web;
    uint16_t connected = RGB565_Forest_green;
    uint16_t disconnected = RGB565_Rojo_spanish_red;
    uint16_t available = RGB565_Forest_green;
    uint16_t unavailable = RGB565_Rojo_spanish_red;
    uint16_t enabled = RGB565_Forest_green;
    uint16_t disabled = RGB565_Gray_web;
    uint16_t locked = RGB565_Palatinate;
    uint16_t unlocked = RGB565_Cadmium_yellow;
    uint16_t recording = RGB565_Rojo_spanish_red;
    uint16_t not_recording = RGB565_Resolution_blue;
    uint16_t script_running = RGB565_Forest_green;
    uint16_t script_paused = RGB565_Orange;
    uint16_t script_stopped = RGB565_Gray_web;
};

// ============================================================================
// GRAPH COLORS
// ============================================================================
struct GraphColors {
    uint16_t background = RGB565_Black;
    uint16_t grid = COLOR_DARK_ROW1;  // 0x2104
    uint16_t axes = RGB565_Gray_web;
    uint16_t labels = RGB565_White;
    uint16_t title = RGB565_White;
    uint16_t legend = RGB565_White;
    uint16_t cursor = RGB565_Cyan;
    uint16_t selection = RGB565_Cadmium_yellow;
    uint16_t device_1 = RGB565_Red;      // GSE-1
    uint16_t device_2 = RGB565_Green;    // GSE-2
    uint16_t device_3 = RGB565_Blue;     // TE-R
    uint16_t device_4 = RGB565_Yellow;   // TE-1
    uint16_t device_5 = RGB565_Cyan;     // TE-2
    uint16_t device_6 = RGB565_Magenta;  // TE-3
    uint16_t current = RGB565_Cyan;
    uint16_t voltage = RGB565_Green;
    uint16_t power = RGB565_Yellow;
};

// ============================================================================
// THEME COLORS
// ============================================================================
struct ThemeColors {
    uint16_t primary = RGB565_Cadmium_yellow;
    uint16_t secondary = RGB565_Dark_slate_gray;
    uint16_t accent = RGB565_Cyan;
    uint16_t highlight = RGB565_Forest_green;
    uint16_t warning = RGB565_Cadmium_yellow;
    uint16_t error = RGB565_Rojo_spanish_red;
    uint16_t success = RGB565_Forest_green;
    uint16_t info = RGB565_Resolution_blue;
    uint16_t neutral = RGB565_Gray_web;
    uint16_t muted = RGB565_Dark_slate_gray;
};

// ============================================================================
// GLOBAL COLOR INSTANCES
// ============================================================================
extern BackgroundColors bg_colors;
extern TextColors text_colors;
extern ButtonColors btn_colors;
extern StatusColors status_colors;
extern GraphColors graph_colors;
extern ThemeColors theme_colors;

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================

// Quick access to commonly used colors
#define UI_BG_MAIN bg_colors.main_screen
#define UI_BG_DIALOG bg_colors.dialog_screen
#define UI_BG_DATA_OFF bg_colors.data_row_off
#define UI_BG_DATA_ON bg_colors.data_row_on

// Text colors
#define UI_TEXT_PRIMARY text_colors.primary
#define UI_TEXT_SECONDARY text_colors.secondary
#define UI_TEXT_ACCENT text_colors.accent
#define UI_TEXT_ERROR text_colors.error
#define UI_TEXT_SUCCESS text_colors.success
#define UI_TEXT_WARNING text_colors.warning
#define UI_TEXT_TOTAL text_colors.total
#define UI_TEXT_MUTED text_colors.muted
#define UI_TEXT_ROW text_colors.row

// Button colors
#define UI_BTN_PRIMARY btn_colors.primary
#define UI_BTN_DANGER btn_colors.danger
#define UI_BTN_SUCCESS btn_colors.success
#define UI_BTN_DISABLED btn_colors.disabled
#define UI_BTN_PRESSED btn_colors.pressed
#define UI_BTN_BORDER btn_colors.border
#define UI_BTN_RECORD btn_colors.record
#define UI_BTN_REFRESH btn_colors.refresh
#define UI_BTN_STOP btn_colors.stop
#define UI_BTN_LOCK btn_colors.lock
#define UI_BTN_SETTINGS btn_colors.settings
#define UI_BTN_RECORDING btn_colors.recording
#define UI_BTN_GRAPH btn_colors.graph
#define UI_BTN_NETWORK btn_colors.network
#define UI_BTN_ABOUT btn_colors.about
#define UI_BTN_INFO btn_colors.info

// Status colors
#define UI_STATUS_AVAILABLE status_colors.available
#define UI_STATUS_UNAVAILABLE status_colors.unavailable

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

// Initialize UI colors
void initUIColors();

#endif // UI_COLORS_H 