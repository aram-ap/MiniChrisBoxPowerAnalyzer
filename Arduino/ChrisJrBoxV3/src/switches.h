/**
* @file switches.h
 * @brief Switch and output control
 */

#ifndef SWITCHES_H
#define SWITCHES_H

#include <Arduino.h>
#include "types.h"

// Switch initialization
void initSwitches();

// Switch handling
void handlePhysicalSwitches();
void setAllOutputsOff();
void syncOutputsToSwitches();
void syncSwitchesToOutputs();
void setOutputState(String deviceName, bool state);
int findSwitchIndex(String deviceName);

// LED control
void updateLEDs(unsigned long currentMillis);
void updateSystemLEDs();

// External objects
extern SwitchOutput switchOutputs[];
extern const int numSwitches;

#endif // SWITCHES_H
