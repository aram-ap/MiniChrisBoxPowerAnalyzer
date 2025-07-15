/**
* @file settings.h
 * @brief EEPROM settings management
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>

// Settings initialization
void initSettings();

// EEPROM operations
void initializeEEPROM();
void saveSettingsToEEPROM();
void loadSettingsFromEEPROM();

// Settings application
void applyFanSettings();
void applyUpdateRate();
void applyDarkMode();

#endif // SETTINGS_H
