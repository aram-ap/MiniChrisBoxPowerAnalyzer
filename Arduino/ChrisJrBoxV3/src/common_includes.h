/**
* @file common_includes.h
 * @brief Common include order to avoid conflicts
 */

#ifndef COMMON_INCLUDES_H
#define COMMON_INCLUDES_H

// Include types.h first to establish our types
#include "types.h"

// Then Arduino core
#include <Arduino.h>

// Then libraries that might have conflicts
#include <Keypad.h>

// Then QNEthernet (after Keypad to avoid CLOSED macro conflict)
#include <QNEthernet.h>

#endif // COMMON_INCLUDES_H
