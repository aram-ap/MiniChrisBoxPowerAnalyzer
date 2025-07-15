/**
* @file sensors.cpp
 * @brief INA226 sensor handling implementation
 */

#include "sensors.h"
#include "config.h"
#include "switches.h"
#include "display.h"

// External references
extern SystemState systemState;
extern GUIState guiState;

// INA226 current/voltage sensors
INA226 ina_gse1(0x40);
INA226 ina_gse2(0x41);
INA226 ina_ter(0x42);
INA226 ina_te1(0x43);
INA226 ina_te2(0x44);
INA226 ina_te3(0x45);
INA226 ina_all(0x46);

INA226* inaDevices[] = { &ina_gse1, &ina_gse2, &ina_ter, &ina_te1, &ina_te2, &ina_te3, &ina_all };
const char* inaNames[] = { "GSE-1", "GSE-2", "TE-R", "TE-1", "TE-2", "TE-3", "Bus" };
const int numIna = 7;

// Sensor data arrays
float deviceVoltage[numIna];
float deviceCurrent[numIna];
float devicePower[numIna];

void initSensors() {
    // Initialize INA226 sensors
    for (int i = 0; i < numIna; i++) {
        inaDevices[i]->begin();
        inaDevices[i]->setMaxCurrentShunt(8, 0.01);
    }
}

void updateSensors(unsigned long currentMillis) {
    if (currentMillis - systemState.lastSensorUpdate >= SENSOR_UPDATE_INTERVAL) {
        updateSensorData();
        systemState.lastSensorUpdate = currentMillis;
    }
}

void updateSensorData() {
    for (int i = 0; i < numIna; i++) {
        deviceVoltage[i] = inaDevices[i]->getBusVoltage();
        deviceCurrent[i] = inaDevices[i]->getCurrent_mA();
        devicePower[i]   = inaDevices[i]->getPower_mW() / 1000.0;
    }
}

int getInaIndexForSwitch(int switchIdx) {
    const char* name = switchOutputs[switchIdx].name;
    for (int i = 0; i < numIna; i++) {
        if (strcasecmp(name, inaNames[i]) == 0) return i;
    }
    return -1;
}
