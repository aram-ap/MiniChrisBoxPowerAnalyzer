/**
* @file network_wrapper.h
 * @brief Wrapper to isolate QNEthernet from macro conflicts
 */

#ifndef NETWORK_WRAPPER_H
#define NETWORK_WRAPPER_H

// Save conflicting macro definitions
#ifdef CLOSED
  #define KEYPAD_CLOSED_WAS_DEFINED
  #undef CLOSED
#endif

#ifdef OPEN
  #define KEYPAD_OPEN_WAS_DEFINED
  #undef OPEN
#endif

// Include QNEthernet without conflicts
#include <QNEthernet.h>

// Restore macro definitions after QNEthernet
#ifdef KEYPAD_CLOSED_WAS_DEFINED
  #define CLOSED HIGH
  #undef KEYPAD_CLOSED_WAS_DEFINED
#endif

#ifdef KEYPAD_OPEN_WAS_DEFINED
  #define OPEN LOW
  #undef KEYPAD_OPEN_WAS_DEFINED
#endif

#endif // NETWORK_WRAPPER_H
