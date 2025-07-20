/**
 * @file datalog.cpp
 * @brief Data logging and SD card operations implementation
 */

#include "datalog.h"
#include "config.h"
#include "switches.h"
#include "sensors.h"
#include "script.h"
#include "time_utils.h"
#include "display.h"
#include <ArduinoJson.h>
#include <rgb565_colors.h>

// External references
extern SystemState systemState;
extern GUIState guiState;

// File handle
File logFile;

// Private variables
static bool currentSDContext = false; // false = external, true = internal

void initDataLogging() {
  smartCheckSDCard();
  checkInternalSD();
}

void checkSDCardStatus(unsigned long currentMillis) {
  if (!systemState.recording && (currentMillis - systemState.lastSDCheck > SD_CHECK_INTERVAL)) {
    smartCheckSDCard();
    checkInternalSD();
    systemState.lastSDCheck = currentMillis;
  }
}

void ensureExternalSDContext() {
  if (currentSDContext != false) {
    SD.begin(SD_CS);
    currentSDContext = false;
  }
}

void ensureInternalSDContext() {
  if (currentSDContext != true) {
    SD.begin(BUILTIN_SDCARD);
    currentSDContext = true;
  }
}

void smartCheckSDCard() {
  ensureExternalSDContext();

  bool nowAvailable = false;
  if (SD.begin(SD_CS)) {
    File root = SD.open("/");
    if (root) {
      nowAvailable = true;
      root.close();
    }
  }

  systemState.sdAvailable = nowAvailable;
  btnRecord.enabled = systemState.sdAvailable && !isScriptRunning;

  if (guiState.currentMode == MODE_MAIN) {
    drawButton(btnRecord,
               !systemState.sdAvailable ? COLOR_GRAY : (systemState.recording ? COLOR_RECORDING : COLOR_RECORD),
               COLOR_WHITE,
               systemState.recording ? "RECORDING" : "RECORD",
               false,
               btnRecord.enabled);

    drawButton(btnSDRefresh,
               systemState.sdAvailable ? RGB565_Apple_green: COLOR_RED,
               COLOR_WHITE,
               "SD",
               false,
               true);
  }
}

void checkInternalSD() {
  if (!systemState.recording) {
    ensureInternalSDContext();
    bool nowAvailable = false;
    if (SD.begin(BUILTIN_SDCARD)) {
      nowAvailable = SD.exists("/");
      if (nowAvailable && !SD.exists(SCRIPTS_DIR)) {
        SD.mkdir(SCRIPTS_DIR);
      }
    }
    systemState.internalSdAvailable = nowAvailable;
  }
}

void handleDataLogging(unsigned long currentMillis) {
  if (systemState.recording && (currentMillis - systemState.lastLogWrite >= LOG_WRITE_INTERVAL)) {
    recordDataDirect();
    systemState.lastLogWrite = currentMillis;
  }
}

void startRecording(bool scriptRequested) {
  if (systemState.recording) return;

  ensureExternalSDContext();

  if (!SD.begin(SD_CS)) {
    Serial.println("Cannot initialize external SD card");
    drawButton(btnRecord, COLOR_GRAY, COLOR_WHITE, "NO SD", false, false);
    delay(100);
    drawButton(btnRecord, COLOR_GRAY, COLOR_WHITE, "RECORD", false, false);
    return;
  }

  File root = SD.open("/");
  if (!root) {
    Serial.println("External SD card not accessible");
    drawButton(btnRecord, COLOR_GRAY, COLOR_WHITE, "SD ERR", false, false);
    delay(100);
    drawButton(btnRecord, COLOR_GRAY, COLOR_WHITE, "RECORD", false, false);
    return;
  }
  root.close();

  // Generate appropriate filename
  if (scriptRequested && strlen(currentScript.scriptName) > 0) {
    generateScriptFilename(systemState.recordFilename, sizeof(systemState.recordFilename), currentScript.scriptName);
  } else {
    if (systemState.csvOutput) {
      nextAvailableFilename(systemState.recordFilename, sizeof(systemState.recordFilename));
      strcpy(systemState.recordFilename + strlen(systemState.recordFilename) - 5, ".csv");
    } else {
      nextAvailableFilename(systemState.recordFilename, sizeof(systemState.recordFilename));
    }
  }

  logFile = SD.open(systemState.recordFilename, FILE_WRITE);
  if (!logFile) {
    Serial.println("Failed to create log file on external SD");
    drawButton(btnRecord, COLOR_GRAY, COLOR_WHITE, "SD ERR", false, false);
    delay(100);
    drawButton(btnRecord, COLOR_GRAY, COLOR_WHITE, "RECORD", false, false);
    return;
  }

  systemState.recordingScript = scriptRequested;

  // Write file header
  if (systemState.csvOutput) {
    logFile.print("Time");
    for (int i = 0; i < numSwitches; i++) {
      logFile.print(",");
      logFile.print(switchOutputs[i].name);
      logFile.print("_State,");
      logFile.print(switchOutputs[i].name);
      logFile.print("_Voltage,");
      logFile.print(switchOutputs[i].name);
      logFile.print("_Current,");
      logFile.print(switchOutputs[i].name);
      logFile.print("_Power");
    }
    logFile.println();
  } else {
    // JSON header with metadata
    logFile.print("{\n");
    logFile.print("\"using_script\":");
    logFile.print(scriptRequested ? "1" : "0");
    logFile.print(",\n");

    if (scriptRequested) {
      logFile.print("\"script_config\":{\n");
      logFile.print("\"name\":\"");
      logFile.print(currentScript.scriptName);
      logFile.print("\",\n\"tstart\":");
      logFile.print(currentScript.tStart);
      logFile.print(",\"tend\":");
      logFile.print(currentScript.tEnd);
      logFile.print(",\"record\":");
      logFile.print(currentScript.useRecord ? "true" : "false");
      logFile.print(",\n\"devices\":[\n");

      // Include device configuration
      for (int i = 0; i < 6; i++) {
        if (i > 0) logFile.print(",\n");
        logFile.print("{\"name\":\"");
        logFile.print(switchOutputs[i].name);
        logFile.print("\",\"enabled\":");
        logFile.print(currentScript.devices[i].enabled ? "true" : "false");
        logFile.print(",\"onTime\":");
        logFile.print(currentScript.devices[i].onTime);
        logFile.print(",\"offTime\":");
        logFile.print(currentScript.devices[i].offTime);
        logFile.print("}");
      }

      logFile.print("\n],\n\"script_ended_early\":false");
      logFile.print("\n},\n");
    } else {
      logFile.print("\"script_config\":null,\n");
    }

    time_t nowT = now();
    logFile.print("\"timestamp\":\"");
    logFile.print(formatTimeHHMMSS(nowT));
    logFile.print("\",\n");
    logFile.print("\"data\":[\n");
  }

  logFile.flush();

  systemState.recording = true;
  systemState.recordStartMillis = millis();
  systemState.firstDataPoint = true;
  btnRecord.label = "RECORDING";

  if (guiState.currentMode == MODE_MAIN) {
    drawButton(btnRecord, COLOR_RECORDING, COLOR_WHITE, "RECORDING", false, true);
  }

  Serial.print("Recording started: ");
  Serial.println(systemState.recordFilename);
}

void stopRecording() {
  if (!systemState.recording) return;

  Serial.println("Stopping recording...");

  ensureExternalSDContext();

  systemState.recording = false;
  bool wasScriptRecording = systemState.recordingScript;
  systemState.recordingScript = false;

  if (logFile) {
    if (!systemState.csvOutput) {
      long durationSec = (millis() - systemState.recordStartMillis) / 1000;
      logFile.print("\n],\n");
      logFile.print("\"duration_sec\":");
      logFile.print(durationSec);

      if (wasScriptRecording) {
        logFile.print(",\n\"script_ended_early\":");
        logFile.print(isScriptEndedEarly() ? "true" : "false");
      }

      logFile.print("\n}");
    }
    logFile.flush();
    logFile.close();
    Serial.println("Recording stopped and file closed successfully");
  }

  btnRecord.label = "RECORD";
  if (guiState.currentMode == MODE_MAIN) {
    drawButton(btnRecord, systemState.sdAvailable ? COLOR_RECORD : COLOR_GRAY, COLOR_WHITE, "RECORD", false, systemState.sdAvailable);
  }
}

void recordDataDirect() {
  if (!systemState.recording || !logFile) return;

  ensureExternalSDContext();

  // Validate log file integrity
  if (!logFile.available() && logFile.size() == 0) {
    Serial.println("Log file became invalid - stopping recording");
    stopRecording();
    return;
  }

  unsigned long t = (millis() - systemState.recordStartMillis);

  if (systemState.csvOutput) {
    // CSV format output
    logFile.print(t);
    for (int i = 0; i < numSwitches; i++) {
      int inaIdx = getInaIndexForSwitch(i);
      logFile.print(",");
      logFile.print(switchOutputs[i].state ? "1" : "0");
      logFile.print(",");
      logFile.print(inaIdx >= 0 ? deviceVoltage[inaIdx] : 0.0f, 4);
      logFile.print(",");
      logFile.print(inaIdx >= 0 ? (deviceCurrent[inaIdx] / 1000.0f) : 0.0f, 4);
      logFile.print(",");
      logFile.print(inaIdx >= 0 ? devicePower[inaIdx] : 0.0f, 4);
    }
    logFile.println();
  } else {
    // JSON format output
    if (!systemState.firstDataPoint) {
      logFile.print(",\n");
    } else {
      systemState.firstDataPoint = false;
    }

    time_t nowT = now();
    logFile.print("{\"time\":");
    logFile.print(t);
    logFile.print(",\"timestamp\":\"");
    logFile.print(formatTimeHHMMSS(nowT));
    logFile.print("\"");

    for (int i = 0; i < numSwitches; i++) {
      int inaIdx = getInaIndexForSwitch(i);

      logFile.print(",\"");
      logFile.print(switchOutputs[i].name);
      logFile.print("_volt\":");
      logFile.print(inaIdx >= 0 ? deviceVoltage[inaIdx] : 0.0f, 4);

      logFile.print(",\"");
      logFile.print(switchOutputs[i].name);
      logFile.print("_curr\":");
      logFile.print(inaIdx >= 0 ? (deviceCurrent[inaIdx] / 1000.0f) : 0.0f, 4);

      logFile.print(",\"");
      logFile.print(switchOutputs[i].name);
      logFile.print("_pow\":");
      logFile.print(inaIdx >= 0 ? devicePower[inaIdx] : 0.0f, 4);

      logFile.print(",\"");
      logFile.print(switchOutputs[i].name);
      logFile.print("_stat\":");
      logFile.print(switchOutputs[i].state ? "1" : "0");
    }
    logFile.print("}");
  }

  // Periodic flush to ensure data integrity
  static int flushCounter = 0;
  if (++flushCounter >= 3) {
    logFile.flush();
    flushCounter = 0;
  }
}

void nextAvailableFilename(char* buf, size_t buflen) {
  int idx = 0;
  const char* ext = systemState.csvOutput ? ".csv" : ".json";
  while (true) {
    if (idx == 0)
      snprintf(buf, buflen, "power_data%s", ext);
    else
      snprintf(buf, buflen, "power_data%d%s", idx, ext);

    ensureExternalSDContext();
    if (!SD.exists(buf)) break;
    idx++;
    if (idx > 999) break;
  }
}
