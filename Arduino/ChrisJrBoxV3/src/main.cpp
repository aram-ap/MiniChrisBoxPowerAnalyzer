/**
 * @file main.cpp
 * @brief Main entry point for Mini Chris Box V4-5.x
 * @author Aram Aprahamian
 * @version 5.1
 * @date July 14, 2025
 */

#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "types.h"
#include "network.h"
#include "display.h"
#include "touch.h"
#include "script.h"
#include "datalog.h"
#include "sensors.h"
#include "switches.h"
#include "settings.h"
#include "serial_commands.h"
#include "time_utils.h"
#include "utils.h"

// Global state variables
SystemState systemState;
GUIState guiState;

void setup() {
  Serial.begin(2000000);
  Wire.begin();

  // Initialize display first for status messages
  initDisplay();

  // Initialize touch screen - THIS WAS MISSING!
  initTouch();

  // Initialize EEPROM and settings
  initSettings();

  // Show initialization screen
  drawInitializationScreen();

  // Initialize subsystems
  initTimeUtils();
  initSensors();
  initSwitches();
  initDataLogging();
  initScript();
  initNetwork();

  // Wait for network initialization
  waitForNetworkInit();

  // Switch to main screen
  guiState.currentMode = MODE_MAIN;
  drawMainScreen();

  // Send startup message
  if (Serial) {
    systemState.serialAvailable = true;
    Serial.println("Teensy 4.1 Power Controller Ready - Network Enabled");
    Serial.println("Type 'help' for available commands");
    printNetworkStatus();
  }
}

void loop() {
  unsigned long currentMillis = millis();

  // Update network
  updateNetwork();

  // Process serial commands
  processSerialCommands();

  // Handle network communication
  handleNetworkCommunication();

  // Handle data streaming
  handleDataStreaming(currentMillis);

  // Check SD card status
  checkSDCardStatus(currentMillis);

  // Handle keypad input
  handleKeypadInput();

  // Handle LED states
  updateLEDs(currentMillis);

  // Update sensors
  updateSensors(currentMillis);

  // Update display
  updateDisplay(currentMillis);

  // Handle data logging
  handleDataLogging(currentMillis);

  // Handle script execution
  handleScriptExecution(currentMillis);

  // Handle physical switches
  handlePhysicalSwitches();

  // Handle touch input
  handleTouch(currentMillis);

  // Update system LEDs
  updateSystemLEDs();
}
