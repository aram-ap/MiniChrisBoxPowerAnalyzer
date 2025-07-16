/**
 * @file utils.cpp
 * @brief General utility functions implementation
 */

#include "types.h"
#include "utils.h"
#include "config.h"
#include "display.h"
#include "switches.h"
#include "script.h"
#include "settings.h"
#include "network_wrapper.h"
#include "network.h"  // Add this include for network functions
#include "time_utils.h"
#include "touch.h"
#include "graphs.h"
#include <Keypad.h>

// Keypad object - use extern arrays from config.h
extern byte ROW_PINS[4];
extern byte COL_PINS[4];
Keypad keypad = Keypad(makeKeymap(KEYPAD_KEYS), ROW_PINS, COL_PINS, 4, 4);

// External references
extern SystemState systemState;
extern GUIState guiState;
extern NetworkConfig networkConfig;

void handleKeypadInput() {
  char key = keypad.getKey();

  if (key == 'B') {
    // Special handling for graph modes
    if (guiState.currentMode == MODE_GRAPH) {
      guiState.currentMode = MODE_MAIN;
      drawMainScreen();
      return;
    }
    if (guiState.currentMode == MODE_GRAPH_SETTINGS) {
      guiState.currentMode = MODE_GRAPH;
      drawGraphPage();
      return;
    }
    if (guiState.currentMode == MODE_GRAPH_DISPLAY) {
      guiState.currentMode = MODE_GRAPH_SETTINGS;
      drawGraphSettingsPage();
      return;
    }

    if (guiState.currentMode != MODE_KEYPAD &&
        guiState.currentMode != MODE_EDIT_SAVE &&
        guiState.currentMode != MODE_EDIT_NAME &&
        guiState.currentMode != MODE_NETWORK_EDIT) {
      handleUniversalBackButton();
    } else {
      handleKeypadInputChar(key);
    }
  }

  // Handle graph mode keypad input
  if (guiState.currentMode == MODE_GRAPH) {
    if (key >= '0' && key <= '6') {
      GraphTab newTab = (GraphTab)(key - '0');
      switchGraphTab(newTab);
      return;
    }

    if (key == '#') {
      // Cycle through data types
      cycleAllGraphDataType();
      return;
    }
  }

  if (guiState.currentMode == MODE_SCRIPT_LOAD) {
    if (key >= '1' && key <= '9') {
      int scriptNum = key - '0';
      if (scriptNum <= numScripts) {
        guiState.selectedScript = scriptNum - 1;
        guiState.highlightedScript = scriptNum - 1;
        guiState.scriptListOffset = max(0, (scriptNum - 1) - 5);
        drawScriptLoadPage();
        return;
      }
    }

    if (key == 'A' && guiState.selectedScript >= 0) {
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
  }

  if (key) {
    handleKeypadInputChar(key);
  }
}

void handleKeypadInputChar(char key) {
  if (guiState.currentMode == MODE_EDIT_SAVE || guiState.currentMode == MODE_EDIT_NAME) {
    // T9 text input handling
    if (key >= '0' && key <= '9') {
      if (!guiState.alphaMode) {
        // Number mode
        if (guiState.keypadPos < 31) {
          guiState.keypadBuffer[guiState.keypadPos++] = key;
          guiState.keypadBuffer[guiState.keypadPos] = '\0';
        }
      } else {
        // Alpha mode - T9 input
        int digit = key - '0';
        const char* letters = T9_LETTERS[digit];
        int letterCount = strlen(letters);

        if (letterCount > 0) {
          unsigned long currentTime = millis();

          if (guiState.lastKey == key && (currentTime - guiState.lastKeyTime) < 1000) {
            // Same key pressed within timeout - cycle through letters
            guiState.currentLetterIndex = (guiState.currentLetterIndex + 1) % letterCount;
            if (guiState.keypadPos > 0) {
              guiState.keypadPos--;
            }
          } else {
            // New key or timeout - start with first letter
            guiState.currentLetterIndex = 0;
          }

          if (guiState.keypadPos < 31) {
            char letter = letters[guiState.currentLetterIndex];

            // Apply case modifications
            if (guiState.capsMode || (guiState.shiftMode && guiState.keypadPos == 0)) {
              letter = toupper(letter);
            }

            guiState.keypadBuffer[guiState.keypadPos++] = letter;
            guiState.keypadBuffer[guiState.keypadPos] = '\0';

            guiState.lastKey = key;
            guiState.lastKeyTime = currentTime;

            // Reset shift mode after one character
            if (guiState.shiftMode && !guiState.capsMode) {
              guiState.shiftMode = false;
            }
          }
        }
      }
      drawEditSavePage();
      return;
    }

    switch (key) {
      case 'A':  // Save
        strncpy(currentScript.scriptName, guiState.keypadBuffer, 31);
        currentScript.scriptName[31] = '\0';

        if (guiState.currentMode == MODE_EDIT_SAVE) {
          saveCurrentScript();
        }
        guiState.currentMode = MODE_EDIT;
        drawEditPage();
        break;

      case 'C':  // Shift
        guiState.shiftMode = !guiState.shiftMode;
        drawEditSavePage();
        break;

      case 'D':  // Caps Lock
        guiState.capsMode = !guiState.capsMode;
        drawEditSavePage();
        break;

      case '#':  // Toggle alpha/numeric
        guiState.alphaMode = !guiState.alphaMode;
        drawEditSavePage();
        break;

      case '*':  // Backspace
        if (guiState.keypadPos > 0) {
          guiState.keypadPos--;
          guiState.keypadBuffer[guiState.keypadPos] = '\0';
        }
        drawEditSavePage();
        break;
    }
    return;
  }

  // Regular keypad input for numeric fields
  if (guiState.currentMode == MODE_KEYPAD) {
    DeviceTimingField* deviceFields = getDeviceFields();
    EditField* editFields = getEditFields();

    bool validKey = false;

    if (key >= '0' && key <= '9') {
      if (guiState.keypadPos < 31) {
        guiState.keypadBuffer[guiState.keypadPos++] = key;
        guiState.keypadBuffer[guiState.keypadPos] = 0;
        validKey = true;
      }
    }
    else if (key == '*') {  // Backspace
      if (guiState.keypadPos > 0) {
        guiState.keypadPos--;
        guiState.keypadBuffer[guiState.keypadPos] = 0;
        validKey = true;
      }
    }
    else if (key == 'D') {
      if (guiState.keypadMode >= KEYPAD_GRAPH_MIN_Y && guiState.keypadMode <= KEYPAD_GRAPH_TIME_RANGE) {
        if (guiState.keypadPos < 31 && !strchr(guiState.keypadBuffer, '.')) {
          guiState.keypadBuffer[guiState.keypadPos++] = '.';
          guiState.keypadBuffer[guiState.keypadPos] = 0;
          validKey = true;
        }
      }
      if (guiState.keypadMode == KEYPAD_NETWORK_IP) {
        if (guiState.keypadPos < 31 && !strchr(guiState.keypadBuffer, '.')) {
          guiState.keypadBuffer[guiState.keypadPos++] = '.';
          guiState.keypadBuffer[guiState.keypadPos] = 0;
          validKey = true;
        }
      }
    }
    else if (key == '#') {  // Toggle sign (if applicable)
      if (guiState.keypadMode == KEYPAD_SCRIPT_TSTART ||
          guiState.keypadMode == KEYPAD_GRAPH_MIN_Y ||
          guiState.keypadMode == KEYPAD_GRAPH_MAX_Y) {
        if (guiState.keypadBuffer[0] == '-') {
          memmove(guiState.keypadBuffer, guiState.keypadBuffer + 1, guiState.keypadPos);
          guiState.keypadPos--;
        } else if (guiState.keypadPos < 31) {
          memmove(guiState.keypadBuffer + 1, guiState.keypadBuffer, guiState.keypadPos + 1);
          guiState.keypadBuffer[0] = '-';
          guiState.keypadPos++;
        }
        validKey = true;
      }
    }
    else if (key == 'A') {  // Enter
      switch (guiState.keypadMode) {
        case KEYPAD_UPDATE_RATE:
          systemState.updateRate = constrain(atoi(guiState.keypadBuffer), 10, 5000);
          saveSettingsToEEPROM();
          applyUpdateRate();
          guiState.currentMode = MODE_SETTINGS;
          drawSettingsPanel();
          break;

        case KEYPAD_FAN_SPEED:
          systemState.fanSpeed = constrain(atoi(guiState.keypadBuffer), 0, 255);
          saveSettingsToEEPROM();
          applyFanSettings();
          guiState.currentMode = MODE_SETTINGS;
          drawSettingsPanel();
          break;

        case KEYPAD_SCRIPT_TSTART:
          currentScript.tStart = atoi(guiState.keypadBuffer);
          strcpy(editFields[0].value, guiState.keypadBuffer);
          guiState.currentMode = MODE_EDIT;
          drawEditPage();
          break;

        case KEYPAD_SCRIPT_TEND:
          currentScript.tEnd = atoi(guiState.keypadBuffer);
          strcpy(editFields[1].value, guiState.keypadBuffer);
          guiState.currentMode = MODE_EDIT;
          drawEditPage();
          break;

        case KEYPAD_DEVICE_ON_TIME:
          if (guiState.selectedDeviceField >= 0) {
            int deviceIdx = deviceFields[guiState.selectedDeviceField].deviceIndex;
            currentScript.devices[deviceIdx].onTime = atoi(guiState.keypadBuffer);
          }
          guiState.currentMode = MODE_EDIT;
          drawEditPage();
          break;

        case KEYPAD_DEVICE_OFF_TIME:
          if (guiState.selectedDeviceField >= 0) {
            int deviceIdx = deviceFields[guiState.selectedDeviceField].deviceIndex;
            currentScript.devices[deviceIdx].offTime = atoi(guiState.keypadBuffer);
          }
          guiState.currentMode = MODE_EDIT;
          drawEditPage();
          break;

        case KEYPAD_SCRIPT_SEARCH:
          {
            int scriptNum = atoi(guiState.keypadBuffer);
            if (scriptNum > 0 && scriptNum <= numScripts) {
              guiState.selectedScript = scriptNum - 1;
              guiState.highlightedScript = scriptNum - 1;
              guiState.scriptListOffset = max(0, (scriptNum - 1) - 5);
              drawScriptLoadPage();
            }
          }
          break;

        case KEYPAD_NETWORK_IP:
        case KEYPAD_NETWORK_PORT:
        case KEYPAD_NETWORK_TIMEOUT:
          if (guiState.selectedNetworkField >= 0) {
            // Save the network field value
            extern void saveNetworkFieldToConfig(int fieldIndex, const char* value);
            saveNetworkFieldToConfig(guiState.selectedNetworkField, guiState.keypadBuffer);
          }
          guiState.currentMode = MODE_NETWORK_EDIT;
          drawNetworkEditPanel();
          break;

        case KEYPAD_GRAPH_MIN_Y:
          {
            float value = atof(guiState.keypadBuffer);
            if (guiState.currentGraphTab == GRAPH_TAB_ALL) {
              setGraphAxisBounds(GRAPH_TAB_ALL, graphSettings.all.dataType,
                               value, graphSettings.all.axisRanges[graphSettings.all.dataType][1]);
            } else {
              int deviceIndex = (int)guiState.currentGraphTab - 1;
              GraphDataType dataType = graphSettings.devices[deviceIndex].dataType;
              setGraphAxisBounds(guiState.currentGraphTab, dataType,
                               value, graphSettings.devices[deviceIndex].axisRanges[dataType][1]);
            }
            guiState.currentMode = MODE_GRAPH_SETTINGS;
            drawGraphSettingsPage();
          }
          break;

        case KEYPAD_GRAPH_MAX_Y:
          {
            float value = atof(guiState.keypadBuffer);
            if (guiState.currentGraphTab == GRAPH_TAB_ALL) {
              setGraphAxisBounds(GRAPH_TAB_ALL, graphSettings.all.dataType,
                               graphSettings.all.axisRanges[graphSettings.all.dataType][0], value);
            } else {
              int deviceIndex = (int)guiState.currentGraphTab - 1;
              GraphDataType dataType = graphSettings.devices[deviceIndex].dataType;
              setGraphAxisBounds(guiState.currentGraphTab, dataType,
                               graphSettings.devices[deviceIndex].axisRanges[dataType][0], value);
            }
            guiState.currentMode = MODE_GRAPH_SETTINGS;
            drawGraphSettingsPage();
          }
          break;

        case KEYPAD_GRAPH_TIME_RANGE:
          graphSettings.timeRange = constrain(atof(guiState.keypadBuffer), 0.01f, 300.0f);
          saveGraphSettings();
          guiState.currentMode = MODE_GRAPH_SETTINGS;
          drawGraphSettingsPage();
          break;

        // ADDED: New keypad modes
        case KEYPAD_GRAPH_MAX_POINTS:
          graphSettings.effectiveMaxPoints = constrain(atoi(guiState.keypadBuffer), 10, GRAPH_MAX_POINTS);
          saveGraphSettings();
          guiState.currentMode = MODE_GRAPH_DISPLAY;
          drawGraphDisplaySettingsPage();
          break;

        case KEYPAD_GRAPH_REFRESH_RATE:
          graphSettings.graphRefreshRate = constrain(atoi(guiState.keypadBuffer), 20, 200);
          saveGraphSettings();
          guiState.currentMode = MODE_GRAPH_DISPLAY;
          drawGraphDisplaySettingsPage();
          break;

        case KEYPAD_NONE:
        case KEYPAD_SCRIPT_NAME:
          // These cases don't need handling here
          break;
      }
      guiState.keypadMode = KEYPAD_NONE;
      return;
    } else if (key == 'C') {
      // Clear
      guiState.keypadBuffer[0] = 0;
      guiState.keypadPos = 0;
      validKey = true;
    }
    else if (key == 'B') {
      if (guiState.keypadMode == KEYPAD_GRAPH_MIN_Y ||
          guiState.keypadMode == KEYPAD_GRAPH_MAX_Y ||
          guiState.keypadMode == KEYPAD_GRAPH_TIME_RANGE) {
        guiState.keypadMode = KEYPAD_NONE; // Clear mode first
        guiState.currentMode = MODE_GRAPH_SETTINGS;
        drawGraphSettingsPage();
        return; // Exit immediately
      } else if (guiState.keypadMode == KEYPAD_GRAPH_MAX_POINTS ||
                 guiState.keypadMode == KEYPAD_GRAPH_REFRESH_RATE) {
        guiState.keypadMode = KEYPAD_NONE; // Clear mode first
        guiState.currentMode = MODE_GRAPH_DISPLAY;
        drawGraphDisplaySettingsPage();
        return; // Exit immediately
      } else if (guiState.keypadMode == KEYPAD_UPDATE_RATE ||
                 guiState.keypadMode == KEYPAD_FAN_SPEED) {
        guiState.keypadMode = KEYPAD_NONE;
        guiState.currentMode = MODE_SETTINGS;
        drawSettingsPanel();
        return;
      } else if (guiState.keypadMode == KEYPAD_NETWORK_IP ||
                 guiState.keypadMode == KEYPAD_NETWORK_PORT ||
                 guiState.keypadMode == KEYPAD_NETWORK_TIMEOUT) {
        guiState.keypadMode = KEYPAD_NONE;
        guiState.currentMode = MODE_NETWORK_EDIT;
        drawNetworkEditPanel();
        return;
      } else {
        guiState.keypadMode = KEYPAD_NONE;
        guiState.currentMode = MODE_EDIT;
        drawEditPage();
        return;
      }
    }

    if (validKey) {
      drawKeypadPanel();
    }
  }
}
