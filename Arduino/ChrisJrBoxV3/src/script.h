/**
* @file script.h
 * @brief Script execution system
 */

#ifndef SCRIPT_H
#define SCRIPT_H

#include <Arduino.h>
#include "types.h"

// Script initialization
void initScript();

// Script execution
void handleScriptExecution(unsigned long currentMillis);
void startScript();
void pauseScript();
void resumeScript();
void stopScript(bool userEnded);

// Script management
void loadScriptFromFile(const char* scriptName);
void saveCurrentScript();
void createNewScript();
bool loadAllScriptNames();
void sortScripts();
void deleteScript(const char* scriptName);
void updateScriptLastUsed(const char* scriptName);
void generateScriptFilename(char* buf, size_t buflen, const char* scriptName);

// External variables
extern Script currentScript;
extern bool isScriptRunning;
extern bool isScriptPaused;
extern long scriptTimeSeconds;
extern ScriptMetadata scriptList[];
extern int numScripts;
extern SortMode currentSortMode;

bool isScriptEndedEarly();

#endif // SCRIPT_H
