/**
 * @file main.cpp
 * @brief Main entry point for Mini Chris Box V4-5.x
 * @author Aram Aprahamian
 * @version 5.1
 * @date July 14, 2025
 * 
 * MIT License
 * 
 * Copyright (c) 2025 Aram Aprahamian
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
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
#include "graphs.h"  
#include "ui_colors.h"
// Remove color settings include
// #include "color_settings.h"
#include <InternalTemperature.h>

// Global state variables
SystemState systemState;
GUIState guiState;

void setup() {
  Serial.begin(2000000);
  Wire.begin();

  // Initialize internal temperature sensor
  InternalTemperature.begin(TEMPERATURE_NO_ADC_SETTING_CHANGES);

  // Initialize display first for status messages
  initDisplay();

  // Initialize touch screen
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
  initGraphs();  // Add graph initialization
  initUIColors(); // Initialize UI color system
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

  // Update graphs
  updateGraphData(currentMillis);  // Add graph data updates

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

  // Update snake game
  if (guiState.currentMode == MODE_SNAKE) {
    updateSnakeGame();
  }

  // Update system LEDs
  updateSystemLEDs();
}
