/**
* @file sensors.h
 * @brief INA226 sensor handling
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <INA226.h>

// Sensor initialization
void initSensors();

// Sensor updates
void updateSensors(unsigned long currentMillis);
void updateSensorData();

// Utility functions
int getInaIndexForSwitch(int switchIdx);

// External data
extern float deviceVoltage[];
extern float deviceCurrent[];
extern float devicePower[];
extern const int numIna;

#endif // SENSORS_H
