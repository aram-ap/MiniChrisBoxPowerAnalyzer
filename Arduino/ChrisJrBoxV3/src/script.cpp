/**
 * @file script.cpp
 * @brief Script execution system implementation
 */

#include "script.h"
#include "config.h"
#include "switches.h"
#include "display.h"
#include "datalog.h"
#include "time_utils.h"
#include <SD.h>
#include <ArduinoJson.h>
#include <graphs.h>

// External references
extern SystemState systemState;
extern GUIState guiState;

// Script variables
Script currentScript;
bool isScriptRunning = false;
bool isScriptPaused = false;
long scriptTimeSeconds = 0;
DMAMEM ScriptMetadata scriptList[MAX_SCRIPTS];  // Move to RAM2 to free up 4KB of RAM1
int numScripts = 0;
SortMode currentSortMode = SORT_NAME;

// Script timing variables (made accessible for graph data collection)
unsigned long scriptStartMillis = 0;
unsigned long scriptPausedTime = 0;
unsigned long pauseStartMillis = 0;
static bool scriptEndedEarly = false;
static bool lockStateBeforeScript = false;
static bool deviceOnTriggered[6] = {false, false, false, false, false, false};
static bool deviceOffTriggered[6] = {false, false, false, false, false, false};

void initScript() {
  createNewScript();
  loadAllScriptNames();
  sortScripts();
}


void handleScripts() {
  unsigned long totalPausedTime = scriptPausedTime;
  if (isScriptPaused) {
    totalPausedTime += (millis() - pauseStartMillis);
  }

  unsigned long msSinceStart = millis() - scriptStartMillis - totalPausedTime;
  long currentSecond = currentScript.tStart + (long)((msSinceStart + 500) / 1000); // Round to nearest second
  scriptTimeSeconds = currentSecond;

  // Check for script completion
  if (currentSecond >= currentScript.tEnd) {
    stopScript(false);
    return;
  }

  // Process device timing events
  for (int i = 0; i < 6; i++) {
    if (!currentScript.devices[i].enabled) continue;

    // Handle device turn-on
    if (currentSecond >= currentScript.devices[i].onTime &&
        !deviceOnTriggered[i] && switchOutputs[i].state == LOW) {
      digitalWrite(switchOutputs[i].outputPin, HIGH);
      switchOutputs[i].state = HIGH;
      deviceOnTriggered[i] = true;
      if (guiState.currentMode == MODE_SCRIPT) {
        updateLiveValueRow(i);
      }
    }

    // Handle device turn-off
    if (currentSecond >= currentScript.devices[i].offTime &&
        !deviceOffTriggered[i] && switchOutputs[i].state == HIGH) {
      digitalWrite(switchOutputs[i].outputPin, LOW);
      switchOutputs[i].state = LOW;
      deviceOffTriggered[i] = true;
      if (guiState.currentMode == MODE_SCRIPT) {
        updateLiveValueRow(i);
      }
    }
  }
}

void handleScriptExecution(unsigned long currentMillis) {
  if (isScriptRunning && !isScriptPaused) {
    handleScripts();
  }
}

void startScript() {
  if (isScriptRunning || systemState.safetyStop) return;

  lockStateBeforeScript = systemState.lock;
  setAllOutputsOff();

  // Reset trigger flags
  for (int i = 0; i < 6; i++) {
    deviceOnTriggered[i] = false;
    deviceOffTriggered[i] = false;
  }

  systemState.lock = true;
  updateLockButton();

  scriptStartMillis = millis();
  scriptPausedTime = 0;
  isScriptRunning   = true;
  isScriptPaused    = false;
  scriptEndedEarly  = false;

  //Clear graph data when starting script to avoid time mismatch
  clearGraphData();


  // Update script usage timestamp
  currentScript.lastUsed = now();
  char filename[64];
  snprintf(filename, sizeof(filename), "%s.json", currentScript.scriptName);
  updateScriptLastUsed(filename);

  if (currentScript.useRecord) {
    startRecording(true);
  }

  if (guiState.currentMode == MODE_SCRIPT) {
    drawScriptPage();
  }
}

void pauseScript() {
  if (!isScriptRunning || isScriptPaused) return;

  isScriptPaused = true;
  pauseStartMillis = millis();

  if (guiState.currentMode == MODE_SCRIPT) {
    drawScriptPage();
  }
}

void resumeScript() {
  if (!isScriptRunning || !isScriptPaused) return;

  scriptPausedTime += (millis() - pauseStartMillis);
  isScriptPaused = false;

  if (guiState.currentMode == MODE_SCRIPT) {
    drawScriptPage();
  }
}

void stopScript(bool userEnded) {
  if (!isScriptRunning) return;

  extern void onScriptEnd();
  onScriptEnd();

  isScriptRunning = false;
  isScriptPaused = false;

  // Reset trigger flags
  for (int i = 0; i < 6; i++) {
    deviceOnTriggered[i] = false;
    deviceOffTriggered[i] = false;
  }

  systemState.lock = lockStateBeforeScript;
  updateLockButton();
  scriptEndedEarly = userEnded;

  if (systemState.recordingScript) {
    stopRecording();
  }

  // Synchronize switch states after script completion
  if (!systemState.safetyStop) {
    syncSwitchesToOutputs();
  }

  if (guiState.currentMode == MODE_SCRIPT) {
    drawScriptPage();
  }
}

void createNewScript() {
  memset(&currentScript, 0, sizeof(Script));
  strcpy(currentScript.scriptName, "Untitled");
  currentScript.useRecord = true;
  currentScript.tStart = 0;
  currentScript.tEnd = 120;
  currentScript.dateCreated = now();
  currentScript.lastUsed = now();

  for (int i = 0; i < 6; i++) {
    currentScript.devices[i].enabled = false;
    currentScript.devices[i].onTime = 0;
    currentScript.devices[i].offTime = 10;
  }
}

void loadScriptFromFile(const char* scriptName) {
  ensureInternalSDContext();

  char filePath[64];
  snprintf(filePath, sizeof(filePath), "%s/%s", SCRIPTS_DIR, scriptName);

  File scriptFile = SD.open(filePath, FILE_READ);
  if (!scriptFile) {
    Serial.print("Failed to open script file: ");
    Serial.println(filePath);
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, scriptFile);
  scriptFile.close();

  if (error) {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    return;
  }

  memset(&currentScript, 0, sizeof(Script));

  char nameOnly[32];
  strncpy(nameOnly, scriptName, sizeof(nameOnly) - 1);
  nameOnly[sizeof(nameOnly) - 1] = '\0';

  // Safely copy and null-terminate the script name
  snprintf(currentScript.scriptName, sizeof(currentScript.scriptName), "%s", nameOnly);

  currentScript.useRecord = doc["useRecord"] | true;
  currentScript.tStart = doc["tStart"] | 0;
  currentScript.tEnd = doc["tEnd"] | 120;
  currentScript.dateCreated = doc["dateCreated"] | now();
  currentScript.lastUsed = now();

  JsonArray devices = doc["devices"];
  for (int i = 0; i < 6 && i < (int)devices.size(); i++) {
    currentScript.devices[i].enabled = devices[i]["enabled"] | false;
    currentScript.devices[i].onTime = devices[i]["onTime"] | 0;
    currentScript.devices[i].offTime = devices[i]["offTime"] | 10;
  }

  updateScriptLastUsed(scriptName);

  Serial.print("Loaded script: ");
  Serial.println(currentScript.scriptName);
  loadAllScriptNames();
}

void saveCurrentScript() {
  ensureInternalSDContext();

  if (!SD.exists(SCRIPTS_DIR)) {
    SD.mkdir(SCRIPTS_DIR);
  }

  char filePath[64];
  snprintf(filePath, sizeof(filePath), "%s/%s.json", SCRIPTS_DIR, currentScript.scriptName);

  if (SD.exists(filePath)) {
    SD.remove(filePath);
  }

  File scriptFile = SD.open(filePath, FILE_WRITE);
  if (!scriptFile) {
    Serial.print("Failed to create script file: ");
    Serial.println(filePath);
    return;
  }

  if (currentScript.dateCreated == 0) {
    currentScript.dateCreated = now();
  }
  currentScript.lastUsed = now();

  JsonDocument doc;
  doc["name"] = currentScript.scriptName;
  doc["useRecord"] = currentScript.useRecord;
  doc["tStart"] = currentScript.tStart;
  doc["tEnd"] = currentScript.tEnd;
  doc["dateCreated"] = currentScript.dateCreated;
  doc["lastUsed"] = currentScript.lastUsed;

  JsonArray devices = doc["devices"].to<JsonArray>();
  for (int i = 0; i < 6; i++) {
    JsonObject device = devices.add<JsonObject>();
    device["enabled"] = currentScript.devices[i].enabled;
    device["onTime"] = currentScript.devices[i].onTime;
    device["offTime"] = currentScript.devices[i].offTime;
  }

  if (serializeJson(doc, scriptFile) == 0) {
    Serial.println("Failed to write to script file");
  } else {
    Serial.print("Saved script: ");
    Serial.println(currentScript.scriptName);
  }

  scriptFile.close();
  loadAllScriptNames();
}

bool loadAllScriptNames() {
  ensureInternalSDContext();

  if (!SD.exists(SCRIPTS_DIR)) {
    SD.mkdir(SCRIPTS_DIR);
  }

  File dir = SD.open(SCRIPTS_DIR);
  if (!dir) {
    Serial.println("Failed to open scripts directory");
    return false;
  }

  numScripts = 0;
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;

    if (strstr(entry.name(), ".json") != NULL) {
      strncpy(scriptList[numScripts].filename, entry.name(), sizeof(scriptList[numScripts].filename) - 1);
      scriptList[numScripts].filename[sizeof(scriptList[numScripts].filename) - 1] = '\0';

      char nameOnly[32];
      strncpy(nameOnly, entry.name(), sizeof(nameOnly) - 1);
      nameOnly[sizeof(nameOnly) - 1] = '\0';
      char* ext = strstr(nameOnly, ".json");
      if (ext) *ext = '\0';

      // Safer approach for the name copy
      size_t nameLen = strlen(nameOnly);
      if (nameLen >= sizeof(scriptList[numScripts].name)) {
        nameLen = sizeof(scriptList[numScripts].name) - 1;
      }
      memcpy(scriptList[numScripts].name, nameOnly, nameLen);
      scriptList[numScripts].name[nameLen] = '\0';

      // Read script metadata
      char filePath[64];
      snprintf(filePath, sizeof(filePath), "%s/%s", SCRIPTS_DIR, entry.name());
      File scriptFile = SD.open(filePath, FILE_READ);
      if (scriptFile) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, scriptFile);
        if (!error) {
          scriptList[numScripts].dateCreated = doc["dateCreated"] | now();
          scriptList[numScripts].lastUsed = doc["lastUsed"] | 946684800;
        } else {
          scriptList[numScripts].dateCreated = now();
          scriptList[numScripts].lastUsed = 946684800;
        }
        scriptFile.close();
      } else {
        scriptList[numScripts].dateCreated = now();
        scriptList[numScripts].lastUsed = now();
      }

      numScripts++;
      if (numScripts >= MAX_SCRIPTS) break;
    }
    entry.close();
  }

  dir.close();
  sortScripts();
  return (numScripts > 0);
}

void sortScripts() {
  for (int i = 0; i < numScripts - 1; i++) {
    for (int j = 0; j < numScripts - i - 1; j++) {
      bool shouldSwap = false;

      switch (currentSortMode) {
        case SORT_NAME:
          shouldSwap = strcmp(scriptList[j].name, scriptList[j + 1].name) > 0;
          break;
        case SORT_LAST_USED:
          shouldSwap = scriptList[j].lastUsed < scriptList[j + 1].lastUsed;
          break;
        case SORT_DATE_CREATED:
          shouldSwap = scriptList[j].dateCreated < scriptList[j + 1].dateCreated;
          break;
      }

      if (shouldSwap) {
        ScriptMetadata temp = scriptList[j];
        scriptList[j] = scriptList[j + 1];
        scriptList[j + 1] = temp;
      }
    }
  }
}

void deleteScript(const char* scriptName) {
  ensureInternalSDContext();

  char filePath[64];
  snprintf(filePath, sizeof(filePath), "%s/%s", SCRIPTS_DIR, scriptName);

  if (SD.exists(filePath)) {
    SD.remove(filePath);
    loadAllScriptNames();
  }
}

void updateScriptLastUsed(const char* scriptName) {
  ensureInternalSDContext();

  char filePath[64];
  snprintf(filePath, sizeof(filePath), "%s/%s", SCRIPTS_DIR, scriptName);

  File scriptFile = SD.open(filePath, FILE_READ);
  if (!scriptFile) {
    Serial.print("Failed to open script file for update: ");
    Serial.println(filePath);
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, scriptFile);
  scriptFile.close();

  if (error) {
    Serial.print("JSON parsing failed during lastUsed update: ");
    Serial.println(error.c_str());
    return;
  }

  doc["lastUsed"] = now();

  if (SD.exists(filePath)) {
    SD.remove(filePath);
  }

  scriptFile = SD.open(filePath, FILE_WRITE);
  if (!scriptFile) {
    Serial.print("Failed to open script file for writing: ");
    Serial.println(filePath);
    return;
  }

  if (serializeJson(doc, scriptFile) == 0) {
    Serial.println("Failed to write updated lastUsed to script file");
  }

  scriptFile.close();
}

void generateScriptFilename(char* buf, size_t buflen, const char* scriptName) {
  ensureExternalSDContext();

  const char* ext = systemState.csvOutput ? ".csv" : ".json";

  // Clean script name for filesystem compatibility
  char cleanName[32];
  int cleanPos = 0;
  size_t nameLen = strlen(scriptName);
  for (size_t i = 0; i < nameLen && cleanPos < 31; i++) {
    char c = scriptName[i];
    if (isalnum(c) || c == '-' || c == '_') {
      cleanName[cleanPos++] = c;
    } else if (c == ' ') {
      cleanName[cleanPos++] = '_';
    }
  }
  cleanName[cleanPos] = '\0';

  // Find next available filename
  int idx = 0;
  while (true) {
    if (idx == 0) {
      snprintf(buf, buflen, "%s%s", cleanName, ext);
    } else {
      snprintf(buf, buflen, "%s_%d%s", cleanName, idx, ext);
    }

    if (!SD.exists(buf)) break;
    idx++;
    if (idx > 999) break;
  }
}

// Helper function to check if script ended early
bool isScriptEndedEarly() {
  return scriptEndedEarly;
}
