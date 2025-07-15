/**
 * @file serial_commands.cpp
 * @brief Serial command processing implementation
 */

#include "serial_commands.h"
#include "config.h"
#include "switches.h"
#include "sensors.h"
#include "script.h"
#include "datalog.h"
#include "network.h"
#include "display.h"
#include <ArduinoJson.h>

using namespace qindesign::network;

// External references
extern SystemState systemState;
extern GUIState guiState;
extern NetworkConfig networkConfig;
extern bool ethernetConnected;

// Private variables
static String serialBuffer = "";

void processSerialCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, serialBuffer);

        if (!error) {
          processNetworkCommand(serialBuffer, &Serial);
        } else {
          handleCommand(serialBuffer);
        }
        serialBuffer = "";
      }
    } else {
      serialBuffer += c;
    }
  }
}

void handleCommand(String command) {
  command.trim();
  command.toLowerCase();

  Serial.print("Command received: ");
  Serial.println(command);

  if (command == "help") {
    printHelp();
  }
  else if (command == "status") {
    printCurrentStatus();
  }
  else if (command == "lock") {
    systemState.lock = true;
    updateLockButton();
    Serial.println("System LOCKED");
  }
  else if (command == "unlock") {
    bool prevLock = systemState.lock;
    systemState.lock = false;
    updateLockButton();
    if (prevLock) syncOutputsToSwitches();
    Serial.println("System UNLOCKED");
  }
  else if (command == "start log") {
    if (!systemState.recording) {
      startRecording(false);
      Serial.println("Logging STARTED");
    } else {
      Serial.println("Already logging");
    }
  }
  else if (command == "stop log") {
    if (systemState.recording) {
      stopRecording();
      Serial.println("Logging STOPPED");
    } else {
      Serial.println("Not currently logging");
    }
  }
  else if (command == "csv on") {
    systemState.csvOutput = true;
    systemState.csvHeaderWritten = false;
    Serial.println("CSV output format ENABLED");
  }
  else if (command == "csv off") {
    systemState.csvOutput = false;
    Serial.println("Human readable output format ENABLED");
  }
  else if (command == "refresh sd") {
    smartCheckSDCard();
    checkInternalSD();
    Serial.println("SD card status refreshed manually");
  }
  else if (command.indexOf(" on") != -1) {
    String deviceName = command.substring(0, command.indexOf(" on"));
    setOutputState(deviceName, true);
  }
  else if (command.indexOf(" off") != -1) {
    String deviceName = command.substring(0, command.indexOf(" off"));
    setOutputState(deviceName, false);
  }
  else {
    Serial.println("Unknown command. Type 'help' for available commands.");
  }
}

void printCurrentStatus() {
  if (systemState.csvOutput) {
    if (!systemState.csvHeaderWritten) {
      Serial.print("Time,");
      for (int i = 0; i < numSwitches; i++) {
        Serial.print(switchOutputs[i].name);
        Serial.print("_State,");
        Serial.print(switchOutputs[i].name);
        Serial.print("_Voltage,");
        Serial.print(switchOutputs[i].name);
        Serial.print("_Current,");
        Serial.print(switchOutputs[i].name);
        Serial.print("_Power");
        if (i < numSwitches-1) Serial.print(",");
      }
      Serial.println();
      systemState.csvHeaderWritten = true;
    }
    Serial.print(millis());
    Serial.print(",");
    for (int i = 0; i < numSwitches; i++) {
      int inaIdx = getInaIndexForSwitch(i);
      Serial.print(switchOutputs[i].state ? "1":"0");
      Serial.print(",");
      Serial.print(inaIdx>=0?deviceVoltage[inaIdx]:0,4);
      Serial.print(",");
      Serial.print(inaIdx>=0?(deviceCurrent[inaIdx]/1000.0):0,4);
      Serial.print(",");
      Serial.print(inaIdx>=0?devicePower[inaIdx]:0,4);
      if (i < numSwitches-1) Serial.print(",");
    }
    Serial.println();
  } else {
    Serial.println("=== Current Status ===");
    Serial.print("System Lock: ");
    Serial.println(systemState.lock ? "LOCKED":"UNLOCKED");
    Serial.print("Safety Stop: ");
    Serial.println(systemState.safetyStop ? "ACTIVE":"INACTIVE");
    Serial.print("Recording: ");
    Serial.println(systemState.recording ? "ACTIVE":"INACTIVE");
    Serial.print("Script Running: ");
    Serial.println(isScriptRunning ? "YES":"NO");
    Serial.print("Output Format: ");
    Serial.println(systemState.csvOutput ? "CSV":"Human Readable");
    Serial.print("Dark Mode: ");
    Serial.println(systemState.darkMode ? "ON":"OFF");
    Serial.print("External SD: ");
    Serial.println(systemState.sdAvailable ? "Available":"Not Available");
    Serial.print("Internal SD: ");
    Serial.println(systemState.internalSdAvailable ? "Available":"Not Available");
    Serial.print("SD Check Interval: ");
    Serial.print(SD_CHECK_INTERVAL);
    Serial.println("ms");
    Serial.print("Ethernet Enabled: ");
    Serial.println(networkConfig.enableEthernet ? "YES":"NO");
    Serial.print("Ethernet Connected: ");
    Serial.println(ethernetConnected ? "YES":"NO");
    if (ethernetConnected) {
      Serial.print("IP Address: ");
      Serial.println(ipToString(Ethernet.localIP()));
      Serial.print("TCP Port: ");
      Serial.println(networkConfig.tcpPort);
      Serial.print("UDP Port: ");
      Serial.println(networkConfig.udpPort);
    }
    Serial.println();

    for (int i = 0; i < numSwitches; i++) {
      int inaIdx = getInaIndexForSwitch(i);
      Serial.print(switchOutputs[i].name);
      Serial.print(": ");
      Serial.print(switchOutputs[i].state ? "ON":"OFF");
      if (inaIdx >= 0) {
        Serial.print(" | V=");
        Serial.print(deviceVoltage[inaIdx], 2);
        Serial.print("V | I=");
        Serial.print(deviceCurrent[inaIdx] / 1000.0, 3);
        Serial.print("A | P=");
        Serial.print(devicePower[inaIdx], 3);
        Serial.print("W");
      }
      Serial.println();
    }
    Serial.println("===================");
  }
}

void printHelp() {
  Serial.println("=== Available Commands ===");
  Serial.println("Output Control:");
  Serial.println("  gse1 on/off  - Control GSE-1 output");
  Serial.println("  gse2 on/off  - Control GSE-2 output");
  Serial.println("  ter on/off   - Control TE-R output");
  Serial.println("  te1 on/off   - Control TE-1 output");
  Serial.println("  te2 on/off   - Control TE-2 output");
  Serial.println("  te3 on/off   - Control TE-3 output");
  Serial.println();
  Serial.println("System Control:");
  Serial.println("  lock         - Lock all outputs");
  Serial.println("  unlock       - Unlock outputs");
  Serial.println("  start log    - Start data logging");
  Serial.println("  stop log     - Stop data logging");
  Serial.println("  refresh sd   - Manually refresh SD card status");
  Serial.println();
  Serial.println("Output Format:");
  Serial.println("  csv on       - Enable CSV output format");
  Serial.println("  csv off      - Enable human readable format");
  Serial.println();
  Serial.println("Information:");
  Serial.println("  status       - Show current system status");
  Serial.println("  help         - Show this help message");
  Serial.println();
  Serial.println("Network Commands (JSON format):");
  Serial.println("  {\"cmd\":\"get_status\"}");
  Serial.println("  {\"cmd\":\"start_stream\",\"interval\":100}");
  Serial.println("  {\"cmd\":\"set_output\",\"device\":\"GSE-1\",\"state\":true}");
  Serial.println("========================");
}

void serialPrint(String message) {
  if (systemState.serialAvailable && message.trim().length() > 0) {
    String temp = message;
    temp.trim();
    Serial.println(temp);
  }
}
