/**
 * @file types.h
 * @brief Type definitions and structures
 */

#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>
#include <Bounce2.h>
#include <TimeLib.h>

// Forward declarations
struct ButtonRegion;

// Network structures
struct NetworkConfig {
  bool enableEthernet = true;
  bool useDHCP = true;
  uint32_t staticIP = 0xC0A80164;    // 192.168.1.100
  uint32_t subnet = 0xFFFFFF00;      // 255.255.255.0
  uint32_t gateway = 0xC0A80101;     // 192.168.1.1
  uint32_t dns = 0x08080808;         // 8.8.8.8
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
  MODE_MAIN, MODE_SETTINGS, MODE_NETWORK, MODE_NETWORK_EDIT, MODE_KEYPAD,
  MODE_SCRIPT, MODE_SCRIPT_LOAD, MODE_EDIT, MODE_EDIT_LOAD, MODE_EDIT_SAVE,
  MODE_EDIT_FIELD, MODE_DATE_TIME, MODE_EDIT_NAME, MODE_DELETE_CONFIRM, MODE_ABOUT
};

enum KeypadMode {
  KEYPAD_NONE, KEYPAD_UPDATE_RATE, KEYPAD_FAN_SPEED, KEYPAD_SCRIPT_TSTART,
  KEYPAD_SCRIPT_TEND, KEYPAD_DEVICE_ON_TIME, KEYPAD_DEVICE_OFF_TIME,
  KEYPAD_SCRIPT_SEARCH, KEYPAD_SCRIPT_NAME, KEYPAD_NETWORK_IP,
  KEYPAD_NETWORK_PORT, KEYPAD_NETWORK_TIMEOUT
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
  Bounce debouncer;
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
  int fieldType; // 0=ON time, 1=OFF time, 2=checkbox
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
  int fieldType; // 0=IP, 1=Port, 2=Timeout
  bool isSelected;
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

  // Timing
  unsigned long lastSensorUpdate = 0;
  unsigned long lastDisplayUpdate = 0;
  unsigned long lastLogWrite = 0;
  unsigned long lastTouchTime = 0;
  unsigned long lastSDCheck = 0;
  unsigned long lastClockRefresh = 0;
  unsigned long lastPowerLedBlink = 0;
  unsigned long recordStartMillis = 0;

  char recordFilename[64] = "power_data.json";
};

struct GUIState {
  GUIMode currentMode = MODE_MAIN;
  GUIMode previousMode = MODE_MAIN;
  KeypadMode keypadMode = KEYPAD_NONE;

  // Edit fields
  int numDeviceFields = 0;
  int selectedDeviceField = -1;
  int numEditFields = 0;
  int selectedField = -1;
  int numNetworkFields = 0;
  int selectedNetworkField = -1;

  // Script list
  int scriptListOffset = 0;
  int selectedScript = -1;
  int highlightedScript = -1;
  bool showDeleteConfirm = false;
  char deleteScriptName[32] = "";

  // Text input
  char keypadBuffer[32] = "";
  int keypadPos = 0;
  bool isEditingName = false;
  bool shiftMode = false;
  bool capsMode = false;
  bool alphaMode = false;
  char lastKey = '\0';
  unsigned long lastKeyTime = 0;
  int currentLetterIndex = 0;
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
