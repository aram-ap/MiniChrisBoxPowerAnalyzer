/**
 * @file touch.cpp
 * @brief Touch screen handling implementation
 */

#include "touch.h"
#include "config.h"
#include "display.h"
#include "switches.h"
#include "script.h"
#include "datalog.h"
#include "network.h"
#include "settings.h"
#include "time_utils.h"
#include "graphs.h"
// extern ButtonRegion btnGraphDisplay;
// extern ButtonRegion btnGraphDisplayBack;
// extern ButtonRegion btnGraphDataTypeFooter;

// Touch object
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

// External references
extern SystemState systemState;
extern GUIState guiState;
extern NetworkConfig networkConfig;

// Constants
const unsigned long touchDebounceMs = 200;

void initTouch() {
  ts.begin();
  ts.setRotation(1);
}

void handleTouch(unsigned long currentMillis) {
  if (ts.touched()) {
    if (currentMillis - systemState.lastTouchTime > touchDebounceMs) {
      TS_Point p = ts.getPoint();
      int16_t x = map(p.x, 200, 3800, 0, SCREEN_WIDTH);
      x = SCREEN_WIDTH - x;
      int16_t y = map(p.y, 200, 3800, SCREEN_HEIGHT, 0);

      // Route touch handling based on current mode
      switch(guiState.currentMode) {
        case MODE_MAIN:
          handleTouchMain(x, y);
        break;
        case MODE_SETTINGS:
          handleTouchSettings(x, y);
        break;
        case MODE_NETWORK:
          handleTouchNetwork(x, y);
        break;
        case MODE_NETWORK_EDIT:
          handleTouchNetworkEdit(x, y);
        break;
        case MODE_SCRIPT:
          handleTouchScript(x, y);
        break;
        case MODE_SCRIPT_LOAD:
          handleTouchScriptLoad(x, y);
        break;
        case MODE_EDIT:
          handleTouchEdit(x, y);
        break;
        case MODE_EDIT_LOAD:
          handleTouchScriptLoad(x, y);
        break;
        case MODE_EDIT_FIELD:
          handleTouchEditField(x, y);
        break;
        case MODE_DATE_TIME:
          handleTouchDateTime(x, y);
        break;
        case MODE_EDIT_SAVE:
          handleTouchEditSave(x,y);
        break;
        case MODE_EDIT_NAME:
          handleTouchEditName(x, y);
        break;
        case MODE_KEYPAD:
          handleTouchKeypad(x,y);
        break;
        case MODE_DELETE_CONFIRM:
          handleTouchDeleteConfirm(x, y);
        break;
        case MODE_ABOUT:
          handleTouchAbout(x, y);
        break;
        case MODE_GRAPH:
          handleTouchGraph(x, y);
        break;
        case MODE_GRAPH_SETTINGS:
          handleTouchGraphSettings(x, y);
        case MODE_GRAPH_DISPLAY:
          handleTouchGraphDisplaySettings(x, y);
        break;

        break;
        default:
          break;
      }
      systemState.lastTouchTime = currentMillis;
    }
  }
}

void handleTouchGraph(int16_t x, int16_t y) {
  // Handle back button
  if (touchInButton(x, y, btnGraphBack)) {
    guiState.currentMode = MODE_MAIN;
    drawMainScreen();
    return;
  }

  // Handle stop button
  if (touchInButton(x, y, btnGraphStop)) {
    if (!systemState.safetyStop) {
      systemState.lockBeforeStop = systemState.lock;
      systemState.safetyStop = true;
      setAllOutputsOff();
      if (isScriptRunning) stopScript(true);
      if (systemState.recording) stopRecording();
    } else {
      systemState.safetyStop = false;
      bool prevLock = systemState.lock;
      systemState.lock = systemState.lockBeforeStop;
      if (!systemState.lock && prevLock) syncOutputsToSwitches();
    }
    drawGraphPage();
    return;
  }

  // Handle footer buttons
  if (touchInButton(x, y, btnGraphClear)) {
    clearGraphData();
    drawGraphPage();
    return;
  }

  if (touchInButton(x, y, btnGraphPause)) {
    if (graphSettings.isPaused) {
      resumeGraphData();
    } else {
      pauseGraphData();
    }
    drawGraphPage();
    return;
  }

  if (touchInButton(x, y, btnGraphSettings)) {
    guiState.currentMode = MODE_GRAPH_SETTINGS;
    drawGraphSettingsPage();
    return;
  }

  if (touchInButton(x, y, btnGraphDataTypeFooter)) {
    cycleAllGraphDataType();
    return;
  }

  // Handle tab touches
  int tabWidth = 50;
  int startX = 90;

  for (int i = 0; i < 7; i++) {
    if (x >= startX + i * tabWidth && x <= startX + (i + 1) * tabWidth - 2 &&
        y >= 5 && y <= 5 + GRAPH_TAB_HEIGHT) {
      switchGraphTab((GraphTab)i);
      return;
        }
  }
}

void handleTouchGraphSettings(int16_t x, int16_t y) {
  if (touchInButton(x, y, btnGraphSettingsBack)) {
    guiState.currentMode = MODE_GRAPH;
    graphState.needsFullRedraw = true;
    drawGraphPage();
    return;  // Ensure immediate return
  }

  // Handle display button
  if (touchInButton(x, y, btnGraphDisplay)) {
    guiState.currentMode = MODE_GRAPH_DISPLAY;
    drawGraphDisplaySettingsPage();
    return;
  }

  // Handle settings based on current tab
  if (guiState.currentGraphTab == GRAPH_TAB_ALL) {
    // Data type button
    if (touchInButton(x, y, btnGraphDataType)) {
      cycleAllGraphDataType();
      drawGraphSettingsPage();
      return;
    }

    // Min/Max buttons
    if (touchInButton(x, y, btnGraphMinY)) {
      guiState.keypadMode = KEYPAD_GRAPH_MIN_Y;
      guiState.currentMode = MODE_KEYPAD;
      sprintf(guiState.keypadBuffer, "%.2f",
              graphSettings.all.axisRanges[graphSettings.all.dataType][0]);
      guiState.keypadPos = strlen(guiState.keypadBuffer);
      drawKeypadPanel();
      return;
    }

    if (touchInButton(x, y, btnGraphMaxY)) {
      guiState.keypadMode = KEYPAD_GRAPH_MAX_Y;
      guiState.currentMode = MODE_KEYPAD;
      sprintf(guiState.keypadBuffer, "%.2f",
              graphSettings.all.axisRanges[graphSettings.all.dataType][1]);
      guiState.keypadPos = strlen(guiState.keypadBuffer);
      drawKeypadPanel();
      return;
    }

    // Line thickness
    if (touchInButton(x, y, btnGraphThickness)) {
      graphSettings.all.lineThickness = (graphSettings.all.lineThickness % 3) + 1;
      saveGraphSettings();
      drawGraphSettingsPage();
      return;
    }

    // Time range
    if (touchInButton(x, y, btnGraphTimeRange)) {
      guiState.keypadMode = KEYPAD_GRAPH_TIME_RANGE;
      guiState.currentMode = MODE_KEYPAD;
      sprintf(guiState.keypadBuffer, "%.2f", graphSettings.timeRange);
      guiState.keypadPos = strlen(guiState.keypadBuffer);
      drawKeypadPanel();
      return;
    }

    // Device buttons instead of checkboxes
    for (int i = 0; i < 6; i++) {
      int buttonX = 30 + (i % 3) * 140;
      int buttonY = 110 + (i / 3) * 40;

      if (x >= buttonX && x <= buttonX + 120 && y >= buttonY && y <= buttonY + 30) {
        toggleDeviceInAll(i);
        drawGraphSettingsPage();
        return;
      }
    }

    // Auto scale checkbox - FIXED: Correct positioning
    if (x >= 120 && x <= 145 && y >= 240 && y <= 265) {
      graphSettings.autoFitEnabled = !graphSettings.autoFitEnabled;
      saveGraphSettings();
      drawGraphSettingsPage();
      return;
    }
  } else {
    // Individual device settings
    int deviceIndex = (int)guiState.currentGraphTab - 1;

    // Data type buttons
    for (int i = 0; i < 3; i++) {
      if (x >= 30 + i * 120 && x <= 130 + i * 120 && y >= 75 && y <= 105) {
        toggleDeviceGraphDataType(deviceIndex, (GraphDataType)i);
        drawGraphSettingsPage();
        return;
      }
    }

    // Color selection
    for (int i = 0; i < 8; i++) {
      int colorX = 30 + i * 50;
      int colorY = 150;
      if (x >= colorX && x <= colorX + 35 && y >= colorY && y <= colorY + 25) {
        setDeviceGraphColor(deviceIndex, DEFAULT_GRAPH_COLORS[i]);
        drawGraphSettingsPage();
        return;
      }
    }

    // Min/Max buttons
    if (touchInButton(x, y, btnGraphMinY)) {
      guiState.keypadMode = KEYPAD_GRAPH_MIN_Y;
      guiState.currentMode = MODE_KEYPAD;
      sprintf(guiState.keypadBuffer, "%.2f",
              graphSettings.devices[deviceIndex].axisRanges[graphSettings.devices[deviceIndex].dataType][0]);
      guiState.keypadPos = strlen(guiState.keypadBuffer);
      drawKeypadPanel();
      return;
    }

    if (touchInButton(x, y, btnGraphMaxY)) {
      guiState.keypadMode = KEYPAD_GRAPH_MAX_Y;
      guiState.currentMode = MODE_KEYPAD;
      sprintf(guiState.keypadBuffer, "%.2f",
              graphSettings.devices[deviceIndex].axisRanges[graphSettings.devices[deviceIndex].dataType][1]);
      guiState.keypadPos = strlen(guiState.keypadBuffer);
      drawKeypadPanel();
      return;
    }

    // Auto scale checkbox - FIXED: Correct positioning
    if (x >= 120 && x <= 145 && y >= 240 && y <= 265) {
      graphSettings.autoFitEnabled = !graphSettings.autoFitEnabled;
      saveGraphSettings();
      drawGraphSettingsPage();
      return;
    }
  }
}

void handleTouchGraphDisplaySettings(int16_t x, int16_t y) {
  if (touchInButton(x, y, btnGraphDisplayBack)) {
    guiState.currentMode = MODE_GRAPH_SETTINGS;
    drawGraphSettingsPage();
    return;
  }

  // Handle toggles and inputs
  // Antialiasing toggle
  if (x >= 180 && x <= 205 && y >= 45 && y <= 70) {
    graphSettings.enableAntialiasing = !graphSettings.enableAntialiasing;
    saveGraphSettings();
    drawGraphDisplaySettingsPage();
    return;
  }

  // Grids toggle
  if (x >= 180 && x <= 205 && y >= 85 && y <= 110) {
    graphSettings.showGrids = !graphSettings.showGrids;
    saveGraphSettings();
    drawGraphDisplaySettingsPage();
    return;
  }

  // Max points input
  if (x >= 180 && x <= 260 && y >= 125 && y <= 150) {
    guiState.keypadMode = KEYPAD_GRAPH_MAX_POINTS;
    guiState.currentMode = MODE_KEYPAD;
    sprintf(guiState.keypadBuffer, "%d", graphSettings.effectiveMaxPoints);
    guiState.keypadPos = strlen(guiState.keypadBuffer);
    drawKeypadPanel();
    return;
  }

  // Refresh rate input
  if (x >= 180 && x <= 260 && y >= 165 && y <= 190) {
    guiState.keypadMode = KEYPAD_GRAPH_REFRESH_RATE;
    guiState.currentMode = MODE_KEYPAD;
    sprintf(guiState.keypadBuffer, "%lu", graphSettings.graphRefreshRate);
    guiState.keypadPos = strlen(guiState.keypadBuffer);
    drawKeypadPanel();
    return;
  }
}

bool touchInButton(int16_t x, int16_t y, const ButtonRegion& btn) {
  return btn.enabled && (x >= btn.x && x <= btn.x+btn.w && y >= btn.y && y <= btn.y+btn.h);
}

void handleTouchMain(int16_t x, int16_t y) {
  ButtonRegion* buttons[] = {
    &btnRecord, &btnSDRefresh, &btnStop, &btnLock, &btnAllOn,
    &btnAllOff, &btnScript, &btnEdit, &btnSettings, &btnGraph
  };
  int btnCount = sizeof(buttons) / sizeof(buttons[0]);
  int pressedIdx = -1;

  for (int i=0; i<btnCount; i++) {
    if (touchInButton(x, y, *buttons[i])) {
      pressedIdx = i;
      break;
    }
  }

  // Show button press feedback
  if (pressedIdx >= 0) {
    ButtonRegion* btn = buttons[pressedIdx];
    drawButton(*btn, btn->color, COLOR_WHITE, btn->label, true, btn->enabled);
    delay(80);
    drawButton(*btn, btn->color, COLOR_BLACK, btn->label, false, btn->enabled);
  }

  // Process button actions
  if (touchInButton(x, y, btnRecord)) {
    if (!btnRecord.enabled) return;
    if (!systemState.recording) {
      startRecording(false);
    } else {
      stopRecording();
    }
  }
  else if (touchInButton(x, y, btnSDRefresh)) {
    smartCheckSDCard();
    checkInternalSD();
  }
  else if (touchInButton(x, y, btnStop)) {
    if (!systemState.safetyStop) {
      systemState.lockBeforeStop = systemState.lock;
      systemState.safetyStop = true;
      setAllOutputsOff();
      drawButton(btnStop, COLOR_PURPLE, COLOR_WHITE, "RELEASE", false, btnStop.enabled);

      if (isScriptRunning) {
        stopScript(true);
      }
      if (systemState.recording) {
        stopRecording();
      }
    } else {
      systemState.safetyStop = false;
      bool prevLock = systemState.lock;
      systemState.lock = systemState.lockBeforeStop;
      drawButton(btnStop, COLOR_YELLOW, COLOR_BLACK, "STOP", false, btnStop.enabled);
      if (!systemState.lock && prevLock) syncOutputsToSwitches();
    }
  }
  else if (touchInButton(x, y, btnLock)) {
    bool prevLock = systemState.lock;
    systemState.lock = !systemState.lock;
    updateLockButton();
    if (!systemState.lock && prevLock) syncOutputsToSwitches();
  }
  else if (touchInButton(x, y, btnAllOn)) {
    if (isScriptRunning) return;
    if (!systemState.safetyStop) {
      systemState.lock = true;
      updateLockButton();
      for (int i=0; i<numSwitches; i++) {
        digitalWrite(switchOutputs[i].outputPin, HIGH);
        switchOutputs[i].state = HIGH;
        drawDeviceRow(i);
      }
    }
  }
  else if (touchInButton(x, y, btnAllOff)) {
    if (isScriptRunning) return;
    if (!systemState.safetyStop) {
      systemState.lock = true;
      updateLockButton();
      setAllOutputsOff();
      for (int i=0; i<numSwitches; i++) drawDeviceRow(i);
    }
  }
  else if (touchInButton(x, y, btnScript)) {
    guiState.currentMode = MODE_SCRIPT;
    drawScriptPage();
    return;
  }
  else if (touchInButton(x, y, btnEdit)) {
    guiState.currentMode = MODE_EDIT;
    drawEditPage();
    return;
  }
  else if (touchInButton(x, y, btnSettings)) {
    guiState.currentMode = MODE_SETTINGS;
    drawSettingsPanel();
    return;
  }
  else if (touchInButton(x, y, btnGraph)) {
    guiState.currentMode = MODE_GRAPH;
    guiState.currentGraphTab = GRAPH_TAB_ALL;
    drawGraphPage();
    return;
  }
}

void handleTouchSettings(int16_t x, int16_t y) {
  ButtonRegion* buttons[] = {
    &btnSettingsBack, &btnSettingsStop, &btnFanSpeedInput, &btnUpdateRateInput,
    &btnSetTimeDate, &btnTimeFormatToggle, &btnDarkModeToggle, &btnNetwork, &btnAbout
  };
  int btnCount = sizeof(buttons) / sizeof(buttons[0]);

  for (int i = 0; i < btnCount; i++) {
    if (touchInButton(x, y, *buttons[i])) {
      ButtonRegion* btn = buttons[i];

      drawButton(*btn, COLOR_BTN_PRESS, COLOR_WHITE, btn->label, true, btn->enabled);
      delay(150);

      if (btn == &btnSettingsBack) {
        guiState.currentMode = MODE_MAIN;
        drawMainScreen();
        return;
      }
      else if (btn == &btnSettingsStop) {
        if (!systemState.safetyStop) {
          systemState.lockBeforeStop = systemState.lock;
          systemState.safetyStop = true;
          setAllOutputsOff();
          if (isScriptRunning) stopScript(true);
          if (systemState.recording) stopRecording();
        } else {
          systemState.safetyStop = false;
          bool prevLock = systemState.lock;
          systemState.lock = systemState.lockBeforeStop;
          if (!systemState.lock && prevLock) syncOutputsToSwitches();
        }
        drawSettingsPanel();
        return;
      }
      else if (btn == &btnFanSpeedInput) {
        guiState.currentMode = MODE_KEYPAD;
        guiState.keypadMode = KEYPAD_FAN_SPEED;
        guiState.keypadPos = 0;
        guiState.keypadBuffer[0] = 0;
        drawKeypadPanel();
        return;
      }
      else if (btn == &btnUpdateRateInput) {
        guiState.currentMode = MODE_KEYPAD;
        guiState.keypadMode = KEYPAD_UPDATE_RATE;
        guiState.keypadPos = 0;
        guiState.keypadBuffer[0] = 0;
        drawKeypadPanel();
        return;
      }
      else if (btn == &btnSetTimeDate) {
        time_t t = now();
        breakTime(t, tmSet);
        guiState.currentMode = MODE_DATE_TIME;
        drawDateTimePanel();
        return;
      }
      else if (btn == &btnTimeFormatToggle) {
        systemState.use24HourFormat = !systemState.use24HourFormat;
        saveSettingsToEEPROM();
        drawSettingsPanel();
        return;
      }
      else if (btn == &btnDarkModeToggle) {
        systemState.darkMode = !systemState.darkMode;
        saveSettingsToEEPROM();
        applyDarkMode();
        drawSettingsPanel();
        return;
      }
      else if (btn == &btnNetwork) {
        guiState.currentMode = MODE_NETWORK;
        drawNetworkPanel();
        return;
      }
      else if (btn == &btnAbout) {
        guiState.currentMode = MODE_ABOUT;
        drawAboutPage();
        return;
      }

      drawSettingsPanel();
      return;
    }
  }
}

void handleTouchNetwork(int16_t x, int16_t y) {
  ButtonRegion* buttons[] = {
    &btnNetworkBack, &btnNetworkStop, &btnEnableLanToggle, &btnNetworkEdit
  };
  int btnCount = sizeof(buttons) / sizeof(buttons[0]);

  for (int i = 0; i < btnCount; i++) {
    if (touchInButton(x, y, *buttons[i])) {
      ButtonRegion* btn = buttons[i];

      drawButton(*btn, COLOR_BTN_PRESS, COLOR_WHITE, btn->label, true, btn->enabled);
      delay(150);

      if (btn == &btnNetworkBack) {
        guiState.currentMode = MODE_SETTINGS;
        drawSettingsPanel();
        return;
      }
      else if (btn == &btnNetworkStop) {
        if (!systemState.safetyStop) {
          systemState.lockBeforeStop = systemState.lock;
          systemState.safetyStop = true;
          setAllOutputsOff();
          if (isScriptRunning) stopScript(true);
          if (systemState.recording) stopRecording();
        } else {
          systemState.safetyStop = false;
          bool prevLock = systemState.lock;
          systemState.lock = systemState.lockBeforeStop;
          if (!systemState.lock && prevLock) syncOutputsToSwitches();
        }
        drawNetworkPanel();
        return;
      }
      else if (btn == &btnEnableLanToggle) {
        networkConfig.enableEthernet = !networkConfig.enableEthernet;
        saveNetworkConfig();
        drawNetworkPanel();
        return;
      }
      else if (btn == &btnNetworkEdit) {
        guiState.currentMode = MODE_NETWORK_EDIT;
        loadNetworkFieldsFromConfig();
        drawNetworkEditPanel();
        return;
      }

      drawNetworkPanel();
      return;
    }
  }
}

void handleTouchNetworkEdit(int16_t x, int16_t y) {
  ButtonRegion* buttons[] = {
    &btnNetworkEditBack, &btnNetworkEditStop, &btnDhcpToggle, &btnNetworkEditSave
  };
  int btnCount = sizeof(buttons) / sizeof(buttons[0]);

  for (int i = 0; i < btnCount; i++) {
    if (touchInButton(x, y, *buttons[i])) {
      ButtonRegion* btn = buttons[i];

      drawButton(*btn, COLOR_BTN_PRESS, COLOR_WHITE, btn->label, true, btn->enabled);
      delay(150);

      if (btn == &btnNetworkEditBack) {
        guiState.currentMode = MODE_NETWORK;
        drawNetworkPanel();
        return;
      }
      else if (btn == &btnNetworkEditStop) {
        if (!systemState.safetyStop) {
          systemState.lockBeforeStop = systemState.lock;
          systemState.safetyStop = true;
          setAllOutputsOff();
          if (isScriptRunning) stopScript(true);
          if (systemState.recording) stopRecording();
        } else {
          systemState.safetyStop = false;
          bool prevLock = systemState.lock;
          systemState.lock = systemState.lockBeforeStop;
          if (!systemState.lock && prevLock) syncOutputsToSwitches();
        }
        drawNetworkEditPanel();
        return;
      }
      else if (btn == &btnDhcpToggle) {
        networkConfig.useDHCP = !networkConfig.useDHCP;
        drawNetworkEditPanel();
        return;
      }
      else if (btn == &btnNetworkEditSave) {
        saveNetworkConfig();
        guiState.currentMode = MODE_NETWORK;
        drawNetworkPanel();
        return;
      }

      drawNetworkEditPanel();
      return;
    }
  }

  // Handle network field touch
  NetworkEditField* networkFields = getNetworkFields();
  for (int i = 0; i < guiState.numNetworkFields; i++) {
    if (x >= networkFields[i].x && x <= (networkFields[i].x + networkFields[i].w) &&
        y >= networkFields[i].y && y <= (networkFields[i].y + networkFields[i].h)) {

      guiState.selectedNetworkField = i;
      strcpy(guiState.keypadBuffer, networkFields[i].value);
      guiState.keypadPos = strlen(guiState.keypadBuffer);

      if (networkFields[i].fieldType == 0) {
        guiState.keypadMode = KEYPAD_NETWORK_IP;
      } else if (networkFields[i].fieldType == 1) {
        guiState.keypadMode = KEYPAD_NETWORK_PORT;
      } else {
        guiState.keypadMode = KEYPAD_NETWORK_TIMEOUT;
      }

      guiState.currentMode = MODE_KEYPAD;
      drawKeypadPanel();
      return;
    }
  }
}

void handleTouchScript(int16_t x, int16_t y) {
  ButtonRegion* buttons[] = {
    &btnScriptBack, &btnScriptStop,
    &btnScriptLoad, &btnScriptEdit,
    &btnScriptStart, &btnScriptEnd, &btnScriptRecord
  };
  int btnCount = sizeof(buttons)/sizeof(buttons[0]);
  int pressedIdx = -1;

  for (int i=0; i<btnCount; i++) {
    if (touchInButton(x, y, *buttons[i])) {
      pressedIdx = i;
      break;
    }
  }
  if (pressedIdx >= 0) {
    ButtonRegion* btn = buttons[pressedIdx];
    drawButton(*btn, btn->color, COLOR_WHITE, btn->label, true, btn->enabled);
    delay(80);
    drawButton(*btn, btn->color, COLOR_BLACK, btn->label, false, btn->enabled);
  }

  if (touchInButton(x, y, btnScriptBack)) {
    guiState.currentMode = MODE_MAIN;
    drawMainScreen();
    return;
  }
  if (touchInButton(x, y, btnScriptStop)) {
    if (!systemState.safetyStop) {
      systemState.lockBeforeStop = systemState.lock;
      systemState.safetyStop = true;
      setAllOutputsOff();
      drawButton(btnScriptStop, COLOR_PURPLE, COLOR_WHITE, "RELEASE",
                 false, btnScriptStop.enabled);
      if (isScriptRunning) {
        stopScript(true);
      }
      if (systemState.recording) {
        stopRecording();
      }
    } else {
      systemState.safetyStop = false;
      bool prevLock = systemState.lock;
      systemState.lock = systemState.lockBeforeStop;
      drawButton(btnScriptStop, COLOR_YELLOW, COLOR_BLACK, "STOP",
                 false, btnScriptStop.enabled);
      if (!systemState.lock && prevLock) syncOutputsToSwitches();
    }
    return;
  }
  if (touchInButton(x, y, btnScriptLoad)) {
    guiState.previousMode = MODE_SCRIPT;
    guiState.currentMode = MODE_SCRIPT_LOAD;
    guiState.selectedScript = -1;
    guiState.highlightedScript = -1;
    drawScriptLoadPage();
    return;
  }
  if (touchInButton(x, y, btnScriptEdit)) {
    guiState.currentMode = MODE_EDIT;
    drawEditPage();
    return;
  }
  if (touchInButton(x, y, btnScriptStart)) {
    if (!isScriptRunning && !systemState.safetyStop) {
      startScript();
      drawScriptPage();
    } else if (isScriptRunning && !isScriptPaused) {
      pauseScript();
      drawScriptPage();
    } else if (isScriptRunning && isScriptPaused) {
      resumeScript();
      drawScriptPage();
    }
    return;
  }
  if (touchInButton(x, y, btnScriptEnd)) {
    if (isScriptRunning) {
      stopScript(true);
      setAllOutputsOff();
      drawScriptPage();
    }
    return;
  }
  if (touchInButton(x, y, btnScriptRecord)) {
    if (systemState.recording && systemState.recordingScript) {
      stopRecording();
    } else {
      currentScript.useRecord = !currentScript.useRecord;
    }
    drawScriptPage();
    return;
  }
}

void handleTouchEdit(int16_t x, int16_t y) {
  // Handle script name editing
  if (y >= 10 && y <= 35 && x >= 100 && x <= 380) {
    strcpy(guiState.keypadBuffer, currentScript.scriptName);
    guiState.keypadPos = strlen(guiState.keypadBuffer);
    guiState.alphaMode = true;
    guiState.shiftMode = false;
    guiState.capsMode = false;
    guiState.currentMode = MODE_EDIT_NAME;
    guiState.keypadMode = KEYPAD_SCRIPT_NAME;
    drawEditSavePage();
    return;
  }

  ButtonRegion* buttons[] = {
    &btnEditBack, &btnEditStop, &btnEditLoad, &btnEditSave, &btnEditNew
  };
  int btnCount = sizeof(buttons)/sizeof(buttons[0]);
  int pressedIdx = -1;

  for(int i=0; i<btnCount; i++) {
    if(touchInButton(x, y, *buttons[i])) {
      pressedIdx = i;
      break;
    }
  }
  if (pressedIdx >= 0) {
    ButtonRegion* btn = buttons[pressedIdx];
    drawButton(*btn, btn->color, COLOR_WHITE, btn->label, true, btn->enabled);
    delay(80);
    drawButton(*btn, btn->color, COLOR_BLACK, btn->label, false, btn->enabled);
  }

  if (touchInButton(x, y, btnEditBack)) {
    guiState.currentMode = MODE_MAIN;
    drawMainScreen();
    return;
  }
  if (touchInButton(x, y, btnEditStop)) {
    if (!systemState.safetyStop) {
      systemState.lockBeforeStop = systemState.lock;
      systemState.safetyStop = true;
      setAllOutputsOff();
      drawButton(btnEditStop, COLOR_PURPLE, COLOR_BLACK, "RELEASE",
                 false, btnEditStop.enabled);
      if (isScriptRunning) {
        stopScript(true);
      }
      if (systemState.recording) {
        stopRecording();
      }
    } else {
      systemState.safetyStop = false;
      bool prevLock = systemState.lock;
      systemState.lock = systemState.lockBeforeStop;
      drawButton(btnEditStop, COLOR_YELLOW, COLOR_BLACK, "STOP",
                 false, btnEditStop.enabled);
      if (!systemState.lock && prevLock) syncOutputsToSwitches();
    }
    return;
  }
  if (touchInButton(x, y, btnEditLoad)) {
    guiState.previousMode = MODE_EDIT;
    guiState.currentMode = MODE_EDIT_LOAD;
    guiState.selectedScript = -1;
    guiState.highlightedScript = -1;
    drawScriptLoadPage();
    return;
  }
  if (touchInButton(x, y, btnEditSave)) {
    guiState.previousMode = MODE_EDIT;
    guiState.currentMode = MODE_EDIT_SAVE;
    strcpy(guiState.keypadBuffer, currentScript.scriptName);
    guiState.keypadPos = strlen(guiState.keypadBuffer);
    guiState.isEditingName = true;
    guiState.alphaMode = true;
    guiState.shiftMode = false;
    guiState.capsMode = false;
    guiState.keypadMode = KEYPAD_SCRIPT_NAME;
    drawEditSavePage();
    return;
  }
  if (touchInButton(x, y, btnEditNew)) {
    createNewScript();
    drawEditPage();
    return;
  }

  // Handle edit field touches
  EditField* editFields = getEditFields();
  for (int i = 0; i < guiState.numEditFields; i++) {
    if (x >= editFields[i].x && x <= (editFields[i].x + editFields[i].w) &&
        y >= editFields[i].y && y <= (editFields[i].y + editFields[i].h)) {

      guiState.selectedField = i;
      if (i == 0) {
        guiState.keypadMode = KEYPAD_SCRIPT_TSTART;
        strcpy(guiState.keypadBuffer, editFields[i].value);
        guiState.keypadPos = strlen(guiState.keypadBuffer);
        guiState.currentMode = MODE_KEYPAD;
        drawKeypadPanel();
      }
      else if (i == 1) {
        guiState.keypadMode = KEYPAD_SCRIPT_TEND;
        strcpy(guiState.keypadBuffer, editFields[i].value);
        guiState.keypadPos = strlen(guiState.keypadBuffer);
        guiState.currentMode = MODE_KEYPAD;
        drawKeypadPanel();
      }
      else if (i == 2) {
        currentScript.useRecord = !currentScript.useRecord;
        drawEditPage();
      }
      return;
    }
  }

  // Handle device field touches
  DeviceTimingField* deviceFields = getDeviceFields();
  for (int i = 0; i < guiState.numDeviceFields; i++) {
    if (x >= deviceFields[i].x && x <= (deviceFields[i].x + deviceFields[i].w) &&
        y >= deviceFields[i].y && y <= (deviceFields[i].y + deviceFields[i].h)) {

      guiState.selectedDeviceField = i;
      int deviceIdx = deviceFields[i].deviceIndex;

      if (deviceFields[i].fieldType == 0) {
        guiState.keypadMode = KEYPAD_DEVICE_ON_TIME;
        snprintf(guiState.keypadBuffer, sizeof(guiState.keypadBuffer), "%d", currentScript.devices[deviceIdx].onTime);
        guiState.keypadPos = strlen(guiState.keypadBuffer);
        guiState.currentMode = MODE_KEYPAD;
        drawKeypadPanel();
      }
      else if (deviceFields[i].fieldType == 1) {
        guiState.keypadMode = KEYPAD_DEVICE_OFF_TIME;
        snprintf(guiState.keypadBuffer, sizeof(guiState.keypadBuffer), "%d", currentScript.devices[deviceIdx].offTime);
        guiState.keypadPos = strlen(guiState.keypadBuffer);
        guiState.currentMode = MODE_KEYPAD;
        drawKeypadPanel();
      }
      else if (deviceFields[i].fieldType == 2) {
        currentScript.devices[deviceIdx].enabled = !currentScript.devices[deviceIdx].enabled;
        drawEditPage();
      }
      return;
    }
  }
}

void handleTouchScriptLoad(int16_t x, int16_t y) {
  if (x < 80 && y < 40) {
    guiState.currentMode = guiState.previousMode;
    guiState.selectedScript = -1;
    guiState.highlightedScript = -1;
    guiState.showDeleteConfirm = false;
    if (guiState.previousMode == MODE_SCRIPT) {
      drawScriptPage();
    } else {
      drawEditPage();
    }
    return;
  }

  if (touchInButton(x, y, btnSortDropdown)) {
    currentSortMode = (SortMode)((currentSortMode + 1) % 3);
    saveSettingsToEEPROM();
    sortScripts();
    guiState.selectedScript = -1;
    guiState.highlightedScript = -1;
    guiState.scriptListOffset = 0;
    drawScriptLoadPage();
    return;
  }

  if (guiState.selectedScript >= 0 && touchInButton(x, y, btnScriptSelect)) {
    loadScriptFromFile(scriptList[guiState.selectedScript].filename);

    guiState.currentMode = guiState.previousMode;
    guiState.selectedScript = -1;
    guiState.highlightedScript = -1;
    if (guiState.previousMode == MODE_SCRIPT) {
      drawScriptPage();
    } else {
      drawEditPage();
    }
    return;
  }

  if (guiState.selectedScript >= 0 && touchInButton(x, y, btnScriptDelete)) {
    strcpy(guiState.deleteScriptName, scriptList[guiState.selectedScript].name);
    guiState.showDeleteConfirm = true;
    guiState.currentMode = MODE_DELETE_CONFIRM;
    drawDeleteConfirmDialog();
    return;
  }

  // Handle script list selection
  int yOffset = 60;
  int lineHeight = 22;
  int visibleScripts = min(numScripts - guiState.scriptListOffset, 10);

  for (int i = 0; i < visibleScripts; i++) {
    int yPos = yOffset + i * lineHeight;
    if (y >= yPos && y < (yPos + lineHeight)) {
      int scriptIdx = guiState.scriptListOffset + i;
      if (scriptIdx < numScripts) {
        guiState.highlightedScript = scriptIdx;
        guiState.selectedScript = scriptIdx;
        drawScriptLoadPage();
        return;
      }
    }
  }

  // Handle scroll controls
  if (numScripts > 10) {
    if (x >= 440 && x <= 470 && y >= 60 && y <= 90 && guiState.scriptListOffset > 0) {
      guiState.scriptListOffset--;
      if (guiState.selectedScript >= 0 && guiState.selectedScript < guiState.scriptListOffset) {
        guiState.selectedScript = -1;
        guiState.highlightedScript = -1;
      }
      drawScriptLoadPage();
      return;
    }

    if (x >= 440 && x <= 470 && y >= 230 && y <= 260 &&
        guiState.scriptListOffset < (numScripts - 10)) {
      guiState.scriptListOffset++;
      if (guiState.selectedScript >= 0 && guiState.selectedScript >= guiState.scriptListOffset + 10) {
        guiState.selectedScript = -1;
        guiState.highlightedScript = -1;
      }
      drawScriptLoadPage();
      return;
    }
  }
}

void handleTouchDeleteConfirm(int16_t x, int16_t y) {
  if (touchInButton(x, y, btnDeleteYes)) {
    if (guiState.selectedScript >= 0) {
      deleteScript(scriptList[guiState.selectedScript].filename);
      guiState.selectedScript = -1;
      guiState.highlightedScript = -1;
    }
    guiState.showDeleteConfirm = false;
    guiState.currentMode = (guiState.previousMode == MODE_SCRIPT) ? MODE_SCRIPT_LOAD : MODE_EDIT_LOAD;
    drawScriptLoadPage();
    return;
  }

  if (touchInButton(x, y, btnDeleteNo)) {
    guiState.showDeleteConfirm = false;
    guiState.currentMode = (guiState.previousMode == MODE_SCRIPT) ? MODE_SCRIPT_LOAD : MODE_EDIT_LOAD;
    drawScriptLoadPage();
    return;
  }
}

void handleTouchEditField(int16_t x, int16_t y) {
  if (touchInButton(x, y, btnEditFieldBack)) {
    guiState.currentMode = MODE_EDIT;
    drawEditPage();
    return;
  }
  guiState.currentMode = MODE_EDIT;
  drawEditPage();
}

void handleTouchEditSave(int16_t x, int16_t y) {
  if (touchInButton(x, y, btnEditSaveBack)) {
    guiState.currentMode = MODE_EDIT;
    drawEditPage();
    return;
  }
}

void handleTouchEditName(int16_t x, int16_t y) {
  if (touchInButton(x, y, btnEditNameBack)) {
    guiState.currentMode = MODE_EDIT;
    drawEditPage();
    return;
  }
}

void handleTouchAbout(int16_t x, int16_t y) {
  if (touchInButton(x, y, btnAboutBack)) {
    guiState.currentMode = MODE_SETTINGS;
    drawSettingsPanel();
    return;
  }
  if (touchInButton(x, y, btnAboutStop)) {
    if (!systemState.safetyStop) {
      systemState.lockBeforeStop = systemState.lock;
      systemState.safetyStop = true;
      setAllOutputsOff();
      drawButton(btnAboutStop, COLOR_PURPLE, COLOR_BLACK, "RELEASE",
                 false, btnAboutStop.enabled);
      if (isScriptRunning) {
        stopScript(true);
      }
      if (systemState.recording) {
        stopRecording();
      }
    } else {
      systemState.safetyStop = false;
      bool prevLock = systemState.lock;
      systemState.lock = systemState.lockBeforeStop;
      drawButton(btnAboutStop, COLOR_YELLOW, COLOR_BLACK, "STOP",
                 false, btnAboutStop.enabled);
      if (!systemState.lock && prevLock) syncOutputsToSwitches();
    }
    return;
  }
}

void handleTouchDateTime(int16_t x, int16_t y) {
  if (x < 80 && y < 40) {
    guiState.currentMode = MODE_SETTINGS;
    drawSettingsPanel();
    return;
  }

  if (x > 400 && y < 40) {
    setDateTime(tmSet);
    guiState.currentMode = MODE_SETTINGS;
    drawSettingsPanel();
    return;
  }

  int fieldX = 180;
  int fieldWidth = 60;
  int fieldHeight = 30;

  // Handle field adjustments
  struct DateTimeField {
    int minY, maxY;
    void (*increment)();
    void (*decrement)();
  };

  DateTimeField fields[] = {
    {70, 70+fieldHeight, [](){tmSet.Year = constrain(tmSet.Year + 1, 25, 99);}, [](){tmSet.Year = constrain(tmSet.Year - 1, 25, 99);}},
    {110, 110+fieldHeight, [](){tmSet.Month = tmSet.Month % 12 + 1;}, [](){tmSet.Month = (tmSet.Month + 10) % 12 + 1;}},
    {150, 150+fieldHeight, [](){tmSet.Day = tmSet.Day % 31 + 1;}, [](){tmSet.Day = (tmSet.Day + 29) % 31 + 1;}},
    {190, 190+fieldHeight, [](){tmSet.Hour = (tmSet.Hour + 1) % 24;}, [](){tmSet.Hour = (tmSet.Hour + 23) % 24;}},
    {230, 230+fieldHeight, [](){tmSet.Minute = (tmSet.Minute + 1) % 60;}, [](){tmSet.Minute = (tmSet.Minute + 59) % 60;}},
    {270, 270+fieldHeight, [](){tmSet.Second = (tmSet.Second + 1) % 60;}, [](){tmSet.Second = (tmSet.Second + 59) % 60;}}
  };

  for (int i = 0; i < 6; i++) {
    if (y >= fields[i].minY && y <= fields[i].maxY) {
      if (x >= fieldX && x <= (fieldX + fieldWidth)) {
        fields[i].increment();
        drawDateTimePanel();
        return;
      }
      if (x >= (fieldX - 30) && x <= fieldX) {
        fields[i].decrement();
        drawDateTimePanel();
        return;
      }
    }
  }
}

void handleTouchKeypad(int16_t x, int16_t y) {
  if (touchInButton(x, y, btnKeypadBack)) {
    if (guiState.keypadMode == KEYPAD_UPDATE_RATE || guiState.keypadMode == KEYPAD_FAN_SPEED) {
      guiState.currentMode = MODE_SETTINGS;
      drawSettingsPanel();
    } else if (guiState.keypadMode == KEYPAD_NETWORK_IP || guiState.keypadMode == KEYPAD_NETWORK_PORT || guiState.keypadMode == KEYPAD_NETWORK_TIMEOUT) {
      guiState.currentMode = MODE_NETWORK_EDIT;
      drawNetworkEditPanel();
    } else if (guiState.keypadMode >= KEYPAD_GRAPH_MIN_Y && guiState.keypadMode <= KEYPAD_GRAPH_TIME_RANGE) {
      guiState.currentMode = MODE_GRAPH_SETTINGS;
      drawGraphSettingsPage();
    } else if (guiState.keypadMode == KEYPAD_GRAPH_MAX_POINTS || guiState.keypadMode == KEYPAD_GRAPH_REFRESH_RATE) {
      guiState.currentMode = MODE_GRAPH_DISPLAY;
      drawGraphDisplaySettingsPage();
    } else {
      guiState.currentMode = MODE_EDIT;
      drawEditPage();
    }
    guiState.keypadMode = KEYPAD_NONE;
    return;
  }
}

void handleUniversalBackButton() {
  switch(guiState.currentMode) {
    case MODE_SETTINGS:
      guiState.currentMode = MODE_MAIN;
      drawMainScreen();
      break;
    case MODE_NETWORK:
      guiState.currentMode = MODE_SETTINGS;
      drawSettingsPanel();
      break;
    case MODE_SCRIPT:
      guiState.currentMode = MODE_MAIN;
      drawMainScreen();
      break;
    case MODE_EDIT:
      guiState.currentMode = MODE_MAIN;
      drawMainScreen();
      break;
    case MODE_SCRIPT_LOAD:
    case MODE_EDIT_LOAD:
      guiState.currentMode = guiState.previousMode;
      if (guiState.previousMode == MODE_SCRIPT) {
        drawScriptPage();
      } else {
        drawEditPage();
      }
      break;
    case MODE_DATE_TIME:
      guiState.currentMode = MODE_SETTINGS;
      drawSettingsPanel();
      break;
    case MODE_DELETE_CONFIRM:
      guiState.currentMode = (guiState.previousMode == MODE_SCRIPT) ? MODE_SCRIPT_LOAD : MODE_EDIT_LOAD;
      drawScriptLoadPage();
      break;
    case MODE_ABOUT:
      guiState.currentMode = MODE_SETTINGS;
      drawSettingsPanel();
      break;
    default:
      break;
  }
}
