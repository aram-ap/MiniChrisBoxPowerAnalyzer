/**
 * @file switches.cpp
 * @brief Switch and output control implementation
 */

#include "switches.h"
#include "config.h"
#include "display.h"
#include "sensors.h"
#include "script.h"
#include <Bounce2.h>

// External references
extern SystemState systemState;
extern GUIState guiState;

// Switch/Output configuration
SwitchOutput switchOutputs[] = {
  { "GSE-1", 0,  41, Bounce(), LOW },
  { "GSE-2", 5,  15, Bounce(), LOW },
  { "TE-R",  1,  40, Bounce(), LOW },
  { "TE-1",  2,  39, Bounce(), LOW },
  { "TE-2",  3,  38, Bounce(), LOW },
  { "TE-3",  4,  24, Bounce(), LOW }
};
const int numSwitches = sizeof(switchOutputs) / sizeof(SwitchOutput);

// LED state variables
static bool powerLedState = false;
static bool lockLedState = false;

void initSwitches() {
  // Initialize switch inputs and outputs
  for (int i = 0; i < numSwitches; i++) {
    if (switchOutputs[i].switchPin == -1) {
      continue;
    }
    pinMode(switchOutputs[i].switchPin, INPUT_PULLUP);
    pinMode(switchOutputs[i].outputPin, OUTPUT);
    switchOutputs[i].debouncer.attach(switchOutputs[i].switchPin);
    switchOutputs[i].debouncer.interval(10);
  }

  // Set initial output states based on switch positions
  for (int i = 0; i < numSwitches; i++) {
    if (switchOutputs[i].switchPin == -1) {
      continue;
    }
    int initialState = digitalRead(switchOutputs[i].switchPin);
    if (initialState == LOW) {
      digitalWrite(switchOutputs[i].outputPin, HIGH);
      switchOutputs[i].state = HIGH;
    } else {
      digitalWrite(switchOutputs[i].outputPin, LOW);
      switchOutputs[i].state = LOW;
    }
  }

  // Initialize LED outputs
  pinMode(PWR_LED_PIN, OUTPUT);
  pinMode(LOCK_LED_PIN, OUTPUT);
  pinMode(STOP_LED_PIN, OUTPUT);

  digitalWrite(PWR_LED_PIN, HIGH);
  digitalWrite(LOCK_LED_PIN, LOW);
  digitalWrite(STOP_LED_PIN, LOW);
}

void handlePhysicalSwitches() {
  for (int i = 0; i < numSwitches; i++) {
    switchOutputs[i].debouncer.update();
    if (!systemState.lock && !systemState.safetyStop && !isScriptRunning) {
      if (switchOutputs[i].debouncer.fell()) {
        digitalWrite(switchOutputs[i].outputPin, HIGH);
        switchOutputs[i].state = HIGH;
        if (guiState.currentMode == MODE_MAIN) drawDeviceRow(i);
        if (guiState.currentMode == MODE_SCRIPT) updateLiveValueRow(i);
      }
      else if (switchOutputs[i].debouncer.rose()) {
        digitalWrite(switchOutputs[i].outputPin, LOW);
        switchOutputs[i].state = LOW;
        if (guiState.currentMode == MODE_MAIN) drawDeviceRow(i);
        if (guiState.currentMode == MODE_SCRIPT) updateLiveValueRow(i);
      }
    }
  }
}

void setAllOutputsOff() {
  for (int i = 0; i < numSwitches; i++) {
    digitalWrite(switchOutputs[i].outputPin, LOW);
    switchOutputs[i].state = LOW;
    if (guiState.currentMode == MODE_MAIN) drawDeviceRow(i);
    if (guiState.currentMode == MODE_SCRIPT) updateLiveValueRow(i);
  }
}

void syncOutputsToSwitches() {
  for (int i = 0; i < numSwitches; i++) {
    if (switchOutputs[i].switchPin == -1) {
      continue;
    }
    int sw = digitalRead(switchOutputs[i].switchPin);
    if (sw == LOW) {
      digitalWrite(switchOutputs[i].outputPin, HIGH);
      switchOutputs[i].state = HIGH;
    } else {
      digitalWrite(switchOutputs[i].outputPin, LOW);
      switchOutputs[i].state = LOW;
    }
    if (guiState.currentMode == MODE_MAIN) drawDeviceRow(i);
    if (guiState.currentMode == MODE_SCRIPT) updateLiveValueRow(i);
  }
}

void syncSwitchesToOutputs() {
  for (int i = 0; i < numSwitches; i++) {
    if (switchOutputs[i].switchPin == -1) continue;

    int switchState = digitalRead(switchOutputs[i].switchPin);
    bool switchOn = (switchState == LOW);

    if (switchOn && switchOutputs[i].state == LOW) {
      digitalWrite(switchOutputs[i].outputPin, HIGH);
      switchOutputs[i].state = HIGH;
      if (guiState.currentMode == MODE_MAIN) drawDeviceRow(i);
      if (guiState.currentMode == MODE_SCRIPT) updateLiveValueRow(i);
    }
    else if (!switchOn && switchOutputs[i].state == HIGH) {
      digitalWrite(switchOutputs[i].outputPin, LOW);
      switchOutputs[i].state = LOW;
      if (guiState.currentMode == MODE_MAIN) drawDeviceRow(i);
      if (guiState.currentMode == MODE_SCRIPT) updateLiveValueRow(i);
    }
  }
}

int findSwitchIndex(String deviceName) {
  if (deviceName == "gse1") deviceName = "gse-1";
  else if (deviceName == "gse2") deviceName = "gse-2";
  else if (deviceName == "ter") deviceName = "te-r";
  else if (deviceName == "te1") deviceName = "te-1";
  else if (deviceName == "te2") deviceName = "te-2";
  else if (deviceName == "te3") deviceName = "te-3";

  for (int i = 0; i < numSwitches; i++) {
    String swName = String(switchOutputs[i].name);
    swName.toLowerCase();
    if (swName == deviceName) {
      return i;
    }
  }
  return -1;
}

void setOutputState(String deviceName, bool state) {
  if (systemState.lock || systemState.safetyStop || isScriptRunning) {
    Serial.println("Cannot change outputs - system is locked, in safety stop, or script is running");
    return;
  }
  int switchIndex = findSwitchIndex(deviceName);
  if (switchIndex >= 0) {
    digitalWrite(switchOutputs[switchIndex].outputPin, state ? HIGH : LOW);
    switchOutputs[switchIndex].state = state ? HIGH : LOW;
    if (guiState.currentMode == MODE_MAIN) drawDeviceRow(switchIndex);
    if (guiState.currentMode == MODE_SCRIPT) updateLiveValueRow(switchIndex);

    Serial.print(switchOutputs[switchIndex].name);
    Serial.print(" turned ");
    Serial.println(state ? "ON" : "OFF");
  } else {
    Serial.print("Unknown device: ");
    Serial.println(deviceName);
    Serial.println("Available devices: gse1, gse2, ter, te1, te2, te3");
  }
}

void updateLEDs(unsigned long currentMillis) {
  // Handle recording LED blinking
  if (systemState.recording && (currentMillis - systemState.lastPowerLedBlink >= 500)) {
    systemState.lastPowerLedBlink = currentMillis;
    powerLedState = !powerLedState;
    digitalWrite(PWR_LED_PIN, powerLedState ? HIGH : LOW);
  } else if (!systemState.recording) {
    digitalWrite(PWR_LED_PIN, HIGH);
  }

  // Handle script execution lock LED blinking
  static unsigned long lastLockBlink = 0;
  if (isScriptRunning && !isScriptPaused) {
    if (currentMillis - lastLockBlink >= 500) {
      lastLockBlink = currentMillis;
      lockLedState = !lockLedState;
      digitalWrite(LOCK_LED_PIN, lockLedState ? HIGH : LOW);
    }
  }
  else {
    digitalWrite(LOCK_LED_PIN, systemState.lock ? HIGH : LOW);
  }
}

void updateSystemLEDs() {
  // Update safety stop LED
  digitalWrite(STOP_LED_PIN, systemState.safetyStop ? HIGH : LOW);
}
