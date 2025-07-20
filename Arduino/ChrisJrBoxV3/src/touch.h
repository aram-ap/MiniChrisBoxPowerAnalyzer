/**
 * @file touch.h
 * @brief Touch screen handling
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
void handleTouchGraph(int16_t x, int16_t y);
void handleTouchGraphSettings(int16_t x, int16_t y);
void handleTouchGraphDisplaySettings(int16_t x, int16_t y);
void handleTouchSnake(int16_t x, int16_t y);

// Navigation
void handleUniversalBackButton();

// External objects
extern XPT2046_Touchscreen ts;

#endif // TOUCH_H
