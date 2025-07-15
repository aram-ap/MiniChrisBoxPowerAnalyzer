/**
* @file time_utils.h
 * @brief Time and RTC utilities
 */

#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include <Arduino.h>
#include <TimeLib.h>

// Time initialization
void initTimeUtils();

// Time functions
time_t getTeensyTime();
void setDateTime(tmElements_t tm);
String getCurrentTimeString();
String formatTimeHHMMSS(time_t t);
String formatDateString(time_t t);
String formatShortDateTime(time_t t);

// External objects
extern tmElements_t tmSet;

#endif // TIME_UTILS_H
