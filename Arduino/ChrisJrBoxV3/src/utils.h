/**
* @file utils.h
 * @brief General utility functions
 */

#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>
#include <Keypad.h>

// Keypad handling
void handleKeypadInput();
void handleKeypadInputChar(char key);

// External objects
extern Keypad keypad;

#endif // UTILS_H
