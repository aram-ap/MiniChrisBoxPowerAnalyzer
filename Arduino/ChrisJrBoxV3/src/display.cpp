/**
 * @file display.cpp
 * @brief Display and GUI implementation
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
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>

using namespace qindesign::network;

// Display object
Adafruit_ST7796S tft = Adafruit_ST7796S(TFT_CS, TFT_DC, TFT_RST);

// External references
extern SystemState systemState;
extern GUIState guiState;
extern NetworkConfig networkConfig;
extern bool ethernetConnected;
extern NetworkInitState networkInitState;

// Field arrays
static DeviceTimingField deviceFields[MAX_DEVICE_FIELDS];
static EditField editFields[MAX_EDIT_FIELDS];
static NetworkEditField networkFields[MAX_NETWORK_FIELDS];

// Button definitions - Main screen (modified layout)
ButtonRegion btnRecord     = {  5,  5,   120, 35, "RECORD",    false, COLOR_RECORD,    false };
ButtonRegion btnSDRefresh  = {130,  5,    40, 35, "SD",        false, COLOR_CYAN,      true };
ButtonRegion btnStop       = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, COLOR_YELLOW, true };
ButtonRegion btnLock       = { SCREEN_WIDTH - 70, SCREEN_HEIGHT - 40, 65, 35, "LOCK",  false, COLOR_YELLOW, true };
ButtonRegion btnAllOn      = {  5,  SCREEN_HEIGHT - 40, 80, 35, "ALL ON", false, COLOR_YELLOW, true };
ButtonRegion btnAllOff     = { 90,  SCREEN_HEIGHT - 40, 80, 35, "ALL OFF",false, COLOR_YELLOW, true };
ButtonRegion btnScript     = {175,  SCREEN_HEIGHT - 40, 60, 35, "Script", false, COLOR_YELLOW, true };
ButtonRegion btnEdit       = {240,  SCREEN_HEIGHT - 40, 60, 35, "Edit",   false, COLOR_YELLOW, true };
ButtonRegion btnSettings   = {305,  SCREEN_HEIGHT - 40, 75, 35, "Settings",false, COLOR_YELLOW, true };

// New graph button in the right column
ButtonRegion btnGraph      = { MAIN_BUTTON_COLUMN_X, 50, MAIN_BUTTON_COLUMN_WIDTH, 35, "Graph", false, COLOR_BLUE, true };

// Graph page buttons
ButtonRegion btnGraphBack     = {  5,  5,   80, 35, "Back",     false, COLOR_YELLOW, true };
ButtonRegion btnGraphStop     = { SCREEN_WIDTH - 85, SCREEN_HEIGHT - 40, 80, 35, "STOP", false, COLOR_YELLOW, true };
ButtonRegion btnGraphClear    = {  5, SCREEN_HEIGHT - 40,  80, 35, "Clear",    false, COLOR_YELLOW, true };
ButtonRegion btnGraphPause    = { 90, SCREEN_HEIGHT - 40,  80, 35, "Pause",    false, COLOR_YELLOW, true };
ButtonRegion btnGraphSettings = {175, SCREEN_HEIGHT - 40,  80, 35, "Settings", false, COLOR_YELLOW, true };

// Graph settings buttons
ButtonRegion btnGraphSettingsBack = {  5,  5,   80, 35, "Back", false, COLOR_YELLOW, true };
ButtonRegion btnGraphDataType     = {150, 60,  100, 30, "Current", false, COLOR_YELLOW, true };
ButtonRegion btnGraphMinY = {
  /*x*/ 0, /*y*/ 0, /*w*/ 80, /*h*/ 30,
  /*label*/ "0.00",
  /*pressed*/ false,
  /*color*/ COLOR_YELLOW,
  /*enabled*/ true
};
ButtonRegion btnGraphMaxY = {
  0, 0, 80, 30,
  "0.00",
  false,
  COLOR_YELLOW,
  true
};

ButtonRegion btnGraphThickness = {
  0, 0, 60, 30,
  "1",
  false,
  COLOR_YELLOW,
  true
};

// ADDED: Missing button definitions
ButtonRegion btnGraphTimeRange = {0, 0, 80, 30, "60.00", false, COLOR_YELLOW, true};
// ButtonRegion btnGraphDisplay = {0, 0, 80, 30, "Display", false, COLOR_YELLOW, true};
// ButtonRegion btnGraphDisplayBack = {5, 5, 80, 35, "Back", false, COLOR_YELLOW, true};
// ButtonRegion btnGraphDataTypeFooter = {265, SCREEN_HEIGHT - 40, 67, 35, "Current", false, COLOR_RED, true};

extern ButtonRegion btnGraphDisplay;
extern ButtonRegion btnGraphDisplayBack;
extern ButtonRegion btnGraphDataTypeFooter;


// Settings panel buttons
ButtonRegion btnSettingsBack = { 5, 5, 80, 35, "Back", false, COLOR_YELLOW, true };
ButtonRegion btnSettingsStop = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, COLOR_YELLOW, true };
ButtonRegion btnNetwork = { 310, SCREEN_HEIGHT - 40, 78, 35, "Network", false, COLOR_YELLOW, true };
ButtonRegion btnAbout = { 390, SCREEN_HEIGHT - 40, 80, 35, "About", false, COLOR_YELLOW, true };

// Settings input buttons
ButtonRegion btnFanSpeedInput = { 320, 70, 80, 30, "", false, COLOR_YELLOW, true };
ButtonRegion btnUpdateRateInput = { 320, 110, 80, 30, "", false, COLOR_YELLOW, true };
ButtonRegion btnSetTimeDate = { 320, 150, 80, 30, "Set", false, COLOR_YELLOW, true };
ButtonRegion btnTimeFormatToggle = { 320, 190, 80, 30, "24H", false, COLOR_YELLOW, true };
ButtonRegion btnDarkModeToggle = { 320, 230, 80, 30, "ON", false, COLOR_YELLOW, true };

// Network settings buttons
ButtonRegion btnNetworkBack = { 5, 5, 80, 35, "Back", false, COLOR_YELLOW, true };
ButtonRegion btnNetworkStop = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, COLOR_YELLOW, true };
ButtonRegion btnNetworkEdit = { 320, SCREEN_HEIGHT - 40, 80, 35, "Edit", false, COLOR_YELLOW, true };
ButtonRegion btnEnableLanToggle = { 320, 70, 80, 30, "ON", false, COLOR_YELLOW, true };

// Network edit buttons
ButtonRegion btnNetworkEditBack = { 5, 5, 80, 35, "Back", false, COLOR_YELLOW, true };
ButtonRegion btnNetworkEditStop = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, COLOR_YELLOW, true };
ButtonRegion btnNetworkEditSave = { 320, SCREEN_HEIGHT - 40, 80, 35, "Save", false, COLOR_GREEN, true };
ButtonRegion btnDhcpToggle = { 320, 70, 80, 30, "ON", false, COLOR_YELLOW, true };

// About page buttons
ButtonRegion btnAboutBack = { 5, 5, 80, 35, "Back", false, COLOR_YELLOW, true };
ButtonRegion btnAboutStop = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, COLOR_YELLOW, true };

// Script panel buttons
ButtonRegion btnScriptBack   = {  5, 5, 80, 35, "Back", false, COLOR_YELLOW, true };
ButtonRegion btnScriptStop   = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, COLOR_YELLOW, true };
ButtonRegion btnScriptLoad   = {  5,  SCREEN_HEIGHT - 40, 60, 35, "Load", false, COLOR_YELLOW, true };
ButtonRegion btnScriptEdit   = { 70,  SCREEN_HEIGHT - 40, 60, 35, "Edit", false, COLOR_YELLOW, true };
ButtonRegion btnScriptStart  = {135, SCREEN_HEIGHT - 40, 70, 35, "Start", false, COLOR_GREEN,  true };
ButtonRegion btnScriptEnd    = {210, SCREEN_HEIGHT - 40, 50, 35,  "Stop",  false, COLOR_RED,    true };
ButtonRegion btnScriptRecord = {265, SCREEN_HEIGHT - 40, 70, 35, "Record", false, COLOR_BLUE,  true };

// Edit panel buttons
ButtonRegion btnEditBack     = {  5, 5, 80, 35, "Back", false, COLOR_YELLOW, true };
ButtonRegion btnEditStop     = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, COLOR_YELLOW, true };
ButtonRegion btnEditLoad     = {  5,  SCREEN_HEIGHT - 40, 80, 35, "Load", false, COLOR_YELLOW, true };
ButtonRegion btnEditSave     = {  90, SCREEN_HEIGHT - 40, 80, 35, "Save", false, COLOR_YELLOW, true };
ButtonRegion btnEditNew      = { 175, SCREEN_HEIGHT - 40, 80, 35, "New",  false, COLOR_YELLOW, true };

// Additional UI buttons
ButtonRegion btnKeypadBack = {5, 5, 80, 35, "Back", false, COLOR_YELLOW, true};
ButtonRegion btnEditSaveBack = {5, 5, 80, 35, "Back", false, COLOR_YELLOW, true};
ButtonRegion btnEditNameBack = {5, 5, 80, 35, "Back", false, COLOR_YELLOW, true};
ButtonRegion btnDateTimeBack = {5, 5, 80, 35, "Back", false, COLOR_YELLOW, true};
ButtonRegion btnEditFieldBack = {5, 5, 80, 35, "Back", false, COLOR_YELLOW, true};
ButtonRegion btnScriptSelect = {SCREEN_WIDTH - 85, SCREEN_HEIGHT - 40, 80, 35, "Select", false, COLOR_GREEN, true};
ButtonRegion btnScriptDelete = {SCREEN_WIDTH - 170, SCREEN_HEIGHT - 40, 80, 35, "Delete", false, COLOR_RED, true};
ButtonRegion btnSortDropdown = {SCREEN_WIDTH - 100, 5, 95, 35, "Name", false, COLOR_YELLOW, true};
ButtonRegion btnDeleteYes = {150, 150, 80, 35, "Yes", false, COLOR_RED, true};
ButtonRegion btnDeleteNo = {250, 150, 80, 35, "No", false, COLOR_YELLOW, true};

void initDisplay() {
  tft.init(320, 480, 0, 0, ST7796S_BGR);
  tft.setRotation(1);
  // ADDED: SPI Optimization for faster refresh
  tft.setSPISpeed(52000000);
  applyDarkMode();
}

void updateDisplay(unsigned long currentMillis) {
  if (currentMillis - systemState.lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    updateDisplayElements();
    systemState.lastDisplayUpdate = currentMillis;
  }

  if (currentMillis - systemState.lastClockRefresh >= 1000) {
    systemState.lastClockRefresh = currentMillis;
    refreshHeaderClock();
  }
}

void drawButton(ButtonRegion& btn, uint16_t bgColor, uint16_t textColor,
                const char* label, bool pressed, bool enabled) {
  btn.color = bgColor;
  btn.pressed = pressed;
  btn.enabled = enabled;
  uint16_t fill = !enabled ? COLOR_GRAY : (pressed ? COLOR_BTN_PRESS : bgColor);
  tft.fillRect(btn.x, btn.y, btn.w, btn.h, fill);
  tft.drawRect(btn.x, btn.y, btn.w, btn.h, COLOR_BLACK);
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
  tft.fillScreen(COLOR_BLACK);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  int16_t x1, y1;
  uint16_t w, h;

  String title = "Mini Chris Box V5.2";
  tft.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_WIDTH - w) / 2, 80);
  tft.print(title);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_YELLOW);

  String status = "Initializing...";
  tft.getTextBounds(status, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_WIDTH - w) / 2, 120);
  tft.print(status);

  // Show initialization progress
  tft.setTextColor(COLOR_CYAN);
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
  uint16_t statusColor = COLOR_GRAY;

  if (networkConfig.enableEthernet) {
    switch (networkInitState) {
      case NET_IDLE:
      case NET_CHECKING_LINK:
      case NET_INITIALIZING:
      case NET_DHCP_WAIT:
        statusColor = COLOR_YELLOW;
      break;
      case NET_INITIALIZED:
        statusColor = COLOR_GREEN;
      break;
      case NET_FAILED:
        statusColor = COLOR_RED;
      break;
    }
  }

  // Update status text in right column
  static String lastStatusText = "";
  if (statusText != lastStatusText) {
    tft.fillRect(250, 160, 200, 100, COLOR_BLACK);  // Right column
    tft.setTextColor(statusColor);
    tft.setCursor(250, 180);
    tft.print(statusText);
    lastStatusText = statusText;
  }

  // Show completion message in right column
  if (networkInitState == NET_INITIALIZED || networkInitState == NET_FAILED || !networkConfig.enableEthernet) {
    tft.setTextColor(COLOR_GREEN);
    tft.setCursor(250, 210);
    tft.print("Network Ready!");

    if (networkInitState == NET_INITIALIZED) {
      tft.setTextColor(COLOR_CYAN);
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
  tft.fillScreen(COLOR_BLACK);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
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
    if (scriptTimeSeconds < 0) {
      snprintf(buff, sizeof(buff), "T-%ld", labs(scriptTimeSeconds));
    } else {
      snprintf(buff, sizeof(buff), "T+%ld", scriptTimeSeconds);
    }
    tft.getTextBounds(buff, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((SCREEN_WIDTH - w) / 2, 30);  // Center on full screen width
    tft.print(buff);
  }

  // Draw control buttons
  drawButton(btnRecord,
             !systemState.sdAvailable ? COLOR_GRAY : (systemState.recording ? COLOR_RECORDING : COLOR_RECORD),
             COLOR_WHITE,
             systemState.recording ? "RECORDING" : "RECORD",
             false,
             systemState.sdAvailable);

  drawButton(btnSDRefresh,
             systemState.sdAvailable ? COLOR_GREEN : COLOR_RED,
             COLOR_WHITE,
             "SD",
             false,
             true);

  drawButton(btnStop, COLOR_YELLOW, COLOR_BLACK, "STOP");
  drawButton(btnAllOn, isScriptRunning ? COLOR_GRAY : COLOR_YELLOW,
                        COLOR_BLACK,
                        "ALL ON", false, !isScriptRunning);
  drawButton(btnAllOff, isScriptRunning ? COLOR_GRAY : COLOR_YELLOW,
                         COLOR_BLACK,
                         "ALL OFF", false, !isScriptRunning);
  drawButton(btnScript, COLOR_YELLOW, COLOR_BLACK, "Script");
  drawButton(btnEdit, COLOR_YELLOW, COLOR_BLACK, "Edit");
  updateLockButton();
  drawButton(btnSettings, COLOR_YELLOW, COLOR_BLACK, "Settings", false, btnSettings.enabled);

  // Draw new graph button in right column
  drawButton(btnGraph, COLOR_BLUE, COLOR_WHITE, "Graph");

  // Draw vertical separator line
  tft.drawLine(MAIN_BUTTON_COLUMN_X - 5, 40, MAIN_BUTTON_COLUMN_X - 5, SCREEN_HEIGHT - 45, COLOR_GRAY);

  // Draw data table header (in reduced width area)
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_WHITE);

  tft.setCursor(10, 60);
  tft.print("Output");
  tft.setCursor(100, 60);
  tft.print("V");
  tft.setCursor(175, 60);
  tft.print("I (A)");
  tft.setCursor(270, 60);
  tft.print("P (W)");

  tft.drawLine(5, 65, MAIN_DATA_WIDTH, 65, COLOR_GRAY);

  // Draw device rows
  for (int i = 0; i < numSwitches; i++) {
    drawDeviceRow(i);
  }

  // Draw total row
  int totalRowY = 85 + numSwitches * 25 + 10;
  tft.drawLine(5, totalRowY - 5, MAIN_DATA_WIDTH, totalRowY - 5, COLOR_GRAY);
  drawTotalRow();
}

void drawSettingsPanel() {
  tft.fillScreen(COLOR_BLACK);

  drawButton(btnSettingsBack, COLOR_YELLOW, COLOR_BLACK, "Back", false, true);
  drawButton(btnSettingsStop, COLOR_YELLOW, COLOR_BLACK, "STOP", false, true);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds("Settings", 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_WIDTH - w) / 2, 32);
  tft.print("Settings");

  tft.setFont(&FreeSans9pt7b);

  // Fan speed setting
  tft.fillRect(20, 70, 460, 30, COLOR_DARK_ROW1);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 90);
  tft.print("Fan Speed (0-255):");
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", systemState.fanSpeed);
  drawButton(btnFanSpeedInput, COLOR_YELLOW, COLOR_BLACK, buf, false, true);

  // Update rate setting
  tft.fillRect(20, 110, 460, 30, COLOR_DARK_ROW2);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 130);
  tft.print("Update Rate (ms):");
  char buf2[12];
  snprintf(buf2, sizeof(buf2), "%lu", systemState.updateRate);
  drawButton(btnUpdateRateInput, COLOR_YELLOW, COLOR_BLACK, buf2, false, true);

  // RTC clock setting
  tft.fillRect(20, 150, 460, 30, COLOR_DARK_ROW1);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 170);
  tft.print("RTC Clock:");
  drawButton(btnSetTimeDate, COLOR_YELLOW, COLOR_BLACK, "Set", false, true);

  // Time format setting
  tft.fillRect(20, 190, 460, 30, COLOR_DARK_ROW2);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 210);
  tft.print("Time Format:");
  drawButton(btnTimeFormatToggle, COLOR_YELLOW, COLOR_BLACK, systemState.use24HourFormat ? "24H" : "12H", false, true);

  // Dark mode setting
  tft.fillRect(20, 230, 460, 30, COLOR_DARK_ROW1);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 250);
  tft.print("Dark Mode:");
  drawButton(btnDarkModeToggle, COLOR_YELLOW, COLOR_BLACK, systemState.darkMode ? "ON" : "OFF", false, true);

  // Additional settings buttons
  drawButton(btnNetwork, COLOR_YELLOW, COLOR_BLACK, "Network", false, true);
  drawButton(btnAbout, COLOR_YELLOW, COLOR_BLACK, "About", false, true);

  // Show current date/time
  tft.setTextColor(COLOR_GRAY);
  tft.setCursor(30, 280);
  tft.print(formatDateString(now()));
  tft.print(" ");
  tft.print(getCurrentTimeString());
}

void drawNetworkPanel() {
  tft.fillScreen(COLOR_BLACK);

  drawButton(btnNetworkBack, COLOR_YELLOW, COLOR_BLACK, "Back", false, true);
  drawButton(btnNetworkStop, COLOR_YELLOW, COLOR_BLACK, "STOP", false, true);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds("Network Settings", 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_WIDTH - w) / 2, 32);
  tft.print("Network Settings");

  tft.setFont(&FreeSans9pt7b);

  // Enable LAN setting
  tft.fillRect(20, 70, 460, 30, COLOR_DARK_ROW1);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 90);
  tft.print("Enable LAN:");
  drawButton(btnEnableLanToggle, COLOR_YELLOW, COLOR_BLACK, networkConfig.enableEthernet ? "ON" : "OFF", false, true);

  // Connection status
  tft.fillRect(20, 110, 460, 30, COLOR_DARK_ROW2);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 130);
  tft.print("Connection:");
  tft.setTextColor(ethernetConnected ? COLOR_GREEN : COLOR_RED);
  tft.setCursor(130, 130);
  tft.print(ethernetConnected ? "Connected" : "Disconnected");

  // Network information (if connected)
  if (ethernetConnected) {
    tft.fillRect(20, 150, 460, 30, COLOR_DARK_ROW1);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(30, 170);
    tft.print("IP Address:");
    tft.setTextColor(COLOR_CYAN);
    tft.setCursor(125, 170);
    tft.print(ipToString(Ethernet.localIP()));

    tft.fillRect(20, 190, 460, 30, COLOR_DARK_ROW2);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(30, 210);
    tft.print("TCP Port:");
    tft.setTextColor(COLOR_CYAN);
    tft.setCursor(120, 210);
    tft.print(networkConfig.tcpPort);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(200, 210);
    tft.print("UDP Port:");
    tft.setTextColor(COLOR_CYAN);
    tft.setCursor(290, 210);
    tft.print(networkConfig.udpPort);
  }

  drawButton(btnNetworkEdit, COLOR_YELLOW, COLOR_BLACK, "Edit", false, true);
}

void drawNetworkEditPanel() {
  tft.fillScreen(COLOR_BLACK);

  drawButton(btnNetworkEditBack, COLOR_YELLOW, COLOR_BLACK, "Back", false, true);
  drawButton(btnNetworkEditStop, COLOR_YELLOW, COLOR_BLACK, "STOP", false, true);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds("Network Configuration", 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_WIDTH - w) / 2, 32);
  tft.print("Network Configuration");

  tft.setFont(&FreeSans9pt7b);

  // DHCP toggle
  tft.fillRect(20, 70, 460, 30, COLOR_DARK_ROW1);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 90);
  tft.print("Use DHCP:");
  drawButton(btnDhcpToggle, COLOR_YELLOW, COLOR_BLACK, networkConfig.useDHCP ? "ON" : "OFF", false, true);

  // Static IP configuration (if DHCP disabled)
  if (!networkConfig.useDHCP) {
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(30, 110);
    tft.print("Static IP:");
    tft.drawRect(networkFields[0].x, networkFields[0].y, networkFields[0].w, networkFields[0].h, COLOR_YELLOW);
    tft.setCursor(networkFields[0].x + 5, networkFields[0].y + 18);
    tft.print(networkFields[0].value);

    tft.setCursor(30, 140);
    tft.print("Subnet:");
    tft.drawRect(networkFields[1].x, networkFields[1].y, networkFields[1].w, networkFields[1].h, COLOR_YELLOW);
    tft.setCursor(networkFields[1].x + 5, networkFields[1].y + 18);
    tft.print(networkFields[1].value);

    tft.setCursor(30, 170);
    tft.print("Gateway:");
    tft.drawRect(networkFields[2].x, networkFields[2].y, networkFields[2].w, networkFields[2].h, COLOR_YELLOW);
    tft.setCursor(networkFields[2].x + 5, networkFields[2].y + 18);
    tft.print(networkFields[2].value);

    tft.setCursor(30, 200);
    tft.print("DNS:");
    tft.drawRect(networkFields[3].x, networkFields[3].y, networkFields[3].w, networkFields[3].h, COLOR_YELLOW);
    tft.setCursor(networkFields[3].x + 5, networkFields[3].y + 18);
    tft.print(networkFields[3].value);
  }

  // Port configuration
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 230);
  tft.print("TCP Port:");
  tft.drawRect(networkFields[4].x, networkFields[4].y, networkFields[4].w, networkFields[4].h, COLOR_YELLOW);
  tft.setCursor(networkFields[4].x + 5, networkFields[4].y + 18);
  tft.print(networkFields[4].value);

  tft.setCursor(30, 260);
  tft.print("UDP Port:");
  tft.drawRect(networkFields[5].x, networkFields[5].y, networkFields[5].w, networkFields[5].h, COLOR_YELLOW);
  tft.setCursor(networkFields[5].x + 5, networkFields[5].y + 18);
  tft.print(networkFields[5].value);

  // Timeout configuration
  tft.setCursor(30, 290);
  tft.print("Timeout (ms):");
  tft.drawRect(networkFields[6].x, networkFields[6].y, networkFields[6].w, networkFields[6].h, COLOR_YELLOW);
  tft.setCursor(networkFields[6].x + 5, networkFields[6].y + 18);
  tft.print(networkFields[6].value);

  tft.setCursor(170, 290);
  tft.print("DHCP Timeout:");
  tft.drawRect(networkFields[7].x, networkFields[7].y, networkFields[7].w, networkFields[7].h, COLOR_YELLOW);
  tft.setCursor(networkFields[7].x + 5, networkFields[7].y + 18);
  tft.print(networkFields[7].value);

  drawButton(btnNetworkEditSave, COLOR_GREEN, COLOR_BLACK, "Save", false, true);
}

void drawAboutPage() {
  tft.fillScreen(COLOR_BLACK);

  drawButton(btnAboutBack, COLOR_YELLOW, COLOR_BLACK, "Back", false, true);
  drawButton(btnAboutStop, COLOR_YELLOW, COLOR_BLACK, "STOP", false, true);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds("About", 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_WIDTH - w) / 2, 32);
  tft.print("About");

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_WHITE);
  int xOffset = 20;

  tft.setCursor(xOffset, 70);
  tft.print(SOFTWARE_VERSION);

  tft.setCursor(xOffset, 95);
  tft.print("Designed by Aram Aprahamian");

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_GRAY);

  tft.setCursor(xOffset, 125);
  tft.print("Copyright (c) 2025 Aram Aprahamian");

  tft.setCursor(xOffset, 145);
  tft.print("Permission is hereby granted, free of charge, to any ");
  tft.setCursor(xOffset, 160);
  tft.print("person obtaining a copy of this software and associated");
  tft.setCursor(xOffset, 175);
  tft.print("documentation files (the \"Software\"), to deal in the");
  tft.setCursor(xOffset, 190);
  tft.print("Software without restriction, including without limitation");
  tft.setCursor(xOffset, 205);
  tft.print("the rights to use, copy, modify, merge, publish,");
  tft.setCursor(xOffset, 220);
  tft.print("distribute, sublicense, and/or sell copies of the Software,");
  tft.setCursor(xOffset, 235);
  tft.print("and to permit persons to whom the Software is");
  tft.setCursor(xOffset, 250);
  tft.print("furnished to do so, subject to the above copyright");
  tft.setCursor(xOffset, 265);
  tft.print("notice being included in all copies.");
}

void drawKeypadPanel() {
  tft.fillScreen(COLOR_BLACK);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(40, 50);

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
  tft.setCursor(40, 90);
  tft.print(guiState.keypadBuffer);

  // Display help text
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(40, 160);

  if (guiState.keypadMode == KEYPAD_DEVICE_ON_TIME || guiState.keypadMode == KEYPAD_DEVICE_OFF_TIME ||
      guiState.keypadMode == KEYPAD_SCRIPT_TSTART || guiState.keypadMode == KEYPAD_SCRIPT_TEND ||
      guiState.keypadMode == KEYPAD_UPDATE_RATE || guiState.keypadMode == KEYPAD_FAN_SPEED ||
      guiState.keypadMode == KEYPAD_NETWORK_PORT || guiState.keypadMode == KEYPAD_NETWORK_TIMEOUT ||
      guiState.keypadMode == KEYPAD_GRAPH_MIN_Y || guiState.keypadMode == KEYPAD_GRAPH_MAX_Y ||
      guiState.keypadMode == KEYPAD_GRAPH_TIME_RANGE || guiState.keypadMode == KEYPAD_GRAPH_MAX_POINTS ||
      guiState.keypadMode == KEYPAD_GRAPH_REFRESH_RATE) {
    tft.print("*=Backspace, #=+/-, A=Enter, B=Back, C=Clear, D=Decimal");
  } else if (guiState.keypadMode == KEYPAD_NETWORK_IP) {
    tft.print("*=Backspace, D=Decimal, A=Enter, B=Back, C=Clear");
  } else {
    tft.print("A=Enter, B=Back, *=Clear");
  }

  if (guiState.keypadMode == KEYPAD_SCRIPT_SEARCH) {
    tft.setCursor(40, 190);
    tft.print("Enter script number (1-");
    tft.print(numScripts);
    tft.print(")");
  }

  drawButton(btnKeypadBack, COLOR_YELLOW, COLOR_BLACK, "Back");
}

void drawScriptPage() {
  tft.fillScreen(COLOR_BLACK);

  drawButton(btnScriptBack, COLOR_YELLOW, COLOR_BLACK, "Back");
  drawButton(btnScriptStop, COLOR_YELLOW, COLOR_BLACK, "STOP");

  tft.setTextColor(COLOR_WHITE);
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
    long sTime = scriptTimeSeconds;
    if (sTime < 0) {
      snprintf(buff, sizeof(buff), "%s - T-%ld", currentScript.scriptName, labs(sTime));
    } else {
      snprintf(buff, sizeof(buff), "%s - T+%ld", currentScript.scriptName, sTime);
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
  tft.drawLine(divX, 45, divX, SCREEN_HEIGHT - 45, COLOR_GRAY);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_WHITE);

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

  tft.drawLine(10, baseY + 6 * rowHeight + 10, divX - 10, baseY + 6 * rowHeight + 10, COLOR_GRAY);

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
  tft.setTextColor(COLOR_YELLOW);
  tft.setCursor(divX + 10, 60);
  tft.print("Device Status");

  for (int i = 0; i < numSwitches; i++) {
    int yPos = 85 + i * 35;
    if (yPos > SCREEN_HEIGHT - 50) break;

    tft.setCursor(divX + 10, yPos);
    bool st = switchOutputs[i].state == HIGH;
    tft.setTextColor(st ? COLOR_GREEN : COLOR_RED);
    tft.print(switchOutputs[i].name);
    tft.print(": ");
    tft.print(st ? "ON" : "OFF");

    int inaIdx = getInaIndexForSwitch(i);
    if (inaIdx >= 0) {
      tft.setCursor(divX + 10, yPos + 15);
      tft.setTextColor(COLOR_CYAN);
      tft.printf("%.1fV %.2fA", deviceVoltage[inaIdx], deviceCurrent[inaIdx] / 1000.0);
    }
  }

  // Control buttons
  drawButton(btnScriptLoad, COLOR_YELLOW, COLOR_BLACK, "Load");
  drawButton(btnScriptEdit, COLOR_YELLOW, COLOR_BLACK, "Edit");

  if (!isScriptRunning) {
    drawButton(btnScriptStart, COLOR_GREEN, COLOR_BLACK, "Start", false, !systemState.safetyStop);
  } else if (isScriptPaused) {
    drawButton(btnScriptStart, COLOR_ORANGE, COLOR_BLACK, "Resume", false, true);
  } else {
    drawButton(btnScriptStart, COLOR_ORANGE, COLOR_BLACK, "Pause", false, true);
  }

  drawButton(btnScriptEnd, COLOR_RED, COLOR_BLACK, "Stop", false, isScriptRunning);

  if (systemState.recording && systemState.recordingScript) {
    drawButton(btnScriptRecord, COLOR_RED, COLOR_WHITE, "Stop Rec", false, true);
  } else {
    drawButton(btnScriptRecord, currentScript.useRecord ? COLOR_BLUE : COLOR_GRAY, COLOR_WHITE, "Record", false, true);
  }
}

void drawEditPage() {
  tft.fillScreen(COLOR_BLACK);

  drawButton(btnEditBack, COLOR_YELLOW, COLOR_BLACK, "Back");
  drawButton(btnEditStop, COLOR_YELLOW, COLOR_BLACK, "STOP");

  // Script name display
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(currentScript.scriptName, 0, 0, &x1, &y1, &w, &h);
  int nameX = (SCREEN_WIDTH - w) / 2;
  tft.drawRect(nameX - 5, 10, w + 10, 25, COLOR_YELLOW);
  tft.setCursor(nameX, 30);
  tft.print(currentScript.scriptName);

  int divX = SCREEN_WIDTH * 2 / 3;
  tft.drawLine(divX, 45, divX, SCREEN_HEIGHT - 45, COLOR_GRAY);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_WHITE);

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
    tft.drawRect(fieldX, yPos, fieldW, fieldH, COLOR_YELLOW);
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
    tft.drawRect(fieldX, yPos, fieldW, fieldH, COLOR_YELLOW);
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
    tft.drawRect(fieldX, yPos, fieldW, fieldH, COLOR_YELLOW);
    if (currentScript.devices[i].enabled) {
      tft.fillRect(fieldX + 5, yPos + 5, fieldW - 10, fieldH - 10, COLOR_YELLOW);
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
  tft.drawRect(fieldX, fieldY, fieldW, fieldH, COLOR_YELLOW);
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
  tft.drawRect(fieldX, fieldY, fieldW, fieldH, COLOR_YELLOW);
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
  tft.drawRect(fieldX, fieldY, fieldW, fieldH, COLOR_YELLOW);
  if (currentScript.useRecord) {
    tft.fillRect(fieldX + 5, fieldY + 5, fieldW - 10, fieldH - 10, COLOR_YELLOW);
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

  drawButton(btnEditLoad, COLOR_YELLOW, COLOR_BLACK, "Load");
  drawButton(btnEditSave, COLOR_YELLOW, COLOR_BLACK, "Save");
  drawButton(btnEditNew, COLOR_YELLOW, COLOR_BLACK, "New");
}

void drawScriptLoadPage() {
  tft.fillScreen(COLOR_BLACK);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(100, 30);
  tft.print("Select Script");

  tft.fillRect(5, 5, 80, 35, COLOR_YELLOW);
  tft.drawRect(5, 5, 80, 35, COLOR_BLACK);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_BLACK);
  tft.setCursor(25, 28);
  tft.print("Back");

  const char* sortLabels[] = {"Name", "Recent", "Created"};
  drawButton(btnSortDropdown, COLOR_YELLOW, COLOR_BLACK, sortLabels[currentSortMode], false, true);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_WHITE);

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
      tft.fillRect(15, yPos - 2, 400, lineHeight, COLOR_BLUE);
    }

    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(20, yPos + 15);
    tft.print(scriptIdx + 1);
    tft.print(".");

    tft.setCursor(50, yPos + 15);
    tft.print(scriptList[scriptIdx].name);

    tft.setCursor(250, yPos + 15);
    tft.setTextColor(COLOR_GRAY);
    tft.print(formatShortDateTime(scriptList[scriptIdx].dateCreated));
    tft.setTextColor(COLOR_WHITE);
  }

  // Scroll indicators
  if (numScripts > 10) {
    if (guiState.scriptListOffset > 0) {
      tft.fillTriangle(450, 70, 440, 80, 460, 80, COLOR_YELLOW);
    }

    if (guiState.scriptListOffset < (numScripts - 10)) {
      tft.fillTriangle(450, 240, 440, 230, 460, 230, COLOR_YELLOW);
    }
  }

  tft.setTextColor(COLOR_GRAY);
  tft.setCursor(80, 300);
  tft.print("Press 1-9 to select, A to load script");

  if (guiState.selectedScript >= 0) {
    drawButton(btnScriptSelect, COLOR_GREEN, COLOR_BLACK, "Select", false, true);
    drawButton(btnScriptDelete, COLOR_RED, COLOR_BLACK, "Delete", false, true);
  }
}

void drawDeleteConfirmDialog() {
  tft.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);

  tft.fillRect(100, 100, 280, 120, COLOR_GRAY);
  tft.drawRect(100, 100, 280, 120, COLOR_WHITE);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(110, 130);
  tft.print("Delete script:");
  tft.setCursor(110, 150);
  tft.print(guiState.deleteScriptName);
  tft.setCursor(110, 170);
  tft.print("Are you sure?");

  drawButton(btnDeleteYes, COLOR_RED, COLOR_WHITE, "Yes", false, true);
  drawButton(btnDeleteNo, COLOR_YELLOW, COLOR_BLACK, "No", false, true);
}

void drawEditSavePage() {
  tft.fillScreen(COLOR_BLACK);
  drawButton(btnEditSaveBack, COLOR_YELLOW, COLOR_BLACK, "Back");

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(100, 50);
  if (guiState.currentMode == MODE_EDIT_NAME) {
    tft.print("Edit Script Name");
  } else {
    tft.print("Save Script");
  }

  tft.drawRect(50, 80, 380, 40, COLOR_YELLOW);
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
  tft.setTextColor(guiState.alphaMode ? COLOR_GREEN : COLOR_RED);
  tft.print(guiState.alphaMode ? "ON" : "OFF");

  tft.setTextColor(COLOR_WHITE);
  tft.print("  Shift: ");
  tft.setTextColor(guiState.shiftMode ? COLOR_GREEN : COLOR_RED);
  tft.print(guiState.shiftMode ? "ON" : "OFF");

  tft.setTextColor(COLOR_WHITE);
  tft.print("  Caps: ");
  tft.setTextColor(guiState.capsMode ? COLOR_GREEN : COLOR_RED);
  tft.print(guiState.capsMode ? "ON" : "OFF");
}

void drawDateTimePanel() {
  tft.fillScreen(COLOR_BLACK);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(120, 30);
  tft.print("Set Date & Time");

  tft.fillRect(5, 5, 80, 35, COLOR_YELLOW);
  tft.drawRect(5, 5, 80, 35, COLOR_BLACK);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_BLACK);
  tft.setCursor(25, 28);
  tft.print("Back");

  tft.fillRect(SCREEN_WIDTH - 85, 5, 80, 35, COLOR_GREEN);
  tft.drawRect(SCREEN_WIDTH - 85, 5, 80, 35, COLOR_BLACK);
  tft.setCursor(SCREEN_WIDTH - 65, 28);
  tft.print("Save");

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_WHITE);

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
    tft.fillRect(150, controls[i].yPos, 30, 30, COLOR_GRAY);
    tft.drawRect(150, controls[i].yPos, 30, 30, COLOR_BLACK);
    tft.setTextColor(COLOR_BLACK);
    tft.setCursor(158, controls[i].yPos + 20);
    tft.print("-");

    // Value display
    tft.fillRect(180, controls[i].yPos, 60, 30, COLOR_YELLOW);
    tft.drawRect(180, controls[i].yPos, 60, 30, COLOR_BLACK);
    tft.setCursor(190, controls[i].yPos + 20);
    if (i == 0) {
      tft.print(controls[i].value);
    } else {
      tft.print(controls[i].value);
    }

    // Increment button
    tft.fillRect(240, controls[i].yPos, 30, 30, COLOR_GRAY);
    tft.drawRect(240, controls[i].yPos, 30, 30, COLOR_BLACK);
    tft.setCursor(248, controls[i].yPos + 20);
    tft.print("+");

    tft.setTextColor(COLOR_WHITE);
  }
}

void drawDeviceRow(int row) {
  int yPos = 85 + row * 25;
  bool isOn = (switchOutputs[row].state == HIGH);

  // Fill background only in the data area
  tft.fillRect(0, yPos - 17, MAIN_DATA_WIDTH, 25, COLOR_BLACK);

  if (isOn) {
    tft.fillRect(0, yPos - 17, MAIN_DATA_WIDTH, 25, COLOR_PURPLE);
  }

  uint16_t textColor = COLOR_WHITE;

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(textColor);

  tft.setCursor(10, yPos);
  tft.print(switchOutputs[row].name);

  int inaIdx = getInaIndexForSwitch(row);
  if (inaIdx >= 0) {
    tft.setCursor(100, yPos);
    tft.printf("%.2fV", deviceVoltage[inaIdx]);

    tft.setCursor(175, yPos);
    tft.printf("%.4fA", deviceCurrent[inaIdx] / 1000.0);

    tft.setCursor(270, yPos);
    tft.printf("%.3fW", devicePower[inaIdx]);
  }
}

void drawTotalRow() {
  int totalRowY = 85 + numSwitches * 25 + 15;

  // Fill background only in the data area
  tft.fillRect(0, totalRowY - 17, MAIN_DATA_WIDTH, 25, COLOR_BLACK);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_YELLOW);

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

    tft.fillRect(divX + 10, yPos - 15, 150, 30, COLOR_BLACK);

    bool isOn = (switchOutputs[row].state == HIGH);
    int inaIdx = getInaIndexForSwitch(row);

    tft.setFont(&FreeSans9pt7b);
    tft.setCursor(divX + 10, yPos);
    tft.setTextColor(isOn ? COLOR_GREEN : COLOR_RED);
    tft.print(switchOutputs[row].name);
    tft.print(": ");
    tft.print(isOn ? "ON" : "OFF");

    if (inaIdx >= 0) {
      tft.setCursor(divX + 10, yPos + 15);
      tft.setTextColor(COLOR_CYAN);
      tft.printf("%.1fV %.2fA", deviceVoltage[inaIdx], deviceCurrent[inaIdx] / 1000.0);
    }
  }
}

void refreshHeaderClock() {
  if (guiState.currentMode != MODE_MAIN && guiState.currentMode != MODE_SCRIPT) return;

  char buff[64];
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE, COLOR_BLACK);

  if (guiState.currentMode == MODE_MAIN) {
    if (!isScriptRunning) {
      String nowStr = getCurrentTimeString();
      nowStr.toCharArray(buff, sizeof(buff));
    } else {
      if (scriptTimeSeconds < 0) {
        snprintf(buff, sizeof(buff), "T-%ld", labs(scriptTimeSeconds));
      } else {
        snprintf(buff, sizeof(buff), "T+%ld", scriptTimeSeconds);
      }
    }
  } else {
    if (!isScriptRunning) {
      strncpy(buff, currentScript.scriptName, sizeof(buff)-1);
      buff[sizeof(buff)-1] = 0;
    } else {
      tft.setFont(&FreeSans9pt7b);
      long sTime = scriptTimeSeconds;
      if (sTime < 0) {
        snprintf(buff, sizeof(buff), "%.15s - T-%ld", currentScript.scriptName, labs(sTime));
      } else {
        snprintf(buff, sizeof(buff), "%.15s - T+%ld", currentScript.scriptName, sTime);
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
  tft.fillRect(clearX, 10, clearW, 25, COLOR_BLACK);
  tft.setCursor(tx, guiState.currentMode == MODE_SCRIPT && isScriptRunning ? 25 : 30);
  tft.print(buff);
}

void updateLockButton() {
  drawButton(btnLock,
             systemState.lock ? COLOR_PURPLE : COLOR_YELLOW,
             systemState.lock ? COLOR_WHITE : COLOR_BLACK,
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
