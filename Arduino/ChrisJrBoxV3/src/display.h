/**
 * @file display.h
 * @brief Display and GUI functions
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7796S.h>
#include "types.h"

// Forward declarations
void drawDeviceGraph(int deviceIndex, GraphDataType dataType, uint16_t color,
                    float minY, float maxY, int thickness);
void drawAllGraphSettings();
void drawDeviceGraphSettings(int deviceIndex);

// Display initialization
void initDisplay();
void updateDisplay(unsigned long currentMillis);
void updateDisplayElements();

// Screen drawing functions
void drawInitializationScreen();
void updateInitializationScreen();
void drawMainScreen();
void drawSettingsPanel();
void drawNetworkPanel();
void drawNetworkEditPanel();
void drawKeypadPanel();
void drawScriptPage();
void drawEditPage();
void drawScriptLoadPage();
void drawEditSavePage();
void drawDateTimePanel();
void drawDeleteConfirmDialog();
void drawAboutPage();

// Element drawing functions
void drawButton(ButtonRegion& btn, uint16_t bgColor, uint16_t textColor,
                const char* label, bool pressed = false, bool enabled = true);
void drawDeviceRow(int row);
void drawTotalRow();
void updateLiveValueRow(int row);
void refreshHeaderClock();
void updateLockButton();

// External objects
extern Adafruit_ST7796S tft;
extern GUIState guiState;

// Main screen buttons
extern ButtonRegion btnRecord;
extern ButtonRegion btnSDRefresh;
extern ButtonRegion btnStop;
extern ButtonRegion btnLock;
extern ButtonRegion btnAllOn;
extern ButtonRegion btnAllOff;
extern ButtonRegion btnScript;
extern ButtonRegion btnEdit;
extern ButtonRegion btnSettings;
extern ButtonRegion btnGraph;

// Graph buttons
extern ButtonRegion btnGraphBack;
extern ButtonRegion btnGraphStop;
extern ButtonRegion btnGraphClear;
extern ButtonRegion btnGraphPause;
extern ButtonRegion btnGraphSettings;
extern ButtonRegion btnGraphSettingsBack;
extern ButtonRegion btnGraphDataType;
extern ButtonRegion btnGraphMinY;
extern ButtonRegion btnGraphMaxY;
extern ButtonRegion btnGraphThickness;
extern ButtonRegion btnGraphTimeRange;
extern ButtonRegion btnGraphDisplay;
extern ButtonRegion btnGraphDisplayBack;
extern ButtonRegion btnGraphDataTypeFooter;

// Settings panel buttons
extern ButtonRegion btnSettingsBack;
extern ButtonRegion btnSettingsStop;
extern ButtonRegion btnNetwork;
extern ButtonRegion btnAbout;
extern ButtonRegion btnFanSpeedInput;
extern ButtonRegion btnUpdateRateInput;
extern ButtonRegion btnSetTimeDate;
extern ButtonRegion btnTimeFormatToggle;
extern ButtonRegion btnDarkModeToggle;

// Network buttons
extern ButtonRegion btnNetworkBack;
extern ButtonRegion btnNetworkStop;
extern ButtonRegion btnNetworkEdit;
extern ButtonRegion btnEnableLanToggle;
extern ButtonRegion btnNetworkEditBack;
extern ButtonRegion btnNetworkEditStop;
extern ButtonRegion btnNetworkEditSave;
extern ButtonRegion btnDhcpToggle;

// Script buttons
extern ButtonRegion btnScriptBack;
extern ButtonRegion btnScriptStop;
extern ButtonRegion btnScriptLoad;
extern ButtonRegion btnScriptEdit;
extern ButtonRegion btnScriptStart;
extern ButtonRegion btnScriptEnd;
extern ButtonRegion btnScriptRecord;

// Edit buttons
extern ButtonRegion btnEditBack;
extern ButtonRegion btnEditStop;
extern ButtonRegion btnEditLoad;
extern ButtonRegion btnEditSave;
extern ButtonRegion btnEditNew;

// Other buttons
extern ButtonRegion btnKeypadBack;
extern ButtonRegion btnEditSaveBack;
extern ButtonRegion btnEditNameBack;
extern ButtonRegion btnDateTimeBack;
extern ButtonRegion btnEditFieldBack;
extern ButtonRegion btnScriptSelect;
extern ButtonRegion btnScriptDelete;
extern ButtonRegion btnSortDropdown;
extern ButtonRegion btnDeleteYes;
extern ButtonRegion btnDeleteNo;
extern ButtonRegion btnAboutBack;
extern ButtonRegion btnAboutStop;

// Field access functions
DeviceTimingField* getDeviceFields();
EditField* getEditFields();
NetworkEditField* getNetworkFields();
int* getNumNetworkFields();

#endif // DISPLAY_H
