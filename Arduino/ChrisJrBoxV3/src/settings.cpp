/**
 * @file settings.cpp
 * @brief EEPROM settings management implementation
 */

#include "settings.h"
#include "config.h"
#include "display.h"
#include "network.h"
#include "script.h"
#include <EEPROM.h>

// External references
extern SystemState systemState;
extern GUIState guiState;
extern NetworkConfig networkConfig;
extern SortMode currentSortMode;
extern Adafruit_ST7796S tft;

void initSettings() {
  initializeEEPROM();
  applyDarkMode();
  applyFanSettings();

  // Debug output
  Serial.print("Fan initialized - On: ");
  Serial.print(systemState.fanOn);
  Serial.print(", Speed: ");
  Serial.println(systemState.fanSpeed);
}

void initializeEEPROM() {
  uint32_t magic;
  uint32_t version;

  EEPROM.get(EEPROM_MAGIC_ADDR, magic);
  EEPROM.get(EEPROM_VERSION_ADDR, version);

  if (magic != EEPROM_MAGIC_NUMBER || version != EEPROM_VERSION_NUMBER) {
    Serial.println("Initializing EEPROM with default values...");

    // Set validation markers
    EEPROM.put(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_NUMBER);
    EEPROM.put(EEPROM_VERSION_ADDR, EEPROM_VERSION_NUMBER);

    // Initialize default system settings
    systemState.fanOn = true;
    systemState.fanSpeed = 255;  // Full speed by default
    systemState.updateRate = 100;
    systemState.use24HourFormat = true;
    systemState.darkMode = true;

    // Initialize network configuration
    networkConfig.enableEthernet = true;
    networkConfig.useDHCP = true;
    networkConfig.staticIP = 0xC0A80164;
    networkConfig.subnet = 0xFFFFFF00;
    networkConfig.gateway = 0xC0A80101;
    networkConfig.dns = 0x08080808;
    networkConfig.tcpPort = 8080;
    networkConfig.udpPort = 8081;
    networkConfig.udpTargetIP = 0xFFFFFFFF;
    networkConfig.udpTargetPort = 8082;
    networkConfig.networkTimeout = 10000;
    networkConfig.dhcpTimeout = 8000;

    saveSettingsToEEPROM();
    saveNetworkConfig();

    Serial.println("EEPROM initialized with default values");
  } else {
    Serial.println("EEPROM already initialized, loading settings...");
    loadSettingsFromEEPROM();
    loadNetworkConfig();
  }
}

void saveSettingsToEEPROM() {
  EEPROM.put(EEPROM_FAN_ON_ADDR, systemState.fanOn);
  EEPROM.put(EEPROM_FAN_SPEED_ADDR, systemState.fanSpeed);
  EEPROM.put(EEPROM_UPDATE_RATE_ADDR, systemState.updateRate);
  EEPROM.put(EEPROM_TIME_FORMAT_ADDR, systemState.use24HourFormat);
  EEPROM.put(EEPROM_DARK_MODE_ADDR, systemState.darkMode);
  EEPROM.put(EEPROM_SORT_MODE_ADDR, (int)currentSortMode);
}

void loadSettingsFromEEPROM() {
  EEPROM.get(EEPROM_FAN_ON_ADDR, systemState.fanOn);
  EEPROM.get(EEPROM_FAN_SPEED_ADDR, systemState.fanSpeed);
  EEPROM.get(EEPROM_UPDATE_RATE_ADDR, systemState.updateRate);
  EEPROM.get(EEPROM_TIME_FORMAT_ADDR, systemState.use24HourFormat);
  EEPROM.get(EEPROM_DARK_MODE_ADDR, systemState.darkMode);

  // Load and validate sort mode
  int sortMode;
  EEPROM.get(EEPROM_SORT_MODE_ADDR, sortMode);
  if (sortMode >= 0 && sortMode <= 2) {
    currentSortMode = (SortMode)sortMode;
  } else {
    currentSortMode = SORT_NAME;
  }

  // Validate loaded values
  if (systemState.fanSpeed < 0 || systemState.fanSpeed > 255) systemState.fanSpeed = 255;
  if (systemState.updateRate < 10 || systemState.updateRate > 5000) systemState.updateRate = 100;

  // Ensure fan state matches speed
  if (systemState.fanSpeed > 0 && !systemState.fanOn) {
    systemState.fanOn = true;
    Serial.println("Fan speed > 0 but fan was off, turning on");
  }
}

void applyFanSettings() {
  int pwmValue = systemState.fanOn ? systemState.fanSpeed : 0;
  analogWrite(FAN_PWM_PIN, pwmValue);

  // Debug output
  Serial.print("Fan PWM set to: ");
  Serial.print(pwmValue);
  Serial.print(" (Pin ");
  Serial.print(FAN_PWM_PIN);
  Serial.println(")");
}

void applyUpdateRate() {
  // Update rate is used directly in main loop
}

void applyDarkMode() {
  tft.invertDisplay(systemState.darkMode);
  Serial.print("Dark mode: ");
  Serial.println(systemState.darkMode ? "ON" : "OFF");
}
