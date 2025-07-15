/**
* @file touch.h
 * @brief Touch screen handling
 */

#ifndef TOUCH_H
#define TOUCH_H

#include <Arduino.h>
#include <XPT2046_Touchscreen.h>
#include "types.h"

// Touch initialization
void initTouch();

// Touch handling
void handleTouch(unsigned long currentMillis);
bool touchInButton(int16_t x, int16_t y, const ButtonRegion& btn);

// Touch handlers for each mode
void handleTouchMain(int16_t x, int16_t y);
void handleTouchSettings(int16_t x, int16_t y);
void handleTouchNetwork(int16_t x, int16_t y);
void handleTouchNetworkEdit(int16_t x, int16_t y);
void handleTouchKeypad(int16_t x, int16_t y);
void handleTouchScript(int16_t x, int16_t y);
void handleTouchEdit(int16_t x, int16_t y);
void handleTouchScriptLoad(int16_t x, int16_t y);
void handleTouchEditSave(int16_t x, int16_t y);
void handleTouchEditField(int16_t x, int16_t y);
void handleTouchDateTime(int16_t x, int16_t y);
void handleTouchEditName(int16_t x, int16_t y);
void handleTouchDeleteConfirm(int16_t x, int16_t y);
void handleTouchAbout(int16_t x, int16_t y);

// Navigation
void handleUniversalBackButton();

// External objects
extern XPT2046_Touchscreen ts;

#endif // TOUCH_H
