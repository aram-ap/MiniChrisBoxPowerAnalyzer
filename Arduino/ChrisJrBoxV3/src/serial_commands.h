/**
* @file serial_commands.h
 * @brief Serial command processing
 */

#ifndef SERIAL_COMMANDS_H
#define SERIAL_COMMANDS_H

#include <Arduino.h>

// Serial command processing
void processSerialCommands();
void handleCommand(String command);

// Status and help
void printCurrentStatus();
void printHelp();
void serialPrint(String message);

#endif // SERIAL_COMMANDS_H
