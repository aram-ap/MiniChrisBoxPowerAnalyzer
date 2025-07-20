/**
 * @file display.cpp
 * @brief Display and GUI implementation
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

#include "display.h"
#include "config.h"
#include "time_utils.h"
#include "switches.h"
#include "sensors.h"
#include "script.h"
#include "settings.h"
#include "network.h"
#include "datalog.h"
#include "graphs.h"
#include "rgb565_colors.h"
#include "ui_colors.h"
// Remove color settings include
// #include "color_settings.h"
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <math.h>
#include <EEPROM.h>

using namespace qindesign::network;

// Display object
Adafruit_ST7796S tft = Adafruit_ST7796S(TFT_CS, TFT_DC, TFT_RST);

// External references
extern SystemState systemState;
extern GUIState guiState;
extern NetworkConfig networkConfig;
extern bool ethernetConnected;

// Snake game global variable
SnakeGame snakeGame;
extern NetworkInitState networkInitState;
extern bool isScriptRunning;
extern bool isScriptPaused;
extern unsigned long scriptStartMillis;
extern unsigned long scriptPausedTime;
extern unsigned long pauseStartMillis;
extern Script currentScript;

// Field arrays
static DeviceTimingField deviceFields[MAX_DEVICE_FIELDS];
static EditField editFields[MAX_EDIT_FIELDS];
static NetworkEditField networkFields[MAX_NETWORK_FIELDS];

// Button definitions - Main screen (modified layout)
ButtonRegion btnRecord     = {  5,  5,   120, 35, "RECORD",    false, UI_BTN_RECORD,    false };
ButtonRegion btnSDRefresh  = {130,  5,    40, 35, "SD",        false, UI_BTN_REFRESH,      true };
ButtonRegion btnStop       = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, UI_BTN_STOP, true };
ButtonRegion btnLock       = { SCREEN_WIDTH - 70, SCREEN_HEIGHT - 40, 65, 35, "LOCK",  false, UI_BTN_LOCK, true };
ButtonRegion btnAllOn      = {  5,  SCREEN_HEIGHT - 40, 80, 35, "ALL ON", false, UI_BTN_PRIMARY, true };
ButtonRegion btnAllOff     = { 90,  SCREEN_HEIGHT - 40, 80, 35, "ALL OFF",false, UI_BTN_PRIMARY, true };
ButtonRegion btnScript     = {175,  SCREEN_HEIGHT - 40, 60, 35, "Script", false, UI_BTN_PRIMARY, true };
ButtonRegion btnEdit       = {240,  SCREEN_HEIGHT - 40, 60, 35, "Edit",   false, UI_BTN_PRIMARY, true };
ButtonRegion btnSettings   = {305,  SCREEN_HEIGHT - 40, 75, 35, "Settings",false, UI_BTN_SETTINGS, true };

// New graph button in the right column
ButtonRegion btnGraph      = { MAIN_BUTTON_COLUMN_X, 50, MAIN_BUTTON_COLUMN_WIDTH, 35, "Graph", false, RGB565_Dark_slate_gray, true };

// Graph page buttons
ButtonRegion btnGraphBack     = {  5,  5,   80, 35, "Back",     false, RGB565_Cadmium_yellow, true };
ButtonRegion btnGraphStop     = { SCREEN_WIDTH - 85, SCREEN_HEIGHT - 40, 80, 35, "STOP", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnGraphClear    = {  5, SCREEN_HEIGHT - 40,  80, 35, "Clear",    false, RGB565_Cadmium_yellow, true };
ButtonRegion btnGraphPause    = { 90, SCREEN_HEIGHT - 40,  80, 35, "Pause",    false, RGB565_Cadmium_yellow, true };
ButtonRegion btnGraphSettings = {175, SCREEN_HEIGHT - 40,  80, 35, "Settings", false, RGB565_Cadmium_yellow, true };

// Graph settings buttons
ButtonRegion btnGraphSettingsBack = {  5,  5,   80, 35, "Back", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnGraphDataType     = {150, 60,  100, 30, "Current", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnGraphMinY = {
  /*x*/ 0, /*y*/ 0, /*w*/ 80, /*h*/ 30,
  /*label*/ "0.00",
  /*pressed*/ false,
  /*color*/ RGB565_Cadmium_yellow,
  /*enabled*/ true
};
ButtonRegion btnGraphMaxY = {
  0, 0, 80, 30,
  "0.00",
  false,
  RGB565_Cadmium_yellow,
  true
};

ButtonRegion btnGraphThickness = {
  0, 0, 60, 30,
  "1",
  false,
  RGB565_Cadmium_yellow,
  true
};

// ADDED: Missing button definitions
ButtonRegion btnGraphTimeRange = {0, 0, 80, 30, "60.00", false, RGB565_Cadmium_yellow, true};
// ButtonRegion btnGraphDisplay = {0, 0, 80, 30, "Display", false, RGB565_Cadmium_yellow, true};
// ButtonRegion btnGraphDisplayBack = {5, 5, 80, 35, "Back", false, RGB565_Cadmium_yellow, true};
// ButtonRegion btnGraphDataTypeFooter = {265, SCREEN_HEIGHT - 40, 67, 35, "Current", false, RGB565_Rojo_spanish_red, true};

extern ButtonRegion btnGraphDisplay;
extern ButtonRegion btnGraphDisplayBack;
extern ButtonRegion btnGraphDataTypeFooter;


// Settings panel buttons
ButtonRegion btnSettingsBack = { 5, 5, 80, 35, "Back", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnSettingsStop = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnNetwork = { 310, SCREEN_HEIGHT - 40, 78, 35, "Network", false, UI_BTN_NETWORK, true };
ButtonRegion btnAbout = { 390, SCREEN_HEIGHT - 40, 80, 35, "About", false, UI_BTN_ABOUT, true };

// Settings input buttons
ButtonRegion btnFanSpeedInput = { 320, 70, 80, 30, "", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnUpdateRateInput = { 320, 110, 80, 30, "", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnSetTimeDate = { 320, 150, 80, 30, "Set", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnTimeFormatToggle = { 320, 190, 80, 30, "24H", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnDarkModeToggle = { 320, 230, 80, 30, "ON", false, RGB565_Cadmium_yellow, true };

// Network settings buttons
ButtonRegion btnNetworkBack = { 5, 5, 80, 35, "Back", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnNetworkStop = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnNetworkEdit = { 320, SCREEN_HEIGHT - 40, 80, 35, "Edit", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnEnableLanToggle = { 320, 70, 80, 30, "ON", false, RGB565_Cadmium_yellow, true };

// Network edit buttons
ButtonRegion btnNetworkEditBack = { 5, 5, 80, 35, "Back", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnNetworkEditStop = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnNetworkEditSave = { 390, SCREEN_HEIGHT - 40, 80, 35, "Save", false, RGB565_Forest_green, true };
ButtonRegion btnDhcpToggle = { 190, 50, 80, 30, "ON", false, RGB565_Cadmium_yellow, true };

// About page buttons
ButtonRegion btnAboutBack = { 5, 5, 80, 35, "Back", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnAboutStop = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, RGB565_Cadmium_yellow, true };

// Script panel buttons
ButtonRegion btnScriptBack   = {  5, 5, 80, 35, "Back", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnScriptStop   = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnScriptLoad   = {  5,  SCREEN_HEIGHT - 40, 60, 35, "Load", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnScriptEdit   = { 70,  SCREEN_HEIGHT - 40, 60, 35, "Edit", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnScriptStart  = {135, SCREEN_HEIGHT - 40, 70, 35, "Start", false, RGB565_Forest_green,  true };
ButtonRegion btnScriptEnd    = {210, SCREEN_HEIGHT - 40, 50, 35,  "Stop",  false, RGB565_Rojo_spanish_red,    true };
ButtonRegion btnScriptRecord = {265, SCREEN_HEIGHT - 40, 80, 35, "Record", false, RGB565_Resolution_blue,  true };

// Edit panel buttons
ButtonRegion btnEditBack     = {  5, 5, 80, 35, "Back", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnEditStop     = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnEditLoad     = {  5,  SCREEN_HEIGHT - 40, 80, 35, "Load", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnEditSave     = {  90, SCREEN_HEIGHT - 40, 80, 35, "Save", false, RGB565_Cadmium_yellow, true };
ButtonRegion btnEditNew      = { 175, SCREEN_HEIGHT - 40, 80, 35, "New",  false, RGB565_Cadmium_yellow, true };

// Additional UI buttons
ButtonRegion btnKeypadBack = {5, 5, 80, 35, "Back", false, RGB565_Cadmium_yellow, true};
ButtonRegion btnEditSaveBack = {5, 5, 80, 35, "Back", false, RGB565_Cadmium_yellow, true};
ButtonRegion btnEditNameBack = {5, 5, 80, 35, "Back", false, RGB565_Cadmium_yellow, true};
ButtonRegion btnDateTimeBack = {5, 5, 80, 35, "Back", false, RGB565_Cadmium_yellow, true};
ButtonRegion btnEditFieldBack = {5, 5, 80, 35, "Back", false, RGB565_Cadmium_yellow, true};
ButtonRegion btnScriptSelect = {SCREEN_WIDTH - 85, SCREEN_HEIGHT - 40, 80, 35, "Select", false, RGB565_Forest_green, true};
ButtonRegion btnScriptDelete = {SCREEN_WIDTH - 170, SCREEN_HEIGHT - 40, 80, 35, "Delete", false, RGB565_Rojo_spanish_red, true};
ButtonRegion btnSortDropdown = {SCREEN_WIDTH - 100, 5, 95, 35, "Name", false, RGB565_Cadmium_yellow, true};
ButtonRegion btnDeleteYes = {150, 150, 80, 35, "Yes", false, RGB565_Rojo_spanish_red, true};
ButtonRegion btnDeleteNo = {250, 150, 80, 35, "No", false, RGB565_Cadmium_yellow, true};

void initDisplay() {
  tft.init(320, 480, 0, 0, ST7796S_BGR);
  tft.setRotation(1);
  // ADDED: SPI Optimization for faster refresh
  tft.setSPISpeed(30000000);
  applyDarkMode();
}

void updateDisplay(unsigned long currentMillis) {
  if (currentMillis - systemState.lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    updateDisplayElements();
    systemState.lastDisplayUpdate = currentMillis;
  }

  // Update clock/timer more frequently when script is running for better timing accuracy
  unsigned long clockUpdateInterval = isScriptRunning ? 100 : 1000; // 100ms for scripts, 1s for normal clock
  if (currentMillis - systemState.lastClockRefresh >= clockUpdateInterval) {
    systemState.lastClockRefresh = currentMillis;
    refreshHeaderClock();
  }
}

void drawButton(ButtonRegion& btn, uint16_t bgColor, uint16_t textColor,
                const char* label, bool pressed, bool enabled) {
  btn.color = bgColor;
  btn.pressed = pressed;
  btn.enabled = enabled;
  uint16_t fill = !enabled ? UI_BTN_DISABLED : (pressed ? UI_BTN_PRESSED : bgColor);
  tft.fillRect(btn.x, btn.y, btn.w, btn.h, fill);
  tft.drawRect(btn.x, btn.y, btn.w, btn.h, UI_BTN_BORDER);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(textColor);

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(label, btn.x, btn.y, &x1, &y1, &w, &h);
  int tx = btn.x + (btn.w - w) / 2;
  int ty = btn.y + (btn.h + h) / 2;
  tft.setCursor(tx, ty);
  tft.print(label);
}

void drawInitializationScreen() {
  tft.fillScreen(RGB565_Black);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(RGB565_White);
  int16_t x1, y1;
  uint16_t w, h;

  String title = "Mini Chris Box V5.2";
  tft.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_WIDTH - w) / 2, 80);
  tft.print(title);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(RGB565_Cadmium_yellow);

  String status = "Initializing...";
  tft.getTextBounds(status, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_WIDTH - w) / 2, 120);
  tft.print(status);

  // Show initialization progress
  tft.setTextColor(RGB565_Cyan);
  tft.setCursor(50, 160);
  tft.print("• Sensors initialized");
  tft.setCursor(50, 180);
  tft.print("• Display ready");
  tft.setCursor(50, 200);
  tft.print("• SD cards checked");
  tft.setCursor(50, 220);
  tft.print("• Graphs initialized");

  updateInitializationScreen();
}

void updateInitializationScreen() {
  unsigned long currentTime = millis();

  // Update network status from network module
  extern void updateNetworkInitStatus(unsigned long);
  extern String getNetworkInitStatusText();
  updateNetworkInitStatus(currentTime);

  String statusText = getNetworkInitStatusText();
  uint16_t statusColor = RGB565_Gray_web;

  if (networkConfig.enableEthernet) {
    switch (networkInitState) {
      case NET_IDLE:
      case NET_CHECKING_LINK:
      case NET_INITIALIZING:
      case NET_DHCP_WAIT:
        statusColor = RGB565_Cadmium_yellow;
      break;
      case NET_INITIALIZED:
        statusColor = RGB565_Forest_green;
      break;
      case NET_FAILED:
        statusColor = RGB565_Rojo_spanish_red;
      break;
    }
  }

  // Update status text in right column
  static String lastStatusText = "";
  if (statusText != lastStatusText) {
    tft.fillRect(250, 160, 200, 100, RGB565_Black);  // Right column
    tft.setTextColor(statusColor);
    tft.setCursor(250, 180);
    tft.print(statusText);
    lastStatusText = statusText;
  }

  // Show completion message in right column
  if (networkInitState == NET_INITIALIZED || networkInitState == NET_FAILED || !networkConfig.enableEthernet) {
    tft.setTextColor(RGB565_Apple_green);
    tft.setCursor(250, 210);
    tft.print("Network Ready!");

    if (networkInitState == NET_INITIALIZED) {
      tft.setTextColor(RGB565_Cyan);
      tft.setCursor(250, 230);
      tft.print("IP: " + ipToString(Ethernet.localIP()));
      tft.setCursor(250, 250);
      tft.print("TCP: " + String(networkConfig.tcpPort));
      tft.setCursor(250, 270);
      tft.print("UDP: " + String(networkConfig.udpPort));
    }
  }
}

void drawMainScreen() {
  tft.fillScreen(UI_BG_MAIN);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(UI_TEXT_PRIMARY);
  int16_t x1, y1;
  uint16_t w, h;

  // Display time or script time
  if (!isScriptRunning) {
    String nowStr = getCurrentTimeString();
    tft.getTextBounds(nowStr, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((SCREEN_WIDTH - w) / 2, 30);  // Center on full screen width
    tft.print(nowStr);
  } else {
    char buff[32];
    // Calculate more precise timing for main screen display
    unsigned long totalPausedTime = scriptPausedTime;
    if (isScriptPaused) {
      totalPausedTime += (millis() - pauseStartMillis);
    }
    unsigned long msSinceStart = millis() - scriptStartMillis - totalPausedTime;
    long preciseTime = currentScript.tStart + (long)((msSinceStart + 500) / 1000); // Round to nearest second
    
    if (preciseTime < 0) {
      snprintf(buff, sizeof(buff), "T-%ld", labs(preciseTime));
    } else {
      snprintf(buff, sizeof(buff), "T+%ld", preciseTime);
    }
    tft.getTextBounds(buff, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((SCREEN_WIDTH - w) / 2, 30);  // Center on full screen width
    tft.print(buff);
  }

  // Draw control buttons
  drawButton(btnRecord,
             !systemState.sdAvailable ? UI_BTN_DISABLED: (systemState.recording ? UI_BTN_RECORDING: UI_BTN_RECORD),
             UI_TEXT_PRIMARY,
             systemState.recording ? "RECORDING" : "RECORD",
             false,
             systemState.sdAvailable);

  drawButton(btnSDRefresh,
             systemState.sdAvailable ? UI_STATUS_AVAILABLE : UI_STATUS_UNAVAILABLE,
             UI_TEXT_PRIMARY,
             "SD",
             false,
             true);

  drawButton(btnStop, UI_BTN_STOP, UI_TEXT_PRIMARY, "STOP");
  drawButton(btnAllOn, isScriptRunning ? UI_BTN_DISABLED : UI_BTN_PRIMARY,
                        UI_TEXT_PRIMARY,
                        "ALL ON", false, !isScriptRunning);
  drawButton(btnAllOff, isScriptRunning ? UI_BTN_DISABLED : UI_BTN_PRIMARY,
                         UI_TEXT_PRIMARY,
                         "ALL OFF", false, !isScriptRunning);
  drawButton(btnScript, UI_BTN_PRIMARY, UI_TEXT_PRIMARY, "Script");
  drawButton(btnEdit, UI_BTN_PRIMARY, UI_TEXT_PRIMARY, "Edit");
  updateLockButton();
  drawButton(btnSettings, UI_BTN_SETTINGS, UI_TEXT_PRIMARY, "Settings", false, btnSettings.enabled);

  // Draw new graph button in right column
  drawButton(btnGraph, UI_BTN_GRAPH, UI_TEXT_PRIMARY, "Graph");

  // Draw vertical separator line
  tft.drawLine(MAIN_BUTTON_COLUMN_X - 5, 40, MAIN_BUTTON_COLUMN_X - 5, SCREEN_HEIGHT - 45, UI_TEXT_MUTED);

  // Draw data table header (in reduced width area)
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(UI_TEXT_PRIMARY);

  tft.setCursor(10, 60);
  tft.print("Output");
  tft.setCursor(100, 60);
  tft.print("V");
  tft.setCursor(175, 60);
  tft.print("I (A)");
  tft.setCursor(270, 60);
  tft.print("P (W)");

  tft.drawLine(5, 65, MAIN_DATA_WIDTH, 65, UI_TEXT_MUTED);

  // Draw device rows
  for (int i = 0; i < numSwitches; i++) {
    drawDeviceRow(i);
  }

  // Draw total row
  int totalRowY = 85 + numSwitches * 25 + 10;
  tft.drawLine(5, totalRowY - 5, MAIN_DATA_WIDTH, totalRowY - 5, UI_TEXT_MUTED);
  drawTotalRow();
}

void drawSettingsPanel() {
  tft.fillScreen(RGB565_Black);

  drawButton(btnSettingsBack, RGB565_Cadmium_yellow, RGB565_Black, "Back", false, true);
  drawButton(btnSettingsStop, RGB565_Cadmium_yellow, RGB565_Black, "STOP", false, true);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(RGB565_White);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds("Settings", 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_WIDTH - w) / 2, 32);
  tft.print("Settings");

  tft.setFont(&FreeSans9pt7b);

  // Fan speed setting
  tft.fillRect(20, 70, 460, 30, COLOR_DARK_ROW1);
  tft.setTextColor(RGB565_White);
  tft.setCursor(30, 90);
  tft.print("Fan Speed (0-255):");
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", systemState.fanSpeed);
  drawButton(btnFanSpeedInput, RGB565_Cadmium_yellow, RGB565_Black, buf, false, true);

  // Update rate setting
  tft.fillRect(20, 110, 460, 30, COLOR_DARK_ROW2);
  tft.setTextColor(RGB565_White);
  tft.setCursor(30, 130);
  tft.print("Update Rate (ms):");
  char buf2[12];
  snprintf(buf2, sizeof(buf2), "%lu", systemState.updateRate);
  drawButton(btnUpdateRateInput, RGB565_Cadmium_yellow, RGB565_Black, buf2, false, true);

  // RTC clock setting
  tft.fillRect(20, 150, 460, 30, COLOR_DARK_ROW1);
  tft.setTextColor(RGB565_White);
  tft.setCursor(30, 170);
  tft.print("RTC Clock:");
  drawButton(btnSetTimeDate, RGB565_Cadmium_yellow, RGB565_Black, "Set", false, true);

  // Time format setting
  tft.fillRect(20, 190, 460, 30, COLOR_DARK_ROW2);
  tft.setTextColor(RGB565_White);
  tft.setCursor(30, 210);
  tft.print("Time Format:");
  drawButton(btnTimeFormatToggle, RGB565_Cadmium_yellow, RGB565_Black, systemState.use24HourFormat ? "24H" : "12H", false, true);

  // Dark mode setting
  tft.fillRect(20, 230, 460, 30, COLOR_DARK_ROW1);
  tft.setTextColor(RGB565_White);
  tft.setCursor(30, 250);
  tft.print("Dark Mode:");
  drawButton(btnDarkModeToggle, RGB565_Cadmium_yellow, RGB565_Black, systemState.darkMode ? "ON" : "OFF", false, true);

  // Additional settings buttons
  drawButton(btnNetwork, RGB565_Cadmium_yellow, RGB565_Black, "Network", false, true);
  drawButton(btnAbout, RGB565_Cadmium_yellow, RGB565_Black, "About", false, true);

  // Show current date/time
  tft.setTextColor(RGB565_Gray_web);
  tft.setCursor(30, 280);
  tft.print(formatDateString(now()));
  tft.print(" ");
  tft.print(getCurrentTimeString());
}

void drawNetworkPanel() {
  tft.fillScreen(RGB565_Black);

  drawButton(btnNetworkBack, RGB565_Cadmium_yellow, RGB565_Black, "Back", false, true);
  drawButton(btnNetworkStop, RGB565_Cadmium_yellow, RGB565_Black, "STOP", false, true);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(RGB565_White);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds("Network Settings", 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_WIDTH - w) / 2, 32);
  tft.print("Network Settings");

  tft.setFont(&FreeSans9pt7b);

  // Enable LAN setting
  tft.fillRect(20, 70, 460, 30, COLOR_DARK_ROW1);
  tft.setTextColor(RGB565_White);
  tft.setCursor(30, 90);
  tft.print("Enable LAN:");
  drawButton(btnEnableLanToggle, RGB565_Cadmium_yellow, RGB565_Black, networkConfig.enableEthernet ? "ON" : "OFF", false, true);

  // Connection status
  tft.fillRect(20, 110, 460, 30, COLOR_DARK_ROW2);
  tft.setTextColor(RGB565_White);
  tft.setCursor(30, 130);
  tft.print("Connection:");
  tft.setTextColor(ethernetConnected ? RGB565_Green: RGB565_Rojo_spanish_red);
  tft.setCursor(130, 130);
  tft.print(ethernetConnected ? "Connected" : "Disconnected");

  // Network information (if connected)
  if (ethernetConnected) {
    tft.fillRect(20, 150, 460, 30, COLOR_DARK_ROW1);
    tft.setTextColor(RGB565_White);
    tft.setCursor(30, 170);
    tft.print("IP Address:");
    tft.setTextColor(RGB565_Cyan);
    tft.setCursor(125, 170);
    tft.print(ipToString(Ethernet.localIP()));

    tft.fillRect(20, 190, 460, 30, COLOR_DARK_ROW2);
    tft.setTextColor(RGB565_White);
    tft.setCursor(30, 210);
    tft.print("TCP Port:");
    tft.setTextColor(RGB565_Cyan);
    tft.setCursor(120, 210);
    tft.print(networkConfig.tcpPort);
    tft.setTextColor(RGB565_White);
    tft.setCursor(200, 210);
    tft.print("UDP Port:");
    tft.setTextColor(RGB565_Cyan);
    tft.setCursor(290, 210);
    tft.print(networkConfig.udpPort);
  }

  drawButton(btnNetworkEdit, RGB565_Cadmium_yellow, RGB565_Black, "Edit", false, true);
}

void drawNetworkEditPanel() {
  tft.fillScreen(RGB565_Black);

  drawButton(btnNetworkEditBack, RGB565_Cadmium_yellow, RGB565_Black, "Back", false, true);
  drawButton(btnNetworkEditStop, RGB565_Cadmium_yellow, RGB565_Black, "STOP", false, true);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(RGB565_White);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds("Network Configuration", 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_WIDTH - w) / 2, 32);
  tft.print("Network Configuration");

  tft.setFont(&FreeSans9pt7b);

  // DHCP toggle
  tft.fillRect(20, 50, 460, 30, COLOR_DARK_ROW1);
  tft.setTextColor(RGB565_White);
  tft.setCursor(30, 70);
  tft.print("Use DHCP:");
  drawButton(btnDhcpToggle, RGB565_Cadmium_yellow, RGB565_Black, networkConfig.useDHCP ? "ON" : "OFF", false, true);

  // Static IP configuration (if DHCP disabled)
  if (!networkConfig.useDHCP) {
    tft.setTextColor(RGB565_White);
    tft.setCursor(30, 110);
    tft.print("Static IP:");
    tft.drawRect(networkFields[0].x, networkFields[0].y, networkFields[0].w, networkFields[0].h, RGB565_Cadmium_yellow);
    tft.setCursor(networkFields[0].x + 5, networkFields[0].y + 18);
    tft.print(networkFields[0].value);

    tft.setCursor(30, 140);
    tft.print("Subnet:");
    tft.drawRect(networkFields[1].x, networkFields[1].y, networkFields[1].w, networkFields[1].h, RGB565_Cadmium_yellow);
    tft.setCursor(networkFields[1].x + 5, networkFields[1].y + 18);
    tft.print(networkFields[1].value);

    tft.setCursor(30, 170);
    tft.print("Gateway:");
    tft.drawRect(networkFields[2].x, networkFields[2].y, networkFields[2].w, networkFields[2].h, RGB565_Cadmium_yellow);
    tft.setCursor(networkFields[2].x + 5, networkFields[2].y + 18);
    tft.print(networkFields[2].value);

    tft.setCursor(30, 200);
    tft.print("DNS:");
    tft.drawRect(networkFields[3].x, networkFields[3].y, networkFields[3].w, networkFields[3].h, RGB565_Cadmium_yellow);
    tft.setCursor(networkFields[3].x + 5, networkFields[3].y + 18);
    tft.print(networkFields[3].value);
  }

  // Port configuration
  tft.setTextColor(RGB565_White);
  tft.setCursor(30, 230);
  tft.print("TCP Port:");
  tft.drawRect(networkFields[4].x, networkFields[4].y, networkFields[4].w, networkFields[4].h, RGB565_Cadmium_yellow);
  tft.setCursor(networkFields[4].x + 5, networkFields[4].y + 18);
  tft.print(networkFields[4].value);

  tft.setCursor(30, 260);
  tft.print("UDP Port:");
  tft.drawRect(networkFields[5].x, networkFields[5].y, networkFields[5].w, networkFields[5].h, RGB565_Cadmium_yellow);
  tft.setCursor(networkFields[5].x + 5, networkFields[5].y + 18);
  tft.print(networkFields[5].value);

  // Timeout configuration
  tft.setCursor(30, 290);
  tft.print("Timeout (ms):");
  tft.drawRect(networkFields[6].x, networkFields[6].y, networkFields[6].w, networkFields[6].h, RGB565_Cadmium_yellow);
  tft.setCursor(networkFields[6].x + 5, networkFields[6].y + 18);
  tft.print(networkFields[6].value);

  tft.setCursor(30, 310);
  tft.print("DHCP Timeout:");
  tft.drawRect(networkFields[7].x, networkFields[7].y, networkFields[7].w, networkFields[7].h, RGB565_Cadmium_yellow);
  tft.setCursor(networkFields[7].x + 5, networkFields[7].y + 18);
  tft.print(networkFields[7].value);

  drawButton(btnNetworkEditSave, RGB565_Green, RGB565_Black, "Save", false, true);
}

void drawAboutPage() {
  tft.fillScreen(RGB565_Black);

  drawButton(btnAboutBack, RGB565_Cadmium_yellow, RGB565_Black, "Back", false, true);
  drawButton(btnAboutStop, RGB565_Cadmium_yellow, RGB565_Black, "STOP", false, true);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(RGB565_White);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds("About", 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_WIDTH - w) / 2, 32);
  tft.print("About");

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(RGB565_White);
  int xOffset = 20;

  tft.setCursor(xOffset, 70);
  tft.print(SOFTWARE_VERSION);

  tft.setCursor(xOffset, 95);
  tft.print("Designed by Aram Aprahamian");

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(RGB565_Gray_web);

  tft.setCursor(xOffset, 125);
  tft.print("Copyright (c) 2025 Aram Aprahamian");

  tft.setCursor(xOffset, 145);
  tft.print("Permission is hereby granted, free of charge, to any");
  tft.setCursor(xOffset, 160);
  tft.print("person obtaining a copy of this device design and");
  tft.setCursor(xOffset, 175);
  tft.print("software, to deal in the device and software without");
  tft.setCursor(xOffset, 190);
  tft.print("restriction, including without limitation the rights");
  tft.setCursor(xOffset, 205);
  tft.print("to use, copy, modify, merge, publish, distribute,");
  tft.setCursor(xOffset, 220);
  tft.print("sublicense, and/or sell copies of the device and");
  tft.setCursor(xOffset, 235);
  tft.print("software, subject to the following conditions:");
  tft.setCursor(xOffset, 250);
  tft.print("The above copyright notice and this permission");
  tft.setCursor(xOffset, 265);
  tft.print("notice must be included in all copies.");

  // Draw secret button if sequence was entered
  if (guiState.showSecretButton) {
    tft.fillRect(380, 280, 90, 35, UI_BTN_PRIMARY);
    tft.drawRect(380, 280, 90, 35, UI_BTN_BORDER);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(UI_TEXT_PRIMARY);
    tft.setCursor(402, 302);
    tft.print("Secret");
  }
}

void drawKeypadPanel() {
  tft.fillScreen(RGB565_Black);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(RGB565_White);
  tft.setCursor(40, 60);

  // Display appropriate prompt based on keypad mode
  switch (guiState.keypadMode) {
    case KEYPAD_UPDATE_RATE:
      tft.print("Enter Update Rate (ms):");
      break;
    case KEYPAD_FAN_SPEED:
      tft.print("Enter Fan Speed (0-255):");
      break;
    case KEYPAD_SCRIPT_TSTART:
      tft.print("Enter Start Time (can be negative):");
      break;
    case KEYPAD_SCRIPT_TEND:
      tft.print("Enter End Time:");
      break;
    case KEYPAD_DEVICE_ON_TIME:
      tft.print("Enter ON Time (seconds):");
      break;
    case KEYPAD_DEVICE_OFF_TIME:
      tft.print("Enter OFF Time (seconds):");
      break;
    case KEYPAD_SCRIPT_SEARCH:
      tft.print("Enter Script Number:");
      break;
    case KEYPAD_NETWORK_IP:
      tft.print("Enter IP Address:");
      break;
    case KEYPAD_NETWORK_PORT:
      tft.print("Enter Port Number:");
      break;
    case KEYPAD_NETWORK_TIMEOUT:
      tft.print("Enter Timeout (ms):");
      break;
    case KEYPAD_GRAPH_MIN_Y:
      tft.print("Enter Min Y Value:");
      break;
    case KEYPAD_GRAPH_MAX_Y:
      tft.print("Enter Max Y Value:");
      break;
    case KEYPAD_GRAPH_TIME_RANGE:
      tft.print("Enter Time Range (sec):");
      break;
    case KEYPAD_GRAPH_MAX_POINTS:
      tft.print("Enter Max Points:");
      break;
    case KEYPAD_GRAPH_REFRESH_RATE:
      tft.print("Enter Refresh Rate (ms):");
      break;
    default:
      tft.print("Enter Value:");
      break;
  }

  // Display current input
  tft.setFont(&FreeMonoBold9pt7b);
  tft.setCursor(40, 100);
  tft.print(guiState.keypadBuffer);

  // Display help text
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(40, 170);

  if (guiState.keypadMode == KEYPAD_DEVICE_ON_TIME || guiState.keypadMode == KEYPAD_DEVICE_OFF_TIME ||
      guiState.keypadMode == KEYPAD_SCRIPT_TSTART || guiState.keypadMode == KEYPAD_SCRIPT_TEND ||
      guiState.keypadMode == KEYPAD_UPDATE_RATE || guiState.keypadMode == KEYPAD_FAN_SPEED ||
      guiState.keypadMode == KEYPAD_NETWORK_PORT || guiState.keypadMode == KEYPAD_NETWORK_TIMEOUT ||
      guiState.keypadMode == KEYPAD_GRAPH_MIN_Y || guiState.keypadMode == KEYPAD_GRAPH_MAX_Y ||
      guiState.keypadMode == KEYPAD_GRAPH_TIME_RANGE || guiState.keypadMode == KEYPAD_GRAPH_MAX_POINTS ||
      guiState.keypadMode == KEYPAD_GRAPH_REFRESH_RATE || guiState.keypadMode == KEYPAD_GRAPH_INTERPOLATION_TENSION ||
      guiState.keypadMode == KEYPAD_GRAPH_INTERPOLATION_CURVESCALE) {
    tft.print("[*]=Backspace, [#]=+/-");
    tft.setCursor(40, 190);
    tft.print("[A]=Enter, [B]=Back, [C]=Clear, [D]=Decimal");
  } else if (guiState.keypadMode == KEYPAD_NETWORK_IP) {
    tft.print("[*]=Backspace, [D]=Decimal, [A]=Enter, [B]=Back, [C]=Clear");
  } else {
    tft.print("[A]=Enter, [B]=Back, [*]=Clear");
  }

  if (guiState.keypadMode == KEYPAD_SCRIPT_SEARCH) {
    tft.setCursor(40, 200);
    tft.print("Enter script number (1-");
    tft.print(numScripts);
    tft.print(")");
  }

  drawButton(btnKeypadBack, RGB565_Cadmium_yellow, RGB565_Black, "Back");
}

void drawScriptPage() {
  tft.fillScreen(RGB565_Black);

  drawButton(btnScriptBack, RGB565_Cadmium_yellow, RGB565_Black, "Back");
  drawButton(btnScriptStop, RGB565_Cadmium_yellow, RGB565_Black, "STOP");

  tft.setTextColor(RGB565_White);
  char buff[64];

  // Display script name and status - FIX DUPLICATE HEADER
  if (!isScriptRunning) {
    tft.setFont(&FreeSansBold12pt7b);
    snprintf(buff, sizeof(buff), "%s", currentScript.scriptName);
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(buff, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((SCREEN_WIDTH - w) / 2, 30);  // Only draw once, centered
    tft.print(buff);
  } else {
    tft.setFont(&FreeSans9pt7b);
    // Calculate more precise timing for script page display
    unsigned long totalPausedTime = scriptPausedTime;
    if (isScriptPaused) {
      totalPausedTime += (millis() - pauseStartMillis);
    }
    unsigned long msSinceStart = millis() - scriptStartMillis - totalPausedTime;
    long preciseTime = currentScript.tStart + (long)((msSinceStart + 500) / 1000); // Round to nearest second
    
    if (preciseTime < 0) {
      snprintf(buff, sizeof(buff), "%s - T-%ld", currentScript.scriptName, labs(preciseTime));
    } else {
      snprintf(buff, sizeof(buff), "%s - T+%ld", currentScript.scriptName, preciseTime);
    }
    if (isScriptPaused) {
      strcat(buff, " (PAUSED)");
    }
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(buff, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((SCREEN_WIDTH - w) / 2, 25);  // Only draw once, centered
    tft.print(buff);
  }

  int divX = SCREEN_WIDTH * 2 / 3;
  tft.drawLine(divX, 45, divX, SCREEN_HEIGHT - 45, RGB565_Gray_web);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(RGB565_White);

  // Script configuration table
  tft.setCursor(10, 70);
  tft.print("Name");
  tft.setCursor(80, 70);
  tft.print("On (s)");
  tft.setCursor(140, 70);
  tft.print("Off (s)");
  tft.setCursor(200, 70);
  tft.print("Use");

  int baseY = 90;
  int rowHeight = 25;

  for (int i = 0; i < 6; i++) {
    int yPos = baseY + i * rowHeight;

    tft.setCursor(10, yPos + 15);
    tft.print(switchOutputs[i].name);

    tft.setCursor(80, yPos + 15);
    if (currentScript.devices[i].enabled) {
      tft.print(currentScript.devices[i].onTime);
    } else {
      tft.print("-");
    }

    tft.setCursor(140, yPos + 15);
    if (currentScript.devices[i].enabled) {
      tft.print(currentScript.devices[i].offTime);
    } else {
      tft.print("-");
    }

    tft.setCursor(200, yPos + 15);
    tft.print(currentScript.devices[i].enabled ? "Y" : "N");
  }

  tft.drawLine(10, baseY + 6 * rowHeight + 10, divX - 10, baseY + 6 * rowHeight + 10, RGB565_Gray_web);

  // Script parameters
  int configY = baseY + 6 * rowHeight + 25;
  tft.setCursor(10, configY);
  tft.print("Start: ");
  tft.print(currentScript.tStart);
  tft.print("  Stop: ");
  tft.print(currentScript.tEnd);
  tft.print("  Record: ");
  tft.print(currentScript.useRecord ? "Yes" : "No");

  // Device status display
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(RGB565_Cadmium_yellow);
  tft.setCursor(divX + 10, 60);
  tft.print("Device Status");

  for (int i = 0; i < numSwitches; i++) {
    int yPos = 85 + i * 35;
    if (yPos > SCREEN_HEIGHT - 50) break;

    tft.setCursor(divX + 10, yPos);
    bool st = switchOutputs[i].state == HIGH;
    tft.setTextColor(st ? RGB565_Forest_green : RGB565_Rojo_spanish_red);
    tft.print(switchOutputs[i].name);
    tft.print(": ");
    tft.print(st ? "ON" : "OFF");

    int inaIdx = getInaIndexForSwitch(i);
    if (inaIdx >= 0) {
      tft.setCursor(divX + 10, yPos + 15);
      tft.setTextColor(RGB565_Cyan);
      tft.printf("%.1fV %.2fA", deviceVoltage[inaIdx], deviceCurrent[inaIdx] / 1000.0);
    }
  }

  // Control buttons
  drawButton(btnScriptLoad, RGB565_Cadmium_yellow, RGB565_Black, "Load");
  drawButton(btnScriptEdit, RGB565_Cadmium_yellow, RGB565_Black, "Edit");

  if (!isScriptRunning) {
    drawButton(btnScriptStart, RGB565_Forest_green, RGB565_Black, "Start", false, !systemState.safetyStop);
  } else if (isScriptPaused) {
    drawButton(btnScriptStart, COLOR_ORANGE, RGB565_Black, "Resume", false, true);
  } else {
    drawButton(btnScriptStart, COLOR_ORANGE, RGB565_Black, "Pause", false, true);
  }

  drawButton(btnScriptEnd, RGB565_Rojo_spanish_red, RGB565_Black, "Stop", false, isScriptRunning);

  if (systemState.recording && systemState.recordingScript) {
    drawButton(btnScriptRecord, RGB565_Rojo_spanish_red, RGB565_White, "Stop Rec", false, true);
  } else {
    drawButton(btnScriptRecord, currentScript.useRecord ? RGB565_Resolution_blue : RGB565_Gray_web, RGB565_White, "Record", false, true);
  }
}

void drawEditPage() {
  tft.fillScreen(RGB565_Black);

  drawButton(btnEditBack, RGB565_Cadmium_yellow, RGB565_Black, "Back");
  drawButton(btnEditStop, RGB565_Cadmium_yellow, RGB565_Black, "STOP");

  // Script name display
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(RGB565_White);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(currentScript.scriptName, 0, 0, &x1, &y1, &w, &h);
  int nameX = (SCREEN_WIDTH - w) / 2;
  tft.drawRect(nameX - 5, 10, w + 10, 25, RGB565_Cadmium_yellow);
  tft.setCursor(nameX, 30);
  tft.print(currentScript.scriptName);

  int divX = SCREEN_WIDTH * 2 / 3;
  tft.drawLine(divX, 45, divX, SCREEN_HEIGHT - 45, RGB565_Gray_web);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(RGB565_White);

  // Device configuration table
  tft.setCursor(10, 70);
  tft.print("Name");
  tft.setCursor(80, 70);
  tft.print("On (s)");
  tft.setCursor(140, 70);
  tft.print("Off (s)");
  tft.setCursor(200, 70);
  tft.print("Use");

  guiState.numDeviceFields = 0;

  int baseY = 90;
  int rowHeight = 30;

  for (int i = 0; i < 6; i++) {
    int yPos = baseY + i * rowHeight;

    tft.setCursor(10, yPos + 15);
    tft.print(switchOutputs[i].name);

    // ON time field
    int fieldX = 80;
    int fieldW = 50;
    int fieldH = 25;
    tft.drawRect(fieldX, yPos, fieldW, fieldH, RGB565_Cadmium_yellow);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", currentScript.devices[i].onTime);
    tft.setCursor(fieldX + 5, yPos + 18);
    tft.print(buf);

    deviceFields[guiState.numDeviceFields].x = fieldX;
    deviceFields[guiState.numDeviceFields].y = yPos;
    deviceFields[guiState.numDeviceFields].w = fieldW;
    deviceFields[guiState.numDeviceFields].h = fieldH;
    deviceFields[guiState.numDeviceFields].deviceIndex = i;
    deviceFields[guiState.numDeviceFields].fieldType = 0;
    guiState.numDeviceFields++;

    // OFF time field
    fieldX = 140;
    tft.drawRect(fieldX, yPos, fieldW, fieldH, RGB565_Cadmium_yellow);
    snprintf(buf, sizeof(buf), "%d", currentScript.devices[i].offTime);
    tft.setCursor(fieldX + 5, yPos + 18);
    tft.print(buf);

    deviceFields[guiState.numDeviceFields].x = fieldX;
    deviceFields[guiState.numDeviceFields].y = yPos;
    deviceFields[guiState.numDeviceFields].w = fieldW;
    deviceFields[guiState.numDeviceFields].h = fieldH;
    deviceFields[guiState.numDeviceFields].deviceIndex = i;
    deviceFields[guiState.numDeviceFields].fieldType = 1;
    guiState.numDeviceFields++;

    // Enable checkbox
    fieldX = 200;
    fieldW = 25;
    tft.drawRect(fieldX, yPos, fieldW, fieldH, RGB565_Cadmium_yellow);
    if (currentScript.devices[i].enabled) {
      tft.fillRect(fieldX + 5, yPos + 5, fieldW - 10, fieldH - 10, RGB565_Cadmium_yellow);
    }

    deviceFields[guiState.numDeviceFields].x = fieldX;
    deviceFields[guiState.numDeviceFields].y = yPos;
    deviceFields[guiState.numDeviceFields].w = fieldW;
    deviceFields[guiState.numDeviceFields].h = fieldH;
    deviceFields[guiState.numDeviceFields].deviceIndex = i;
    deviceFields[guiState.numDeviceFields].fieldType = 2;
    guiState.numDeviceFields++;
  }

  guiState.numEditFields = 0;

  // Script parameters
  tft.setCursor(divX + 10, 70);
  tft.print("T_START:");

  int fieldX = divX + 10;
  int fieldY = 85;
  int fieldW = 60;
  int fieldH = 25;
  tft.drawRect(fieldX, fieldY, fieldW, fieldH, RGB565_Cadmium_yellow);
  snprintf(editFields[guiState.numEditFields].value, sizeof(editFields[guiState.numEditFields].value),
           "%d", currentScript.tStart);
  editFields[guiState.numEditFields].x = fieldX;
  editFields[guiState.numEditFields].y = fieldY;
  editFields[guiState.numEditFields].w = fieldW;
  editFields[guiState.numEditFields].h = fieldH;
  tft.setCursor(fieldX + 5, fieldY + 18);
  tft.print(editFields[guiState.numEditFields].value);
  guiState.numEditFields++;

  tft.setCursor(divX + 10, 130);
  tft.print("T_END:");
  fieldY = 145;
  tft.drawRect(fieldX, fieldY, fieldW, fieldH, RGB565_Cadmium_yellow);
  snprintf(editFields[guiState.numEditFields].value, sizeof(editFields[guiState.numEditFields].value),
           "%d", currentScript.tEnd);
  editFields[guiState.numEditFields].x = fieldX;
  editFields[guiState.numEditFields].y = fieldY;
  editFields[guiState.numEditFields].w = fieldW;
  editFields[guiState.numEditFields].h = fieldH;
  tft.setCursor(fieldX + 5, fieldY + 18);
  tft.print(editFields[guiState.numEditFields].value);
  guiState.numEditFields++;

  tft.setCursor(divX + 10, 190);
  tft.print("Record:");
  fieldY = 205;
  fieldW = 30;
  tft.drawRect(fieldX, fieldY, fieldW, fieldH, RGB565_Cadmium_yellow);
  if (currentScript.useRecord) {
    tft.fillRect(fieldX + 5, fieldY + 5, fieldW - 10, fieldH - 10, RGB565_Cadmium_yellow);
  }
  editFields[guiState.numEditFields].x = fieldX;
  editFields[guiState.numEditFields].y = fieldY;
  editFields[guiState.numEditFields].w = fieldW;
  editFields[guiState.numEditFields].h = fieldH;
  snprintf(editFields[guiState.numEditFields].value, sizeof(editFields[guiState.numEditFields].value),
           "%s", currentScript.useRecord ? "Yes" : "No");
  tft.setCursor(fieldX + 40, fieldY + 18);
  tft.print(editFields[guiState.numEditFields].value);
  guiState.numEditFields++;

  drawButton(btnEditLoad, RGB565_Cadmium_yellow, RGB565_Black, "Load");
  drawButton(btnEditSave, RGB565_Cadmium_yellow, RGB565_Black, "Save");
  drawButton(btnEditNew, RGB565_Cadmium_yellow, RGB565_Black, "New");
}

void drawScriptLoadPage() {
  tft.fillScreen(RGB565_Black);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(RGB565_White);
  tft.setCursor(100, 30);
  tft.print("Select Script");

  tft.fillRect(5, 5, 80, 35, RGB565_Cadmium_yellow);
  tft.drawRect(5, 5, 80, 35, RGB565_Black);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(RGB565_Black);
  tft.setCursor(25, 28);
  tft.print("Back");

  const char* sortLabels[] = {"Name", "Recent", "Created"};
  drawButton(btnSortDropdown, RGB565_Cadmium_yellow, RGB565_Black, sortLabels[currentSortMode], false, true);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(RGB565_White);

  int yOffset = 60;
  int lineHeight = 22;
  int visibleScripts = min(numScripts - guiState.scriptListOffset, 10);

  for (int i = 0; i < visibleScripts; i++) {
    int yPos = yOffset + i * lineHeight;
    int scriptIdx = guiState.scriptListOffset + i;

    // Alternating row backgrounds
    uint16_t rowColor = (i % 2 == 0) ? COLOR_LIST_ROW1 : COLOR_LIST_ROW2;
    tft.fillRect(15, yPos - 2, 400, lineHeight, rowColor);

    // Highlight selected script
    if (scriptIdx == guiState.highlightedScript) {
      tft.fillRect(15, yPos - 2, 400, lineHeight, RGB565_Resolution_blue);
    }

    tft.setTextColor(RGB565_White);
    tft.setCursor(20, yPos + 15);
    tft.print(scriptIdx + 1);
    tft.print(".");

    tft.setCursor(50, yPos + 15);
    tft.print(scriptList[scriptIdx].name);

    tft.setCursor(250, yPos + 15);
    tft.setTextColor(RGB565_Gray_web);
    tft.print(formatShortDateTime(scriptList[scriptIdx].dateCreated));
    tft.setTextColor(RGB565_White);
  }

  // Scroll indicators
  if (numScripts > 10) {
    if (guiState.scriptListOffset > 0) {
      tft.fillTriangle(450, 70, 440, 80, 460, 80, RGB565_Cadmium_yellow);
    }

    if (guiState.scriptListOffset < (numScripts - 10)) {
      tft.fillTriangle(450, 240, 440, 230, 460, 230, RGB565_Cadmium_yellow);
    }
  }

  tft.setTextColor(RGB565_Gray_web);
  tft.setCursor(80, 300);
  tft.print("Press 1-9 to select, A to load script");

  if (guiState.selectedScript >= 0) {
    drawButton(btnScriptSelect, RGB565_Green, RGB565_Black, "Select", false, true);
    drawButton(btnScriptDelete, RGB565_Candy_apple_red, RGB565_Black, "Delete", false, true);
  }
}

void drawDeleteConfirmDialog() {
  tft.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, RGB565_Black);

  tft.fillRect(100, 100, 280, 120, RGB565_Gray_web);
  tft.drawRect(100, 100, 280, 120, RGB565_White);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(RGB565_White);
  tft.setCursor(110, 130);
  tft.print("Delete script:");
  tft.setCursor(110, 150);
  tft.print(guiState.deleteScriptName);
  tft.setCursor(110, 170);
  tft.print("Are you sure?");

  drawButton(btnDeleteYes, RGB565_Rojo_spanish_red, RGB565_White, "Yes", false, true);
  drawButton(btnDeleteNo, RGB565_Cadmium_yellow, RGB565_Black, "No", false, true);
}

void drawEditSavePage() {
  tft.fillScreen(RGB565_Black);
  drawButton(btnEditSaveBack, RGB565_Cadmium_yellow, RGB565_Black, "Back");

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(RGB565_White);
  tft.setCursor(100, 50);
  if (guiState.currentMode == MODE_EDIT_NAME) {
    tft.print("Edit Script Name");
  } else {
    tft.print("Save Script");
  }

  tft.drawRect(50, 80, 380, 40, RGB565_Cadmium_yellow);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(60, 105);
  tft.print(guiState.keypadBuffer);

  tft.setCursor(50, 150);
  tft.print("T9 Text Input:");
  tft.setCursor(50, 170);
  tft.print("1=abc 2=def 3=ghi 4=jkl 5=mno");
  tft.setCursor(50, 190);
  tft.print("6=pqrs 7=tuv 8=wxyz 0=-,_,space");
  tft.setCursor(50, 220);
  tft.print("#=Alpha/Num A=Save B=Back C=Shift D=Caps");
  tft.setCursor(50, 240);
  tft.print("*=Backspace");

  tft.setCursor(50, 270);
  tft.print("Alpha: ");
  tft.setTextColor(guiState.alphaMode ? RGB565_Forest_green : RGB565_Rojo_spanish_red);
  tft.print(guiState.alphaMode ? "ON" : "OFF");

  tft.setTextColor(RGB565_White);
  tft.print("  Shift: ");
  tft.setTextColor(guiState.shiftMode ? RGB565_Forest_green : RGB565_Rojo_spanish_red);
  tft.print(guiState.shiftMode ? "ON" : "OFF");

  tft.setTextColor(RGB565_White);
  tft.print("  Caps: ");
  tft.setTextColor(guiState.capsMode ? RGB565_Forest_green : RGB565_Rojo_spanish_red);
  tft.print(guiState.capsMode ? "ON" : "OFF");
}

void drawDateTimePanel() {
  tft.fillScreen(RGB565_Black);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(RGB565_White);
  tft.setCursor(120, 30);
  tft.print("Set Date & Time");

  tft.fillRect(5, 5, 80, 35, RGB565_Cadmium_yellow);
  tft.drawRect(5, 5, 80, 35, RGB565_Black);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(RGB565_Black);
  tft.setCursor(25, 28);
  tft.print("Back");

  tft.fillRect(SCREEN_WIDTH - 85, 5, 80, 35, RGB565_Forest_green);
  tft.drawRect(SCREEN_WIDTH - 85, 5, 80, 35, RGB565_Black);
  tft.setCursor(SCREEN_WIDTH - 65, 28);
  tft.print("Save");

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(RGB565_White);

  // Date/time fields with +/- buttons
  struct DateTimeControl {
    int yPos;
    const char* label;
    int value;
  };

  DateTimeControl controls[] = {
    {70, "Year:", 2000 + tmSet.Year},
    {110, "Month:", tmSet.Month},
    {150, "Day:", tmSet.Day},
    {190, "Hour:", tmSet.Hour},
    {230, "Minute:", tmSet.Minute},
    {270, "Second:", tmSet.Second}
  };

  for (int i = 0; i < 6; i++) {
    tft.setCursor(50, controls[i].yPos + 10);
    tft.print(controls[i].label);

    // Decrement button
    tft.fillRect(150, controls[i].yPos, 30, 30, RGB565_Gray_web);
    tft.drawRect(150, controls[i].yPos, 30, 30, RGB565_Black);
    tft.setTextColor(RGB565_Black);
    tft.setCursor(158, controls[i].yPos + 20);
    tft.print("-");

    // Value display
    tft.fillRect(180, controls[i].yPos, 60, 30, RGB565_Cadmium_yellow);
    tft.drawRect(180, controls[i].yPos, 60, 30, RGB565_Black);
    tft.setCursor(190, controls[i].yPos + 20);
    if (i == 0) {
      tft.print(controls[i].value);
    } else {
      tft.print(controls[i].value);
    }

    // Increment button
    tft.fillRect(240, controls[i].yPos, 30, 30, RGB565_Gray_web);
    tft.drawRect(240, controls[i].yPos, 30, 30, RGB565_Black);
    tft.setCursor(248, controls[i].yPos + 20);
    tft.print("+");

    tft.setTextColor(RGB565_White);
  }
}

void drawDeviceRow(int row) {
  int yPos = 85 + row * 25;
  bool isOn = (switchOutputs[row].state == HIGH);

  // Determine row background color
  uint16_t rowBgColor = isOn ? UI_BG_DATA_ON: UI_BG_DATA_OFF;

  // Fill the row background (no initial black clear needed if we overdraw properly)
  tft.fillRect(0, yPos - 17, MAIN_DATA_WIDTH, 25, rowBgColor);

  uint16_t textColor = UI_TEXT_ROW;

  tft.setFont(&FreeSans9pt7b);

  // Set text color with background to match row (avoids black flash)
  tft.setTextColor(textColor, rowBgColor);

  // Draw device name
  tft.setCursor(10, yPos);
  tft.print(switchOutputs[row].name);

  int inaIdx = getInaIndexForSwitch(row);
  if (inaIdx >= 0) {
    // Voltage
    tft.setCursor(100, yPos);
    tft.printf("%.2fV", deviceVoltage[inaIdx]);

    // Current
    tft.setCursor(175, yPos);
    tft.printf("%.4fA", deviceCurrent[inaIdx] / 1000.0);

    // Power
    tft.setCursor(270, yPos);
    tft.printf("%.3fW", devicePower[inaIdx]);
  }

  // Reset text color to default (no background) for other drawings
  tft.setTextColor(textColor);
}

void drawTotalRow() {
  int totalRowY = 85 + numSwitches * 25 + 15;

  // Fill background only in the data area
  tft.fillRect(0, totalRowY - 17, MAIN_DATA_WIDTH, 25, UI_BG_DATA_OFF);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(UI_TEXT_TOTAL);

  tft.setCursor(10, totalRowY);
  tft.print("Bus");

  // Use ina_all device (index 6) for total readings
  tft.setCursor(100, totalRowY);
  tft.printf("%.2fV", deviceVoltage[6]);

  tft.setCursor(175, totalRowY);
  tft.printf("%.4fA", deviceCurrent[6] / 1000.0);

  tft.setCursor(270, totalRowY);
  tft.printf("%.3fW", devicePower[6]);
}

void updateLiveValueRow(int row) {
  if (guiState.currentMode != MODE_MAIN && guiState.currentMode != MODE_SCRIPT) return;

  if (guiState.currentMode == MODE_MAIN) {
    drawDeviceRow(row);
  }
  else if (guiState.currentMode == MODE_SCRIPT) {
    int divX = SCREEN_WIDTH * 2 / 3;
    int yPos = 85 + row * 35;

    if (yPos > SCREEN_HEIGHT - 50) return;

    tft.fillRect(divX + 10, yPos - 15, 150, 30, RGB565_Black);

    bool isOn = (switchOutputs[row].state == HIGH);
    int inaIdx = getInaIndexForSwitch(row);

    tft.setFont(&FreeSans9pt7b);
    tft.setCursor(divX + 10, yPos);
    tft.setTextColor(isOn ? RGB565_Forest_green : RGB565_Rojo_spanish_red);
    tft.print(switchOutputs[row].name);
    tft.print(": ");
    tft.print(isOn ? "ON" : "OFF");

    if (inaIdx >= 0) {
      tft.setCursor(divX + 10, yPos + 15);
      tft.setTextColor(RGB565_Cyan);
      tft.printf("%.1fV %.2fA", deviceVoltage[inaIdx], deviceCurrent[inaIdx] / 1000.0);
    }
  }
}

void refreshHeaderClock() {
  if (guiState.currentMode != MODE_MAIN && guiState.currentMode != MODE_SCRIPT) return;

  char buff[64];
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(RGB565_White, RGB565_Black);

  if (guiState.currentMode == MODE_MAIN) {
    if (!isScriptRunning) {
      String nowStr = getCurrentTimeString();
      nowStr.toCharArray(buff, sizeof(buff));
    } else {
      // Calculate more precise timing for display
      unsigned long totalPausedTime = scriptPausedTime;
      if (isScriptPaused) {
        totalPausedTime += (millis() - pauseStartMillis);
      }
      unsigned long msSinceStart = millis() - scriptStartMillis - totalPausedTime;
      long preciseTime = currentScript.tStart + (long)((msSinceStart + 500) / 1000); // Round to nearest second
      
      if (preciseTime < 0) {
        snprintf(buff, sizeof(buff), "T-%ld", labs(preciseTime));
      } else {
        snprintf(buff, sizeof(buff), "T+%ld", preciseTime);
      }
    }
  } else {
    if (!isScriptRunning) {
      strncpy(buff, currentScript.scriptName, sizeof(buff)-1);
      buff[sizeof(buff)-1] = 0;
    } else {
      tft.setFont(&FreeSans9pt7b);
      // Calculate more precise timing for script page display
      unsigned long totalPausedTime = scriptPausedTime;
      if (isScriptPaused) {
        totalPausedTime += (millis() - pauseStartMillis);
      }
      unsigned long msSinceStart = millis() - scriptStartMillis - totalPausedTime;
      long preciseTime = currentScript.tStart + (long)((msSinceStart + 500) / 1000); // Round to nearest second
      
      if (preciseTime < 0) {
        snprintf(buff, sizeof(buff), "%.15s - T-%ld", currentScript.scriptName, labs(preciseTime));
      } else {
        snprintf(buff, sizeof(buff), "%.15s - T+%ld", currentScript.scriptName, preciseTime);
      }
      if (isScriptPaused) {
        strncat(buff, " (PAUSED)", sizeof(buff) - strlen(buff) - 1);
      }
    }
  }

  int16_t x1,y1;
  uint16_t w,h;
  tft.getTextBounds(buff, 0, 0, &x1, &y1, &w, &h);
  int tx = (SCREEN_WIDTH - w) / 2;  // Center on full screen width
  int clearX = max(0, tx - 10);
  int clearW = min(SCREEN_WIDTH, w + 20);
  tft.fillRect(clearX, 10, clearW, 25, RGB565_Black);
  tft.setCursor(tx, guiState.currentMode == MODE_SCRIPT && isScriptRunning ? 25 : 30);
  tft.print(buff);
}

void updateLockButton() {
  drawButton(btnLock,
             systemState.lock ? RGB565_Palatinate: RGB565_Cadmium_yellow,
             systemState.lock ? RGB565_White : RGB565_Black,
             "LOCK",
             btnLock.pressed,
             btnLock.enabled);
}

void updateDisplayElements() {
  if (guiState.currentMode == MODE_MAIN || guiState.currentMode == MODE_SCRIPT) {
    for (int i = 0; i < numSwitches; i++) {
      updateLiveValueRow(i);
    }
    if (guiState.currentMode == MODE_MAIN) {
      drawTotalRow();
    }
  }
  else if (guiState.currentMode == MODE_GRAPH) {
    updateGraphAreaSmooth();
    drawGraphInfo();
  }
}

// Functions to access field arrays
DeviceTimingField* getDeviceFields() {
  return deviceFields;
}

EditField* getEditFields() {
  return editFields;
}

NetworkEditField* getNetworkFields() {
  return networkFields;
}

int* getNumNetworkFields() {
  return &guiState.numNetworkFields;
}

// ============================================================================
// SNAKE GAME IMPLEMENTATION
// ============================================================================

#define SNAKE_GRID_SIZE 12
#define SNAKE_GRID_WIDTH (SCREEN_WIDTH / SNAKE_GRID_SIZE)
#define SNAKE_GRID_HEIGHT (SCREEN_HEIGHT / SNAKE_GRID_SIZE)
#define SNAKE_INITIAL_SPEED 300
#define SNAKE_DARK_GRAY 0x2104  // Very dark gray for checkerboard

// Grid positioning and border calculations
#define SNAKE_GRID_CELLS_X 39  // Number of grid cells horizontally (excluding borders)
#define SNAKE_GRID_CELLS_Y 23  // Number of grid cells vertically (excluding borders)
#define SNAKE_GRID_START_X ((SCREEN_WIDTH - (SNAKE_GRID_CELLS_X * SNAKE_GRID_SIZE)) / 2)  // Center the grid horizontally
#define SNAKE_GRID_START_Y 35  // Start below UI elements
#define SNAKE_GRID_END_X (SNAKE_GRID_START_X + (SNAKE_GRID_CELLS_X * SNAKE_GRID_SIZE))
#define SNAKE_GRID_END_Y (SNAKE_GRID_START_Y + (SNAKE_GRID_CELLS_Y * SNAKE_GRID_SIZE))
#define SNAKE_BORDER_WIDTH 2   // Border thickness

void initSnakeGame() {
  // Load max score from EEPROM
  EEPROM.get(EEPROM_SNAKE_MAX_SCORE_ADDR, snakeGame.maxScore);
  if (snakeGame.maxScore < 0 || snakeGame.maxScore > 9999) {
    snakeGame.maxScore = 0;
  }
  
  // Initialize snake in center of playable area
  snakeGame.length = 3;
  snakeGame.segments[0] = {SNAKE_GRID_CELLS_X / 2, SNAKE_GRID_CELLS_Y / 2};
  snakeGame.segments[1] = {SNAKE_GRID_CELLS_X / 2, SNAKE_GRID_CELLS_Y / 2 + 1};
  snakeGame.segments[2] = {SNAKE_GRID_CELLS_X / 2, SNAKE_GRID_CELLS_Y / 2 + 2};
  
  snakeGame.direction = SNAKE_UP;
  snakeGame.nextDirection = SNAKE_UP;
  
  // Initialize input buffer for enhanced controls
  snakeGame.inputBufferHead = 0;
  snakeGame.inputBufferTail = 0;
  snakeGame.inputBufferSize = 0;
  
  snakeGame.score = 0;
  snakeGame.gameRunning = false;
  snakeGame.gamePaused = false;
  snakeGame.gameOver = false;
  snakeGame.pausedByBackButton = false;
  snakeGame.newHighScore = false;  // Reset for each new game
  snakeGame.lastMoveTime = 0;
  snakeGame.moveInterval = SNAKE_INITIAL_SPEED;
  
  // Place initial food
  placeSnakeFood();
}

// Helper function to clear input buffer
void clearSnakeInputBuffer() {
  snakeGame.inputBufferHead = 0;
  snakeGame.inputBufferTail = 0;
  snakeGame.inputBufferSize = 0;
}

void placeSnakeFood() {
  bool validPosition = false;
  while (!validPosition) {
    snakeGame.foodX = random(0, SNAKE_GRID_CELLS_X);
    snakeGame.foodY = random(0, SNAKE_GRID_CELLS_Y);
    
    // Check if food position conflicts with snake
    validPosition = true;
    for (int i = 0; i < snakeGame.length; i++) {
      if (snakeGame.segments[i].x == snakeGame.foodX && 
          snakeGame.segments[i].y == snakeGame.foodY) {
        validPosition = false;
        break;
      }
    }
  }
}

void updateSnakeGame() {
  if (!snakeGame.gameRunning || snakeGame.gamePaused || snakeGame.gameOver) {
    return;
  }
  
  unsigned long currentTime = millis();
  if (currentTime - snakeGame.lastMoveTime >= snakeGame.moveInterval) {
    snakeGame.lastMoveTime = currentTime;
    
    // Read next direction from input buffer if available
    if (snakeGame.inputBufferSize > 0) {
      snakeGame.nextDirection = snakeGame.inputBuffer[snakeGame.inputBufferTail];
      snakeGame.inputBufferTail = (snakeGame.inputBufferTail + 1) % 4;
      snakeGame.inputBufferSize--;
    }
    
    // Update direction
    snakeGame.direction = snakeGame.nextDirection;
    
    // Calculate new head position
    SnakeSegment newHead = snakeGame.segments[0];
    switch (snakeGame.direction) {
      case SNAKE_UP:
        newHead.y--;
        break;
      case SNAKE_DOWN:
        newHead.y++;
        break;
      case SNAKE_LEFT:
        newHead.x--;
        break;
      case SNAKE_RIGHT:
        newHead.x++;
        break;
    }
    
    // Check wall collision (now using grid cell boundaries)
    if (newHead.x < 0 || newHead.x >= SNAKE_GRID_CELLS_X ||
        newHead.y < 0 || newHead.y >= SNAKE_GRID_CELLS_Y) {
      snakeGame.gameOver = true;
      drawSnakeGameUI(); // Update UI to show game over
      // Trigger LED effects
      if (snakeGame.newHighScore) {
        flashHighScoreLEDs();
      } else {
        flashGameOverLEDs();
      }
      return;
    }
    
    // Check self collision
    for (int i = 0; i < snakeGame.length; i++) {
      if (snakeGame.segments[i].x == newHead.x && snakeGame.segments[i].y == newHead.y) {
        snakeGame.gameOver = true;
        drawSnakeGameUI(); // Update UI to show game over
        // Trigger LED effects
        if (snakeGame.newHighScore) {
          flashHighScoreLEDs();
        } else {
          flashGameOverLEDs();
        }
        return;
      }
    }
    
    // Check food collision
    bool ateFood = (newHead.x == snakeGame.foodX && newHead.y == snakeGame.foodY);
    
    // Store old tail position before moving
    SnakeSegment oldTail = snakeGame.segments[snakeGame.length - 1];
    
    // Move snake
    if (!ateFood) {
      // Clear old tail
      clearSnakeSegment(oldTail.x, oldTail.y);
      
      // Move segments
      for (int i = snakeGame.length - 1; i > 0; i--) {
        snakeGame.segments[i] = snakeGame.segments[i - 1];
      }
    } else {
      // Growing - move segments but don't remove tail
      for (int i = snakeGame.length; i > 0; i--) {
        snakeGame.segments[i] = snakeGame.segments[i - 1];
      }
      snakeGame.length++;
      snakeGame.score += 10;
      
      // Increase speed more aggressively
      if (snakeGame.moveInterval > 80) {
        snakeGame.moveInterval -= 8;  // Faster speed increase
      } else if (snakeGame.moveInterval > 60) {
        snakeGame.moveInterval -= 4;
      } else if (snakeGame.moveInterval > 50) {
        snakeGame.moveInterval -= 2;
      }
      
      // Update max score if needed
      if (snakeGame.score > snakeGame.maxScore) {
        snakeGame.maxScore = snakeGame.score;
        snakeGame.newHighScore = true;  // Flag new high score
        EEPROM.put(EEPROM_SNAKE_MAX_SCORE_ADDR, snakeGame.maxScore);
      }
      
      // Clear old food position
      clearSnakeSegment(snakeGame.foodX, snakeGame.foodY);
      placeSnakeFood();
      // Draw new food
      drawSnakeSegment(snakeGame.foodX, snakeGame.foodY, RGB565_Red);
      
      // Update score display
      updateScoreDisplay();
    }
    
    // Add new head
    snakeGame.segments[0] = newHead;
    
    // Draw new head
    drawSnakeSegment(newHead.x, newHead.y, RGB565_Forest_green);
    
    // Update head color for previous head (now body)
    if (snakeGame.length > 1) {
      drawSnakeSegment(snakeGame.segments[1].x, snakeGame.segments[1].y, RGB565_Green);
    }
  }
}

void drawSnakeGame() {
  tft.fillScreen(RGB565_Black);
  
  // Draw UI elements
  drawSnakeGameUI();
  
  // Draw game field
  // drawSnakeGameField();
}

void drawSnakeGameUI() {
  // Draw border
  tft.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, RGB565_White);
  
  // Back button (top left)
  tft.fillRect(5, 5, 60, 25, UI_BTN_PRIMARY);
  tft.drawRect(5, 5, 60, 25, UI_BTN_BORDER);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(UI_TEXT_PRIMARY);
  tft.setCursor(20, 22);
  tft.print("Back");
  
  // Update pause button with new coordinates
  updatePauseButton();
  
  // Update score display
  updateScoreDisplay();
  
  // Draw the playing field with checkerboard pattern
  drawSnakeGameField();
  
  // Update game status text (smart text-only clearing)
  updateGameStatusText();
  
  // Only clear and redraw controls hint area when not actively playing
  if (!snakeGame.gameRunning || snakeGame.gameOver) {
    tft.fillRect(0, 290, SCREEN_WIDTH, 30, RGB565_Black);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(RGB565_Gray_web);
    tft.setCursor(20, 310);
    tft.print("Controls: 2=Up, 8=Down, 4=Left, 6=Right");
    
    // Only redraw outer border when controls text is shown (not during active gameplay)
    tft.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, RGB565_White);
  }
}

void drawSnakeGameField() {
  // Clear game area (avoid UI elements)
  tft.fillRect(SNAKE_GRID_START_X - SNAKE_BORDER_WIDTH, SNAKE_GRID_START_Y - SNAKE_BORDER_WIDTH, 
               SNAKE_GRID_CELLS_X * SNAKE_GRID_SIZE + 2 * SNAKE_BORDER_WIDTH, 
               SNAKE_GRID_CELLS_Y * SNAKE_GRID_SIZE + 2 * SNAKE_BORDER_WIDTH, RGB565_Black);
  
  // Draw checkerboard pattern - aligned with border
  for (int gridY = 0; gridY < SNAKE_GRID_CELLS_Y; gridY++) {
    for (int gridX = 0; gridX < SNAKE_GRID_CELLS_X; gridX++) {
      uint16_t color = ((gridX + gridY) % 2 == 0) ? RGB565_Black : SNAKE_DARK_GRAY;
      int x = SNAKE_GRID_START_X + gridX * SNAKE_GRID_SIZE;
      int y = SNAKE_GRID_START_Y + gridY * SNAKE_GRID_SIZE;
      tft.fillRect(x, y, SNAKE_GRID_SIZE, SNAKE_GRID_SIZE, color);
    }
  }
  
  // Draw game border - now sized based on grid
  tft.drawRect(SNAKE_GRID_START_X - SNAKE_BORDER_WIDTH, SNAKE_GRID_START_Y - SNAKE_BORDER_WIDTH,
               SNAKE_GRID_CELLS_X * SNAKE_GRID_SIZE + 2 * SNAKE_BORDER_WIDTH,
               SNAKE_GRID_CELLS_Y * SNAKE_GRID_SIZE + 2 * SNAKE_BORDER_WIDTH, RGB565_Gray_web);
  
  // Draw snake if game is running
  if (snakeGame.gameRunning && !snakeGame.gamePaused) {
    for (int i = 0; i < snakeGame.length; i++) {
      uint16_t color = (i == 0) ? RGB565_Forest_green : RGB565_Green;
      drawSnakeSegment(snakeGame.segments[i].x, snakeGame.segments[i].y, color);
    }
    
    // Draw food
    drawSnakeSegment(snakeGame.foodX, snakeGame.foodY, RGB565_Red);
  }
}

void flashGameOverLEDs() {
  // Flash all LEDs 4 times for normal game over
  for (int i = 0; i < 4; i++) {
    digitalWrite(PWR_LED_PIN, HIGH);
    digitalWrite(LOCK_LED_PIN, HIGH);
    digitalWrite(STOP_LED_PIN, HIGH);
    delay(200);
    digitalWrite(PWR_LED_PIN, LOW);
    digitalWrite(LOCK_LED_PIN, LOW);
    digitalWrite(STOP_LED_PIN, LOW);
    delay(200);
  }
}

void flashHighScoreLEDs() {
  // Special pattern for high score: power->lock->fault->lock->power, repeat 3 times
  for (int cycle = 0; cycle < 3; cycle++) {
    // Forward sequence
    digitalWrite(PWR_LED_PIN, HIGH);
    delay(150);
    digitalWrite(PWR_LED_PIN, LOW);
    digitalWrite(LOCK_LED_PIN, HIGH);
    delay(150);
    digitalWrite(LOCK_LED_PIN, LOW);
    digitalWrite(STOP_LED_PIN, HIGH);
    delay(150);
    digitalWrite(STOP_LED_PIN, LOW);
    
    // Reverse sequence
    digitalWrite(STOP_LED_PIN, HIGH);
    delay(150);
    digitalWrite(STOP_LED_PIN, LOW);
    digitalWrite(LOCK_LED_PIN, HIGH);
    delay(150);
    digitalWrite(LOCK_LED_PIN, LOW);
    digitalWrite(PWR_LED_PIN, HIGH);
    delay(150);
    digitalWrite(PWR_LED_PIN, LOW);
    
    delay(100); // Brief pause between cycles
  }
}

void clearSnakeSegment(int gridX, int gridY) {
  // Bounds check to prevent clearing outside valid game area
  if (gridX < 0 || gridX >= SNAKE_GRID_CELLS_X || 
      gridY < 0 || gridY >= SNAKE_GRID_CELLS_Y) {
    return;  // Don't clear if outside valid bounds
  }
  
  int x = SNAKE_GRID_START_X + gridX * SNAKE_GRID_SIZE;
  int y = SNAKE_GRID_START_Y + gridY * SNAKE_GRID_SIZE;
  // Restore checkerboard pattern
  uint16_t color = ((gridX + gridY) % 2 == 0) ? RGB565_Black : SNAKE_DARK_GRAY;
  tft.fillRect(x, y, SNAKE_GRID_SIZE - 1, SNAKE_GRID_SIZE - 1, color);
}

void drawSnakeSegment(int gridX, int gridY, uint16_t color) {
  // Bounds check to prevent drawing outside valid game area
  if (gridX < 0 || gridX >= SNAKE_GRID_CELLS_X || 
      gridY < 0 || gridY >= SNAKE_GRID_CELLS_Y) {
    return;  // Don't draw if outside valid bounds
  }
  
  int x = SNAKE_GRID_START_X + gridX * SNAKE_GRID_SIZE;
  int y = SNAKE_GRID_START_Y + gridY * SNAKE_GRID_SIZE;
  tft.fillRect(x, y, SNAKE_GRID_SIZE - 1, SNAKE_GRID_SIZE - 1, color);
}

void updateScoreDisplay() {
  // Clear score and max score area  
  tft.fillRect(70, 5, 300, 25, RGB565_Black);
  
  // Best score next to back button
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(RGB565_Gray_web);
  char maxScoreText[32];
  snprintf(maxScoreText, sizeof(maxScoreText), "Best: %d", snakeGame.maxScore);
  tft.setCursor(75, 25);  // Right next to back button
  tft.print(maxScoreText);
  
  // Regular score centered on screen
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(RGB565_White);
  char scoreText[32];
  snprintf(scoreText, sizeof(scoreText), "Score: %d", snakeGame.score);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(scoreText, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_WIDTH - w) / 2+10, 25);  // Perfect center
  tft.print(scoreText);
}

void updatePauseButton() {
  // Clear and redraw pause button area (20px longer)
  tft.fillRect(380, 5, 90, 25, RGB565_Black);
  if (snakeGame.gameRunning && !snakeGame.gameOver) {
    tft.fillRect(380, 5, 90, 25, UI_BTN_PRIMARY);
    tft.drawRect(380, 5, 90, 25, UI_BTN_BORDER);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(UI_TEXT_PRIMARY);
    tft.setCursor(392, 22);
    tft.print(snakeGame.gamePaused ? "Resume" : "Pause");
  }
}

void clearGameStatusText() {
  // Redraw the entire playing field to clean up any text overlays
  redrawPlayingField();
}

void redrawPlayingField() {
  // Only redraw the central playing area, avoid UI elements
  // Clear the center area first
  tft.fillRect(SNAKE_GRID_START_X - SNAKE_BORDER_WIDTH, SNAKE_GRID_START_Y - SNAKE_BORDER_WIDTH,
               SNAKE_GRID_CELLS_X * SNAKE_GRID_SIZE + 2 * SNAKE_BORDER_WIDTH,
               SNAKE_GRID_CELLS_Y * SNAKE_GRID_SIZE + 2 * SNAKE_BORDER_WIDTH, RGB565_Black);
  
  // Draw checkerboard pattern - aligned with border
  for (int gridY = 0; gridY < SNAKE_GRID_CELLS_Y; gridY++) {
    for (int gridX = 0; gridX < SNAKE_GRID_CELLS_X; gridX++) {
      uint16_t color = ((gridX + gridY) % 2 == 0) ? RGB565_Black : SNAKE_DARK_GRAY;
      int x = SNAKE_GRID_START_X + gridX * SNAKE_GRID_SIZE;
      int y = SNAKE_GRID_START_Y + gridY * SNAKE_GRID_SIZE;
      tft.fillRect(x, y, SNAKE_GRID_SIZE, SNAKE_GRID_SIZE, color);
    }
  }
  
  // Draw game border
  tft.drawRect(SNAKE_GRID_START_X - SNAKE_BORDER_WIDTH, SNAKE_GRID_START_Y - SNAKE_BORDER_WIDTH,
               SNAKE_GRID_CELLS_X * SNAKE_GRID_SIZE + 2 * SNAKE_BORDER_WIDTH,
               SNAKE_GRID_CELLS_Y * SNAKE_GRID_SIZE + 2 * SNAKE_BORDER_WIDTH, RGB565_Gray_web);
  
  // Redraw snake if game is running
  if (snakeGame.gameRunning && !snakeGame.gameOver) {
    for (int i = 0; i < snakeGame.length; i++) {
      uint16_t color = (i == 0) ? RGB565_Forest_green : RGB565_Green;
      drawSnakeSegment(snakeGame.segments[i].x, snakeGame.segments[i].y, color);
    }
    
    // Redraw food
    drawSnakeSegment(snakeGame.foodX, snakeGame.foodY, RGB565_Red);
  }
}

void updateGameStatusText() {
  int16_t x1, y1;
  uint16_t w, h;
  
  if (!snakeGame.gameRunning) {
    tft.setFont(&FreeSansBold12pt7b);
    tft.setTextColor(RGB565_Cadmium_yellow);
    tft.getTextBounds("Press A to Start", 0, 0, &x1, &y1, &w, &h);
    int textX = (SCREEN_WIDTH - w) / 2;
    int textY = 160;
    // Clear small area around text with black background
    tft.fillRect(textX - 5, textY - h - 5, w + 10, h + 10, RGB565_Black);
    tft.setCursor(textX, textY);
    tft.print("Press A to Start");
  } else if (snakeGame.gameOver) {
    if (snakeGame.newHighScore) {
      // Show new high score message
      tft.setFont(&FreeSansBold12pt7b);
      tft.setTextColor(RGB565_Cadmium_yellow);
      tft.getTextBounds("New High Score!", 0, 0, &x1, &y1, &w, &h);
      int textX = (SCREEN_WIDTH - w) / 2;
      int textY = 150;
      // Clear area around high score text
      tft.fillRect(textX - 5, textY - h - 5, w + 10, h + 10, RGB565_Black);
      tft.setCursor(textX, textY);
      tft.print("New High Score!");
      
      tft.setFont(&FreeSans9pt7b);
      tft.setTextColor(RGB565_White);
      char scoreText[32];
      snprintf(scoreText, sizeof(scoreText), "Score: %d", snakeGame.score);
      tft.getTextBounds(scoreText, 0, 0, &x1, &y1, &w, &h);
      textX = (SCREEN_WIDTH - w) / 2;
      textY = 170;
      tft.fillRect(textX - 5, textY - h - 5, w + 10, h + 10, RGB565_Black);
      tft.setCursor(textX, textY);
      tft.print(scoreText);
    } else {
      tft.setFont(&FreeSansBold12pt7b);
      tft.setTextColor(RGB565_Rojo_spanish_red);
      tft.getTextBounds("Game Over!", 0, 0, &x1, &y1, &w, &h);
      int textX = (SCREEN_WIDTH - w) / 2;
      int textY = 160;
      // Clear area around game over text
      tft.fillRect(textX - 5, textY - h - 5, w + 10, h + 10, RGB565_Black);
      tft.setCursor(textX, textY);
      tft.print("Game Over!");
    }
    
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(RGB565_White);
    tft.getTextBounds("Press A to Restart", 0, 0, &x1, &y1, &w, &h);
    int textX = (SCREEN_WIDTH - w) / 2;
    int textY = 190;
    tft.fillRect(textX - 5, textY - h - 5, w + 10, h + 10, RGB565_Black);
    tft.setCursor(textX, textY);
    tft.print("Press A to Restart");
  } else if (snakeGame.gamePaused) {
    tft.setFont(&FreeSansBold12pt7b);
    tft.setTextColor(RGB565_Orange);
    tft.getTextBounds("Paused", 0, 0, &x1, &y1, &w, &h);
    int textX = (SCREEN_WIDTH - w) / 2;
    int textY = 160;
    // Clear small area around pause text
    tft.fillRect(textX - 5, textY - h - 5, w + 10, h + 10, RGB565_Black);
    tft.setCursor(textX, textY);
    tft.print("Paused");
  }
}
