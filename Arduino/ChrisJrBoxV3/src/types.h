/**
 * @file types.h
 * @brief Type definitions and structures
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

#ifndef TYPES_H
#define TYPES_H

#include "config.h"

// Core Arduino types and constants must come first.
#include <Arduino.h>
#include <TimeLib.h> // For time_t
#include <Bounce2.h>

// Graph-related enums and structures
enum GraphDataType {
  GRAPH_CURRENT = 0,
  GRAPH_VOLTAGE = 1,
  GRAPH_POWER = 2
};

enum GraphTab {
  GRAPH_TAB_ALL = 0,
  GRAPH_TAB_GSE1 = 1,
  GRAPH_TAB_GSE2 = 2,
  GRAPH_TAB_TER = 3,
  GRAPH_TAB_TE1 = 4,
  GRAPH_TAB_TE2 = 5,
  GRAPH_TAB_TE3 = 6
};

#define GRAPH_COLORS_COUNT 8

struct DeviceGraphSettings {
  bool enabled;
  GraphDataType dataType;
  uint16_t lineColor;
  float axisRanges[3][2];  // Min/max for each data type
  bool autoScale;
};

struct AllGraphSettings {
  GraphDataType dataType;
  bool deviceEnabled[6];
  float axisRanges[3][2];  // Min/max for each data type
  bool autoScale;
  int lineThickness;
};

struct GraphSettings {
  DeviceGraphSettings devices[6];
  AllGraphSettings all;
  bool isPaused;
  bool autoScroll;
  bool showAxesLabels;
  float timeRange;
  float panOffsetX;
  float panOffsetY;
  bool enablePanning;
  bool autoFitEnabled = true;
  int effectiveMaxPoints = GRAPH_MAX_POINTS;
  unsigned long graphRefreshRate = GRAPH_UPDATE_INTERVAL;
  bool enableAntialiasing = false;
  bool enableInterpolation = false;
  float interpolationSmoothness = 1.0f;
  bool enableGaussianFilter = false;
  float interpolationTension = 0.0f;
  float interpolationCurveScale = 2.0f;
  int interpolationSubdiv = 32;
  bool showGrids = true;
  float pausedMinTime = 0.0f;
  float pausedMaxTime = 0.0f;
  uint32_t checksum;
};

// Network structures
struct NetworkConfig {
  bool enableEthernet = true;
  bool useDHCP = true;
  uint32_t staticIP = 0xC0A80164;
  uint32_t subnet = 0xFFFFFF00;
  uint32_t gateway = 0xC0A80101;
  uint32_t dns = 0x08080808;
  uint16_t tcpPort = 8080;
  uint16_t udpPort = 8081;
  uint32_t udpTargetIP = 0xFFFFFFFF;
  uint16_t udpTargetPort = 8082;
  unsigned long networkTimeout = 10000;
  unsigned long dhcpTimeout = 8000;
};

struct StreamConfig {
  bool usbStreamEnabled = false;
  bool tcpStreamEnabled = false;
  bool udpStreamEnabled = false;
  unsigned long streamInterval = 100;
  bool streamActiveOnly = false;
};

// GUI structures
enum GUIMode {
  MODE_MAIN, MODE_SETTINGS, MODE_NETWORK, MODE_NETWORK_EDIT,
  MODE_SCRIPT, MODE_SCRIPT_LOAD, MODE_EDIT, MODE_EDIT_LOAD, MODE_EDIT_FIELD,
  MODE_EDIT_SAVE, MODE_EDIT_NAME, MODE_DATE_TIME, MODE_KEYPAD,
  MODE_DELETE_CONFIRM, MODE_ABOUT, MODE_GRAPH, MODE_GRAPH_SETTINGS, MODE_GRAPH_DISPLAY,
  MODE_SNAKE
};

enum KeypadMode {
  KEYPAD_NONE, KEYPAD_UPDATE_RATE, KEYPAD_FAN_SPEED, KEYPAD_SCRIPT_TSTART,
  KEYPAD_SCRIPT_TEND, KEYPAD_DEVICE_ON_TIME, KEYPAD_DEVICE_OFF_TIME,
  KEYPAD_SCRIPT_SEARCH, KEYPAD_SCRIPT_NAME, KEYPAD_NETWORK_IP,
  KEYPAD_NETWORK_PORT, KEYPAD_NETWORK_TIMEOUT, KEYPAD_GRAPH_MIN_Y, KEYPAD_GRAPH_MAX_Y,
  KEYPAD_GRAPH_TIME_RANGE, KEYPAD_GRAPH_MAX_POINTS, KEYPAD_GRAPH_REFRESH_RATE,
  KEYPAD_GRAPH_INTERPOLATION_TENSION, KEYPAD_GRAPH_INTERPOLATION_CURVESCALE, KEYPAD_GRAPH_INTERPOLATION_SUBDIV
};

struct ButtonRegion {
  int x, y, w, h;
  const char* label;
  bool pressed;
  uint16_t color;
  bool enabled;
};

// Switch/Output structures
struct SwitchOutput {
  const char* name;
  int outputPin;
  int switchPin;
  Bounce debouncer;  // FIXED: Now has complete class definition
  bool state;
};

// Script structures
struct DeviceScript {
  bool enabled;
  int onTime;
  int offTime;
};

struct Script {
  char scriptName[32];
  bool useRecord;
  int tStart;
  int tEnd;
  DeviceScript devices[6];
  time_t dateCreated;
  time_t lastUsed;
};

struct ScriptMetadata {
  char name[32];
  char filename[32];
  time_t dateCreated;
  time_t lastUsed;
};

// Edit field structures
struct DeviceTimingField {
  int x, y, w, h;
  int deviceIndex;
  int fieldType;
  bool isSelected;
};

struct EditField {
  int x, y, w, h;
  char value[32];
  bool isSelected;
};

struct NetworkEditField {
  int x, y, w, h;
  char value[32];
  int fieldType;
  bool isSelected;
};

// Snake game structures
enum SnakeDirection {
  SNAKE_UP = 0,
  SNAKE_DOWN = 1,
  SNAKE_LEFT = 2,
  SNAKE_RIGHT = 3
};

struct SnakeSegment {
  int x, y;
};

struct SnakeGame {
  SnakeSegment segments[100];  // Maximum snake length
  int length;
  SnakeDirection direction;
  SnakeDirection nextDirection;
  int foodX, foodY;
  int score;
  bool gameRunning;
  bool gamePaused;
  bool gameOver;
  bool pausedByBackButton;  // Track if paused by B button
  bool newHighScore;  // Track if current game achieved new high score
  unsigned long lastMoveTime;
  unsigned long moveInterval;
  int maxScore;
};

// System state
struct SystemState {
  bool lock = false;
  bool safetyStop = false;
  bool lockBeforeStop = false;
  bool recording = false;
  bool recordingScript = false;
  bool sdAvailable = false;
  bool internalSdAvailable = false;
  bool serialAvailable = false;
  bool fanOn = false;
  bool use24HourFormat = true;
  bool darkMode = true;
  bool csvOutput = false;
  bool csvHeaderWritten = false;
  bool firstDataPoint = true;
  bool currentSDContext = false;
  int fanSpeed = 255;
  unsigned long updateRate = 100;
  unsigned long lastSensorUpdate = 0;
  unsigned long lastDisplayUpdate = 0;
  unsigned long lastLogWrite = 0;
  unsigned long lastTouchTime = 0;
  unsigned long lastSDCheck = 0;
  unsigned long lastClockRefresh = 0;
  unsigned long lastPowerLedBlink = 0;
  unsigned long recordStartMillis = 0;
  unsigned long lastGraphUpdate = 0;
  char recordFilename[64] = "power_data.json";
};

struct GUIState {
  GUIMode currentMode = MODE_MAIN;
  GUIMode previousMode = MODE_MAIN;
  KeypadMode keypadMode = KEYPAD_NONE;
  GraphTab currentGraphTab = GRAPH_TAB_ALL;
  bool isInGraphSettings = false;
  int numDeviceFields = 0;
  int selectedDeviceField = -1;
  int numEditFields = 0;
  int selectedField = -1;
  int numNetworkFields = 0;
  int selectedNetworkField = -1;
  int scriptListOffset = 0;
  int selectedScript = -1;
  int highlightedScript = -1;
  bool showDeleteConfirm = false;
  char deleteScriptName[32] = "";
  char keypadBuffer[32] = "";
  int keypadPos = 0;
  bool isEditingName = false;
  bool shiftMode = false;
  bool capsMode = false;
  bool alphaMode = false;
  char lastKey = '\0';
  unsigned long lastKeyTime = 0;
  int currentLetterIndex = 0;
  // Secret sequence tracking for snake game
  char secretSequence[4] = "";
  int secretSequencePos = 0;
  bool showSecretButton = false;
  unsigned long lastSecretKeyTime = 0;
};

enum SortMode {
  SORT_NAME,
  SORT_LAST_USED,
  SORT_DATE_CREATED
};

enum NetworkInitState {
  NET_IDLE,
  NET_CHECKING_LINK,
  NET_INITIALIZING,
  NET_DHCP_WAIT,
  NET_INITIALIZED,
  NET_FAILED
};

#endif // TYPES_H
