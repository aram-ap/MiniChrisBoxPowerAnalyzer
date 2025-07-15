/**
* @file time_utils.cpp
 * @brief Time and RTC utilities implementation
 */

#include "time_utils.h"
#include "types.h"
#include <DS1307RTC.h>

// External references
extern SystemState systemState;

// Time editing variables
tmElements_t tmSet;

void initTimeUtils() {
    setSyncProvider(getTeensyTime);

    if (timeStatus() != timeSet) {
        Serial.println("Unable to sync with the RTC");
        setTime(0, 0, 0, 1, 1, 2025);
    } else {
        Serial.println("RTC has set the system time");
    }
}

time_t getTeensyTime() {
    return Teensy3Clock.get();
}

void setDateTime(tmElements_t tm) {
    time_t t = makeTime(tm);
    setTime(t);
    Teensy3Clock.set(t);
}

String getCurrentTimeString() {
    time_t t = now();
    return formatTimeHHMMSS(t);
}

String formatTimeHHMMSS(time_t t) {
    char buf[12];
    if (systemState.use24HourFormat) {
        sprintf(buf, "%02d:%02d:%02d", hour(t), minute(t), second(t));
    } else {
        int h = hour(t);
        bool isPM = h >= 12;
        if (h == 0) h = 12;
        else if (h > 12) h -= 12;
        sprintf(buf, "%d:%02d:%02d %s", h, minute(t), second(t), isPM ? "PM" : "AM");
    }
    return String(buf);
}

String formatDateString(time_t t) {
    char buf[12];
    sprintf(buf, "20%02d-%02d-%02d", year(t) % 100, month(t), day(t));
    return String(buf);
}

String formatShortDateTime(time_t t) {
    char buf[16];
    if (systemState.use24HourFormat) {
        sprintf(buf, "%02d/%02d %02d:%02d", month(t), day(t), hour(t), minute(t));
    } else {
        int h = hour(t);
        bool isPM = h >= 12;
        if (h == 0) h = 12;
        else if (h > 12) h -= 12;
        sprintf(buf, "%02d/%02d %d:%02d%s", month(t), day(t), h, minute(t), isPM ? "P" : "A");
    }
    return String(buf);
}
