/**
* @file datalog.h
 * @brief Data logging and SD card operations
 */

#ifndef DATALOG_H
#define DATALOG_H

#include <Arduino.h>
#include <SD.h>

// SD card initialization
void initDataLogging();

// SD card operations
void checkSDCardStatus(unsigned long currentMillis);
void ensureExternalSDContext();
void ensureInternalSDContext();
void smartCheckSDCard();
void checkInternalSD();

// Data logging
void handleDataLogging(unsigned long currentMillis);
void startRecording(bool scriptRequested = false);
void stopRecording();
void recordDataDirect();
void nextAvailableFilename(char* buf, size_t buflen);

// External objects
extern File logFile;

#endif // DATALOG_H
