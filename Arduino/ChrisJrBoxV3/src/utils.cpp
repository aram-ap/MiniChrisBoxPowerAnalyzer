/**
* @file utils.cpp
 * @brief General utility functions implementation
 */

// Order is important - types.h must come before Arduino-dependent includes
#include "types.h"
#include "config.h"

// Include Keypad before network to establish its macros
#include <Keypad.h>

// Then include other headers
#include "utils.h"
#include "display.h"
#include "switches.h"
#include "script.h"
#include "settings.h"
#include "network.h"
#include "time_utils.h"
#include "touch.h"

// External references
extern SystemState systemState;
extern GUIState guiState;
extern NetworkConfig networkConfig;

// Keypad object
Keypad keypad = Keypad(makeKeymap(KEYPAD_KEYS), (byte*)ROW_PINS, (byte*)COL_PINS, 4, 4);

// T9 timeout
const unsigned long T9_TIMEOUT = 300;

void handleKeypadInput() {
  char key = keypad.getKey();

  if (key == 'B') {
    if (guiState.currentMode != MODE_KEYPAD &&
        guiState.currentMode != MODE_EDIT_SAVE &&
        guiState.currentMode != MODE_EDIT_NAME &&
        guiState.currentMode != MODE_NETWORK_EDIT) {
      handleUniversalBackButton();
    } else {
      handleKeypadInputChar(key);
    }
  }

  // Handle script selection shortcuts
  if (guiState.currentMode == MODE_SCRIPT_LOAD && key >= '1' && key <= '9') {
    int scriptNum = key - '0';
    if (scriptNum <= numScripts) {
      guiState.selectedScript = scriptNum - 1;
      guiState.highlightedScript = scriptNum - 1;
      guiState.scriptListOffset = max(0, (scriptNum - 1) - 5);
      drawScriptLoadPage();
      return;
    }
  }

  // Handle script load confirmation
  if (guiState.currentMode == MODE_SCRIPT_LOAD && key == 'A' && guiState.selectedScript >= 0) {
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

  // Handle keypad input for text modes
  if (guiState.currentMode == MODE_KEYPAD ||
      guiState.currentMode == MODE_EDIT_SAVE ||
      guiState.currentMode == MODE_EDIT_NAME ||
      guiState.currentMode == MODE_NETWORK_EDIT) {
    if (key && key != 'B') {
      handleKeypadInputChar(key);
    }
  }
}

void handleKeypadInputChar(char key) {
  if (guiState.currentMode == MODE_KEYPAD) {
    if (guiState.keypadMode == KEYPAD_DEVICE_ON_TIME || guiState.keypadMode == KEYPAD_DEVICE_OFF_TIME ||
        guiState.keypadMode == KEYPAD_SCRIPT_TSTART || guiState.keypadMode == KEYPAD_SCRIPT_TEND ||
        guiState.keypadMode == KEYPAD_UPDATE_RATE || guiState.keypadMode == KEYPAD_FAN_SPEED ||
        guiState.keypadMode == KEYPAD_NETWORK_IP || guiState.keypadMode == KEYPAD_NETWORK_PORT ||
        guiState.keypadMode == KEYPAD_NETWORK_TIMEOUT) {

      if (key >= '0' && key <= '9') {
        if (guiState.keypadPos < (int)sizeof(guiState.keypadBuffer) - 1) {
          guiState.keypadBuffer[guiState.keypadPos++] = key;
          guiState.keypadBuffer[guiState.keypadPos] = 0;
        }
      }
      else if (key == '.' && guiState.keypadMode == KEYPAD_NETWORK_IP) {
        if (guiState.keypadPos < (int)sizeof(guiState.keypadBuffer) - 1) {
          guiState.keypadBuffer[guiState.keypadPos++] = key;
          guiState.keypadBuffer[guiState.keypadPos] = 0;
        }
      }
      else if (key == '*') {
        if (guiState.keypadPos > 0) {
          guiState.keypadBuffer[--guiState.keypadPos] = 0;
        }
      }
      else if (key == '#') {
        if (guiState.keypadMode != KEYPAD_NETWORK_IP) {
          if (guiState.keypadBuffer[0] == '-') {
            memmove(guiState.keypadBuffer, guiState.keypadBuffer + 1, guiState.keypadPos);
            guiState.keypadPos--;
          } else {
            memmove(guiState.keypadBuffer + 1, guiState.keypadBuffer, guiState.keypadPos + 1);
            guiState.keypadBuffer[0] = '-';
            guiState.keypadPos++;
          }
        }
      }
      else if (key == 'A') {
        DeviceTimingField* deviceFields = getDeviceFields();
        NetworkEditField* networkFields = getNetworkFields();

        switch (guiState.keypadMode) {
          case KEYPAD_UPDATE_RATE: {
            unsigned long val = atol(guiState.keypadBuffer);
            if (val < 10) val = 10;
            if (val > 5000) val = 5000;
            systemState.updateRate = val;
            saveSettingsToEEPROM();
            applyUpdateRate();
            guiState.currentMode = MODE_SETTINGS;
            guiState.keypadMode = KEYPAD_NONE;
            drawSettingsPanel();
            break;
          }
          case KEYPAD_FAN_SPEED: {
            int val = atoi(guiState.keypadBuffer);
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            systemState.fanSpeed = val;
            systemState.fanOn = (val > 0);
            saveSettingsToEEPROM();
            applyFanSettings();
            guiState.currentMode = MODE_SETTINGS;
            guiState.keypadMode = KEYPAD_NONE;
            drawSettingsPanel();
            break;
          }
          case KEYPAD_SCRIPT_TSTART: {
            int val = atoi(guiState.keypadBuffer);
            currentScript.tStart = val;
            guiState.currentMode = MODE_EDIT;
            guiState.keypadMode = KEYPAD_NONE;
            drawEditPage();
            break;
          }
          case KEYPAD_SCRIPT_TEND: {
            int val = atoi(guiState.keypadBuffer);
            if (val <= currentScript.tStart) val = currentScript.tStart + 10;
            currentScript.tEnd = val;
            guiState.currentMode = MODE_EDIT;
            guiState.keypadMode = KEYPAD_NONE;
            drawEditPage();
            break;
          }
          case KEYPAD_DEVICE_ON_TIME: {
            int val = atoi(guiState.keypadBuffer);
            if (guiState.selectedDeviceField >= 0) {
              int deviceIdx = deviceFields[guiState.selectedDeviceField].deviceIndex;
              currentScript.devices[deviceIdx].onTime = val;
            }
            guiState.currentMode = MODE_EDIT;
            guiState.keypadMode = KEYPAD_NONE;
            drawEditPage();
            break;
          }
          case KEYPAD_DEVICE_OFF_TIME: {
            int val = atoi(guiState.keypadBuffer);
            if (guiState.selectedDeviceField >= 0) {
              int deviceIdx = deviceFields[guiState.selectedDeviceField].deviceIndex;
              currentScript.devices[deviceIdx].offTime = val;
            }
            guiState.currentMode = MODE_EDIT;
            guiState.keypadMode = KEYPAD_NONE;
            drawEditPage();
            break;
          }
          case KEYPAD_NETWORK_IP:
          case KEYPAD_NETWORK_PORT:
          case KEYPAD_NETWORK_TIMEOUT: {
            if (guiState.selectedNetworkField >= 0) {
              saveNetworkFieldToConfig(guiState.selectedNetworkField, guiState.keypadBuffer);
              strcpy(networkFields[guiState.selectedNetworkField].value, guiState.keypadBuffer);
            }
            guiState.currentMode = MODE_NETWORK_EDIT;
            guiState.keypadMode = KEYPAD_NONE;
            drawNetworkEditPanel();
            break;
          }
          default:
            break;
        }
        return;
      }
      else if (key == 'B') {
        if (guiState.keypadMode == KEYPAD_UPDATE_RATE || guiState.keypadMode == KEYPAD_FAN_SPEED) {
          guiState.currentMode = MODE_SETTINGS;
          drawSettingsPanel();
        } else if (guiState.keypadMode == KEYPAD_NETWORK_IP || guiState.keypadMode == KEYPAD_NETWORK_PORT || guiState.keypadMode == KEYPAD_NETWORK_TIMEOUT) {
          guiState.currentMode = MODE_NETWORK_EDIT;
          drawNetworkEditPanel();
        } else {
          guiState.currentMode = MODE_EDIT;
          drawEditPage();
        }
        guiState.keypadMode = KEYPAD_NONE;
        return;
      }
      else if (key == 'C') {
        guiState.keypadPos = 0;
        guiState.keypadBuffer[0] = 0;
      }

      drawKeypadPanel();
    }
  }
  else if (guiState.currentMode == MODE_EDIT_SAVE || guiState.currentMode == MODE_EDIT_NAME) {
    if (guiState.alphaMode) {
      if (key >= '1' && key <= '9') {
        unsigned long currentTime = millis();

        if (key == guiState.lastKey && (currentTime - guiState.lastKeyTime) < T9_TIMEOUT) {
          const char* letters = T9_LETTERS[key - '0'];
          int numLetters = strlen(letters);

          if (numLetters > 0) {
            guiState.currentLetterIndex = (guiState.currentLetterIndex + 1) % numLetters;

            if (guiState.keypadPos > 0) {
              char newChar = letters[guiState.currentLetterIndex];
              if (guiState.capsMode || guiState.shiftMode) {
                newChar = toupper(newChar);
              }
              guiState.keypadBuffer[guiState.keypadPos - 1] = newChar;
            }
          }
        } else {
          const char* letters = T9_LETTERS[key - '0'];
          if (strlen(letters) > 0 && guiState.keypadPos < (int)sizeof(guiState.keypadBuffer) - 1) {
            guiState.currentLetterIndex = 0;
            char newChar = letters[guiState.currentLetterIndex];
            if (guiState.capsMode || guiState.shiftMode) {
              newChar = toupper(newChar);
            }
            guiState.keypadBuffer[guiState.keypadPos++] = newChar;
            guiState.keypadBuffer[guiState.keypadPos] = '\0';
          }
        }

        guiState.lastKey = key;
        guiState.lastKeyTime = currentTime;

        if (guiState.shiftMode) guiState.shiftMode = false;
      }
      else if (key == '0') {
        const char* chars = T9_LETTERS[0];
        if (guiState.keypadPos < (int)sizeof(guiState.keypadBuffer) - 1) {
          guiState.keypadBuffer[guiState.keypadPos++] = chars[0];
          guiState.keypadBuffer[guiState.keypadPos] = '\0';
        }
        guiState.lastKey = '\0';
      }
    } else {
      if (key >= '0' && key <= '9') {
        if (guiState.keypadPos < (int)sizeof(guiState.keypadBuffer) - 1) {
          guiState.keypadBuffer[guiState.keypadPos++] = key;
          guiState.keypadBuffer[guiState.keypadPos] = '\0';
        }
        guiState.lastKey = '\0';
      }
    }

    if (key == '#') {
      guiState.alphaMode = !guiState.alphaMode;
      guiState.lastKey = '\0';
    }
    else if (key == 'A') {
      strncpy(currentScript.scriptName, guiState.keypadBuffer, sizeof(currentScript.scriptName) - 1);
      currentScript.scriptName[sizeof(currentScript.scriptName) - 1] = '\0';
      if (guiState.currentMode == MODE_EDIT_SAVE) {
        saveCurrentScript();
      }
      guiState.currentMode = MODE_EDIT;
      drawEditPage();
      return;
    }
    else if (key == 'B') {
      guiState.currentMode = MODE_EDIT;
      drawEditPage();
      return;
    }
    else if (key == 'C') {
      guiState.shiftMode = !guiState.shiftMode;
      guiState.lastKey = '\0';
    }
    else if (key == 'D') {
      guiState.capsMode = !guiState.capsMode;
      guiState.lastKey = '\0';
    }
    else if (key == '*') {
      if (guiState.keypadPos > 0) {
        guiState.keypadBuffer[--guiState.keypadPos] = '\0';
      }
      guiState.lastKey = '\0';
    }

    drawEditSavePage();
  }
}
