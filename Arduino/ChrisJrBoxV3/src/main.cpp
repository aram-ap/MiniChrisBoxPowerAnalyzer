/**
 * @file main.cpp
 * @brief Network-enabled power controller for Teensy 4.1 with touchscreen interface. Built for Mini Chris Box V4-5.x.
 * @author Aram Aprahamian
 * @version 5.1
 * @date July 14, 2025
 * 
 * Features:
 * - 6-channel power switching with INA226 current monitoring
 * - 4.8" touchscreen display with ST7796S controller
 * - Ethernet connectivity with TCP/UDP communication
 * - SD card data logging (external and internal)
 * - Script-based automation system
 * - Real-time clock integration
 * - Fan control and environmental monitoring
 */

#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
#include <QNEthernet.h>
#include <Bounce2.h>
#include <INA226.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7796S.h>
#include <XPT2046_Touchscreen.h>
#include <SD.h>
#include <SPI.h>
#include <Keypad.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <TimeLib.h>
#include <DS1307RTC.h>
#include <ArduinoJson.h>

using namespace qindesign::network;

// ==================== System Configuration ====================
const char* SOFTWARE_VERSION = "Mini Chris Box V5.1 - Network Enabled";

// EEPROM validation constants
#define EEPROM_MAGIC_NUMBER 0xDEADBEEF
#define EEPROM_MAGIC_ADDR 0
#define EEPROM_VERSION_ADDR 4
#define EEPROM_VERSION_NUMBER 2

// Performance timing constants
#define SENSOR_UPDATE_INTERVAL 50
#define DISPLAY_UPDATE_INTERVAL 200
#define LOG_WRITE_INTERVAL 50

// Hardware pin definitions
const int pwrLed = 22;
const int lockLed = 21;
const int stopLed = 20;
#define FAN_PWM_PIN 33

// Display and touch pins
#define TFT_CS 10
#define TFT_DC 9
#define TFT_RST 7
#define TOUCH_CS 8
#define TOUCH_IRQ 14

// SD card pins
#define SD_CS 36
#define BUILTIN_SDCARD 254
#define SCRIPTS_DIR "/scripts"

// Screen dimensions
const int SCREEN_WIDTH = 480;
const int SCREEN_HEIGHT = 320;

// ==================== Color Definitions ====================
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_YELLOW    0xFFE0
#define COLOR_PURPLE    0x780F
#define COLOR_BTN_BG    0x2104
#define COLOR_BTN_DARK  0x18A3
#define COLOR_BTN_PRESS 0x3186
#define COLOR_RECORD    0x001F
#define COLOR_RECORDING 0xF800
#define COLOR_GRAY      0x8410
#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_CYAN      0x07FF
#define COLOR_BLUE      0x001F
#define COLOR_ORANGE    0xFD20
#define COLOR_DARK_ROW1 0x2104
#define COLOR_DARK_ROW2 0x18C3
#define COLOR_LIST_ROW1 0x1082
#define COLOR_LIST_ROW2 0x0841

// ==================== Network Configuration ====================
struct NetworkConfig {
  bool enableEthernet = true;
  bool useDHCP = true;
  uint32_t staticIP = 0xC0A80164;    // 192.168.1.100
  uint32_t subnet = 0xFFFFFF00;     // 255.255.255.0
  uint32_t gateway = 0xC0A80101;    // 192.168.1.1
  uint32_t dns = 0x08080808;        // 8.8.8.8
  uint16_t tcpPort = 8080;
  uint16_t udpPort = 8081;
  uint32_t udpTargetIP = 0xFFFFFFFF;
  uint16_t udpTargetPort = 8082;
  unsigned long networkTimeout = 10000;
  unsigned long dhcpTimeout = 8000;
} networkConfig;

// Network state variables
EthernetServer tcpServer(8080);
EthernetUDP udp;
EthernetClient tcpClients[5];
bool networkInitialized = false;
bool ethernetConnected = false;
unsigned long lastNetworkCheck = 0;
const unsigned long NETWORK_CHECK_INTERVAL = 5000;

// Network initialization state machine
enum NetworkInitState {
  NET_IDLE, NET_CHECKING_LINK, NET_INITIALIZING, 
  NET_DHCP_WAIT, NET_INITIALIZED, NET_FAILED
};
NetworkInitState networkInitState = NET_IDLE;
unsigned long networkInitStartTime = 0;
unsigned long lastInitScreenUpdate = 0;
String lastInitStatusText = "";

// Data streaming configuration
struct StreamConfig {
  bool usbStreamEnabled = false;
  bool tcpStreamEnabled = false;
  bool udpStreamEnabled = false;
  unsigned long streamInterval = 100;
  bool streamActiveOnly = false;
} streamConfig;

unsigned long lastStreamTime = 0;
bool streamingActive = false;
unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL = 10000;
bool heartbeatEnabled = true;
char responseBuffer[1024];

// ==================== Hardware Objects ====================
Adafruit_ST7796S tft = Adafruit_ST7796S(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);
File logFile;

// INA226 current/voltage sensors
INA226 ina_gse1(0x40);
INA226 ina_gse2(0x41);
INA226 ina_ter(0x42);
INA226 ina_te1(0x43);
INA226 ina_te2(0x44);
INA226 ina_te3(0x45);
INA226 ina_all(0x46);
INA226* inaDevices[] = { &ina_gse1, &ina_gse2, &ina_ter, &ina_te1, &ina_te2, &ina_te3, &ina_all };
const char* inaNames[] = { "GSE-1", "GSE-2", "TE-R", "TE-1", "TE-2", "TE-3", "Bus" };
const int numIna = 7;
float deviceVoltage[numIna];
float deviceCurrent[numIna];
float devicePower[numIna];

// ==================== System State Variables ====================
bool lock = false;
bool safetyStop = false;
bool lockBeforeStop = false;
bool recording = false;
bool recordingScript = false;
bool sdAvailable = false;
bool internalSdAvailable = false;
bool currentSDContext = false;
bool serialAvailable = false;
bool fanOn = false;
bool use24HourFormat = true;
bool darkMode = true;
bool csvOutput = false;
bool csvHeaderWritten = false;
bool firstDataPoint = true;

// Timing variables
unsigned long lastSensorUpdate = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastLogWrite = 0;
unsigned long lastTouchTime = 0;
unsigned long lastSDCheck = 0;
unsigned long lastClockRefresh = 0;
unsigned long lastPowerLedBlink = 0;
unsigned long recordStartMillis = 0;
unsigned long SD_CHECK_INTERVAL = 2000;
unsigned long updateRate = 100;
const unsigned long touchDebounceMs = 200;

// Settings
int fanSpeed = 255;
bool powerLedState = false;
String serialBuffer = "";
String networkCommandBuffer = "";
char recordFilename[64] = "power_data.json";

// ==================== Switch/Output Configuration ====================
struct SwitchOutput {
  const char* name;
  int outputPin;
  int switchPin;
  Bounce debouncer;
  bool state;
};

SwitchOutput switchOutputs[] = {
  { "GSE-1", 0,  41, Bounce(), LOW },
  { "GSE-2", 5,  15, Bounce(), LOW },
  { "TE-R",  1,  40, Bounce(), LOW },
  { "TE-1",  2,  39, Bounce(), LOW },
  { "TE-2",  3,  38, Bounce(), LOW },
  { "TE-3",  4,  24, Bounce(), LOW }
};
const int numSwitches = sizeof(switchOutputs) / sizeof(SwitchOutput);

// ==================== Script System ====================
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

Script currentScript;
bool isScriptRunning = false;
bool isScriptPaused = false;
unsigned long scriptStartMillis = 0;
unsigned long scriptPausedTime = 0;
unsigned long pauseStartMillis = 0;
bool scriptEndedEarly = false;
bool lockStateBeforeScript = false;
bool deviceOnTriggered[6] = {false, false, false, false, false, false};
bool deviceOffTriggered[6] = {false, false, false, false, false, false};
unsigned long lastLockBlink = 0;
bool lockLedState = false;
long scriptTimeSeconds = 0;

// ==================== GUI System ====================
enum GUIMode {
  MODE_MAIN, MODE_SETTINGS, MODE_NETWORK, MODE_NETWORK_EDIT, MODE_KEYPAD,
  MODE_SCRIPT, MODE_SCRIPT_LOAD, MODE_EDIT, MODE_EDIT_LOAD, MODE_EDIT_SAVE,
  MODE_EDIT_FIELD, MODE_DATE_TIME, MODE_EDIT_NAME, MODE_DELETE_CONFIRM, MODE_ABOUT
};
GUIMode currentMode = MODE_MAIN;
GUIMode previousMode = MODE_MAIN;

// Button structure
struct ButtonRegion {
  int x, y, w, h;
  const char* label;
  bool pressed;
  uint16_t color;
  bool enabled;
};

// Main screen buttons
ButtonRegion btnRecord     = {  5,  5,   120, 35, "RECORD",    false, COLOR_RECORD,    false };
ButtonRegion btnSDRefresh  = {130,  5,    40, 35, "SD",        false, COLOR_CYAN,      true };
ButtonRegion btnStop       = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, COLOR_YELLOW, true };
ButtonRegion btnLock       = { SCREEN_WIDTH - 70, SCREEN_HEIGHT - 40, 65, 35, "LOCK",  false, COLOR_YELLOW, true };
ButtonRegion btnAllOn      = {  5,  SCREEN_HEIGHT - 40, 80, 35, "ALL ON", false, COLOR_YELLOW, true };
ButtonRegion btnAllOff     = { 90,  SCREEN_HEIGHT - 40, 80, 35, "ALL OFF",false, COLOR_YELLOW, true };
ButtonRegion btnScript     = {175,  SCREEN_HEIGHT - 40, 60, 35, "Script", false, COLOR_YELLOW, true };
ButtonRegion btnEdit       = {240,  SCREEN_HEIGHT - 40, 60, 35, "Edit",   false, COLOR_YELLOW, true };
ButtonRegion btnSettings   = {305,  SCREEN_HEIGHT - 40, 80, 35, "Settings",false, COLOR_YELLOW, true };

// Settings panel buttons
ButtonRegion btnSettingsBack = { 5, 5, 80, 35, "Back", false, COLOR_YELLOW, true };
ButtonRegion btnSettingsStop = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, COLOR_YELLOW, true };
ButtonRegion btnNetwork = { 310, SCREEN_HEIGHT - 40, 78, 35, "Network", false, COLOR_YELLOW, true };
ButtonRegion btnAbout = { 390, SCREEN_HEIGHT - 40, 80, 35, "About", false, COLOR_YELLOW, true };

// Settings input buttons
ButtonRegion btnFanSpeedInput = { 320, 70, 80, 30, "", false, COLOR_YELLOW, true };
ButtonRegion btnUpdateRateInput = { 320, 110, 80, 30, "", false, COLOR_YELLOW, true };
ButtonRegion btnSetTimeDate = { 320, 150, 80, 30, "Set", false, COLOR_YELLOW, true };
ButtonRegion btnTimeFormatToggle = { 320, 190, 80, 30, "24H", false, COLOR_YELLOW, true };
ButtonRegion btnDarkModeToggle = { 320, 230, 80, 30, "ON", false, COLOR_YELLOW, true };

// Network settings buttons
ButtonRegion btnNetworkBack = { 5, 5, 80, 35, "Back", false, COLOR_YELLOW, true };
ButtonRegion btnNetworkStop = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, COLOR_YELLOW, true };
ButtonRegion btnNetworkEdit = { 320, SCREEN_HEIGHT - 40, 80, 35, "Edit", false, COLOR_YELLOW, true };
ButtonRegion btnEnableLanToggle = { 320, 70, 80, 30, "ON", false, COLOR_YELLOW, true };

// Network edit buttons
ButtonRegion btnNetworkEditBack = { 5, 5, 80, 35, "Back", false, COLOR_YELLOW, true };
ButtonRegion btnNetworkEditStop = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, COLOR_YELLOW, true };
ButtonRegion btnNetworkEditSave = { 320, SCREEN_HEIGHT - 40, 80, 35, "Save", false, COLOR_GREEN, true };
ButtonRegion btnDhcpToggle = { 320, 70, 80, 30, "ON", false, COLOR_YELLOW, true };

// About page buttons
ButtonRegion btnAboutBack = { 5, 5, 80, 35, "Back", false, COLOR_YELLOW, true };
ButtonRegion btnAboutStop = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, COLOR_YELLOW, true };

// Script panel buttons
ButtonRegion btnScriptBack   = {  5, 5, 80, 35, "Back", false, COLOR_YELLOW, true };
ButtonRegion btnScriptStop   = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, COLOR_YELLOW, true };
ButtonRegion btnScriptLoad   = {  5,  SCREEN_HEIGHT - 40, 60, 35, "Load", false, COLOR_YELLOW, true };
ButtonRegion btnScriptEdit   = { 70,  SCREEN_HEIGHT - 40, 60, 35, "Edit", false, COLOR_YELLOW, true };
ButtonRegion btnScriptStart  = {135, SCREEN_HEIGHT - 40, 70, 35, "Start", false, COLOR_GREEN,  true };
ButtonRegion btnScriptEnd    = {210, SCREEN_HEIGHT - 40, 50, 35,  "Stop",  false, COLOR_RED,    true };
ButtonRegion btnScriptRecord = {265, SCREEN_HEIGHT - 40, 70, 35, "Record", false, COLOR_BLUE,  true };

// Edit panel buttons
ButtonRegion btnEditBack     = {  5, 5, 80, 35, "Back", false, COLOR_YELLOW, true };
ButtonRegion btnEditStop     = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, COLOR_YELLOW, true };
ButtonRegion btnEditLoad     = {  5,  SCREEN_HEIGHT - 40, 80, 35, "Load", false, COLOR_YELLOW, true };
ButtonRegion btnEditSave     = {  90, SCREEN_HEIGHT - 40, 80, 35, "Save", false, COLOR_YELLOW, true };
ButtonRegion btnEditNew      = { 175, SCREEN_HEIGHT - 40, 80, 35, "New",  false, COLOR_YELLOW, true };

// Additional UI buttons
ButtonRegion btnKeypadBack = {5, 5, 80, 35, "Back", false, COLOR_YELLOW, true};
ButtonRegion btnEditSaveBack = {5, 5, 80, 35, "Back", false, COLOR_YELLOW, true};
ButtonRegion btnEditNameBack = {5, 5, 80, 35, "Back", false, COLOR_YELLOW, true};
ButtonRegion btnDateTimeBack = {5, 5, 80, 35, "Back", false, COLOR_YELLOW, true};
ButtonRegion btnEditFieldBack = {5, 5, 80, 35, "Back", false, COLOR_YELLOW, true};
ButtonRegion btnScriptSelect = {SCREEN_WIDTH - 85, SCREEN_HEIGHT - 40, 80, 35, "Select", false, COLOR_GREEN, true};
ButtonRegion btnScriptDelete = {SCREEN_WIDTH - 170, SCREEN_HEIGHT - 40, 80, 35, "Delete", false, COLOR_RED, true};
ButtonRegion btnSortDropdown = {SCREEN_WIDTH - 100, 5, 95, 35, "Name", false, COLOR_YELLOW, true};
ButtonRegion btnDeleteYes = {150, 150, 80, 35, "Yes", false, COLOR_RED, true};
ButtonRegion btnDeleteNo = {250, 150, 80, 35, "No", false, COLOR_YELLOW, true};

// ==================== Edit Field Structures ====================
struct DeviceTimingField {
  int x, y, w, h;
  int deviceIndex;
  int fieldType; // 0=ON time, 1=OFF time, 2=checkbox
  bool isSelected;
};

#define MAX_DEVICE_FIELDS 30
DeviceTimingField deviceFields[MAX_DEVICE_FIELDS];
int numDeviceFields = 0;
int selectedDeviceField = -1;

struct EditField {
  int x, y, w, h;
  char value[32];
  bool isSelected;
};

#define MAX_EDIT_FIELDS 10
EditField editFields[MAX_EDIT_FIELDS];
int numEditFields = 0;
int selectedField = -1;

struct NetworkEditField {
  int x, y, w, h;
  char value[32];
  int fieldType; // 0=IP, 1=Port, 2=Timeout
  bool isSelected;
};

#define MAX_NETWORK_FIELDS 10
NetworkEditField networkFields[MAX_NETWORK_FIELDS];
int numNetworkFields = 0;
int selectedNetworkField = -1;

// ==================== Script Management ====================
struct ScriptMetadata {
  char name[32];
  char filename[32];
  time_t dateCreated;
  time_t lastUsed;
};

#define MAX_SCRIPTS 50
ScriptMetadata scriptList[MAX_SCRIPTS];
int numScripts = 0;
int scriptListOffset = 0;
int selectedScript = -1;
int highlightedScript = -1;

enum SortMode { SORT_NAME, SORT_LAST_USED, SORT_DATE_CREATED };
SortMode currentSortMode = SORT_NAME;

bool showDeleteConfirm = false;
char deleteScriptName[32] = "";
char currentEditName[32] = "Untitled";
bool isEditingName = false;
bool shiftMode = false;
bool capsMode = false;
bool alphaMode = false;

// T9 keypad variables
char lastKey = '\0';
unsigned long lastKeyTime = 0;
int currentLetterIndex = 0;
const unsigned long T9_TIMEOUT = 300;

// ==================== Keypad System ====================
enum KeypadMode {
  KEYPAD_NONE, KEYPAD_UPDATE_RATE, KEYPAD_FAN_SPEED, KEYPAD_SCRIPT_TSTART,
  KEYPAD_SCRIPT_TEND, KEYPAD_DEVICE_ON_TIME, KEYPAD_DEVICE_OFF_TIME,
  KEYPAD_SCRIPT_SEARCH, KEYPAD_SCRIPT_NAME, KEYPAD_NETWORK_IP,
  KEYPAD_NETWORK_PORT, KEYPAD_NETWORK_TIMEOUT
};
KeypadMode keypadMode = KEYPAD_NONE;

const char keys[4][4] = {
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '*', '0', '#', 'D' }
};
byte rowPins[4] = { 28, 27, 26, 25 };
byte colPins[4] = { 32, 31, 30, 29 };
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, 4, 4);
char keypadBuffer[32] = "";
int keypadPos = 0;

// T9 keypad character mapping
const char* t9Letters[] = {
  "-_ ", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz", ""
};

// ==================== EEPROM Memory Map ====================
#define EEPROM_FAN_ON_ADDR     8
#define EEPROM_FAN_SPEED_ADDR  12
#define EEPROM_UPDATE_RATE_ADDR 16
#define EEPROM_TIME_FORMAT_ADDR 20
#define EEPROM_DARK_MODE_ADDR  24
#define EEPROM_NETWORK_CONFIG_ADDR 28
#define EEPROM_SORT_MODE_ADDR  60

// Date/time editing
tmElements_t tmSet;

// ==================== Function Prototypes ====================
// System initialization
void initializeEEPROM();
void initRTC();
void initNetworkBackground();
void updateNetworkInit();

// Network functions
void handleNetworkCommunication();
void handleTCPClients();
void handleUDPCommunication();
void processNetworkCommand(String command, Print* responseOutput);
void sendLiveDataStream();
void sendHeartbeat();
void checkNetworkStatus();
void saveNetworkConfig();
void loadNetworkConfig();
void saveNetworkFieldToConfig(int fieldIndex, const char* value);
void loadNetworkFieldsFromConfig();
bool generateLiveDataJSON(char* buffer, size_t bufferSize);
bool generateStatusJSON(char* buffer, size_t bufferSize);
bool generateScriptListJSON(char* buffer, size_t bufferSize);
void sendResponse(const char* response, Print* output);

// Data acquisition and logging
void updateSensorData();
void updateDisplayElements();
void recordDataDirect();
void startRecording(bool scriptRequested=false);
void stopRecording();

// SD card management
void ensureExternalSDContext();
void ensureInternalSDContext();
void smartCheckSDCard();
void checkInternalSD();

// Script system
void handleScripts();
void startScript();
void pauseScript();
void resumeScript();
void stopScript(bool userEnded);
void loadScriptFromFile(const char* scriptName);
void saveCurrentScript();
void createNewScript();
bool loadAllScriptNames();
void sortScripts();
void deleteScript(const char* scriptName);
void updateScriptLastUsed(const char* scriptName);
void generateScriptFilename(char* buf, size_t buflen, const char* scriptName);
void syncSwitchesToOutputs();

// GUI and display
void drawButton(ButtonRegion& btn, uint16_t bgColor, uint16_t textColor,
                const char* label, bool pressed=false, bool enabled=true);
void drawMainScreen();
void drawSettingsPanel();
void drawNetworkPanel();
void drawNetworkEditPanel();
void drawKeypadPanel();
void drawScriptPage();
void drawEditPage();
void drawScriptLoadPage();
void drawEditSavePage();
void drawDateTimePanel();
void drawDeleteConfirmDialog();
void drawAboutPage();
void drawInitializationScreen();
void updateInitializationScreen();
void drawDeviceRow(int row);
void drawTotalRow();
void updateLiveValueRow(int row);
void refreshHeaderClock();
void updateLockButton();

// Touch handling
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
void handleUniversalBackButton();

// Settings and EEPROM
void saveSettingsToEEPROM();
void loadSettingsFromEEPROM();
void applyFanSettings();
void applyUpdateRate();
void applyDarkMode();

// Serial communication
void processSerialCommands();
void handleCommand(String command);
void setOutputState(String deviceName, bool state);
void printCurrentStatus();
void printHelp();
void serialPrint(String message);

// Input handling
void handleKeypadInput(char key);

// Utility functions
int getInaIndexForSwitch(int switchIdx);
int findSwitchIndex(String deviceName);
void setAllOutputsOff();
void syncOutputsToSwitches();
void nextAvailableFilename(char* buf, size_t buflen);
bool touchInButton(int16_t x, int16_t y, const ButtonRegion& btn);
time_t getTeensyTime();
void setDateTime(tmElements_t tm);
String getCurrentTimeString();
String formatTimeHHMMSS(time_t t);
String formatDateString(time_t t);
String formatShortDateTime(time_t t);
IPAddress uint32ToIP(uint32_t ip);
uint32_t ipToUint32(IPAddress ip);
String ipToString(IPAddress ip);

// ==================== Network Helper Functions ====================

/**
 * @brief Convert 32-bit integer to IPAddress object
 * @param ip 32-bit representation of IP address
 * @return IPAddress object
 */
IPAddress uint32ToIP(uint32_t ip) {
  return IPAddress((ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
}

/**
 * @brief Convert IPAddress object to 32-bit integer
 * @param ip IPAddress object
 * @return 32-bit representation of IP address
 */
uint32_t ipToUint32(IPAddress ip) {
  return ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) | ((uint32_t)ip[2] << 8) | (uint32_t)ip[3];
}

/**
 * @brief Convert IPAddress to string representation
 * @param ip IPAddress object
 * @return String in dotted decimal notation
 */
String ipToString(IPAddress ip) {
  return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
}

// ==================== EEPROM Management ====================

/**
 * @brief Initialize EEPROM with default values if not previously configured
 */
void initializeEEPROM() {
  uint32_t magic;
  uint32_t version;

  EEPROM.get(EEPROM_MAGIC_ADDR, magic);
  EEPROM.get(EEPROM_VERSION_ADDR, version);

  if (magic != EEPROM_MAGIC_NUMBER || version != EEPROM_VERSION_NUMBER) {
    Serial.println("Initializing EEPROM with default values...");

    // Set validation markers
    EEPROM.put(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_NUMBER);
    EEPROM.put(EEPROM_VERSION_ADDR, EEPROM_VERSION_NUMBER);

    // Initialize default system settings
    fanOn = false;
    fanSpeed = 255;
    updateRate = 100;
    use24HourFormat = true;
    darkMode = true;

    // Initialize network configuration
    networkConfig.enableEthernet = true;
    networkConfig.useDHCP = true;
    networkConfig.staticIP = 0xC0A80164;
    networkConfig.subnet = 0xFFFFFF00;
    networkConfig.gateway = 0xC0A80101;
    networkConfig.dns = 0x08080808;
    networkConfig.tcpPort = 8080;
    networkConfig.udpPort = 8081;
    networkConfig.udpTargetIP = 0xFFFFFFFF;
    networkConfig.udpTargetPort = 8082;
    networkConfig.networkTimeout = 10000;
    networkConfig.dhcpTimeout = 8000;

    saveSettingsToEEPROM();
    saveNetworkConfig();

    Serial.println("EEPROM initialized with default values");
  } else {
    Serial.println("EEPROM already initialized, loading settings...");
    loadSettingsFromEEPROM();
    loadNetworkConfig();
  }
}

/**
 * @brief Save current settings to EEPROM
 */
void saveSettingsToEEPROM() {
  EEPROM.put(EEPROM_FAN_ON_ADDR, fanOn);
  EEPROM.put(EEPROM_FAN_SPEED_ADDR, fanSpeed);
  EEPROM.put(EEPROM_UPDATE_RATE_ADDR, updateRate);
  EEPROM.put(EEPROM_TIME_FORMAT_ADDR, use24HourFormat);
  EEPROM.put(EEPROM_DARK_MODE_ADDR, darkMode);
  EEPROM.put(EEPROM_SORT_MODE_ADDR, (int)currentSortMode);
}

/**
 * @brief Load settings from EEPROM with validation
 */
void loadSettingsFromEEPROM() {
  EEPROM.get(EEPROM_FAN_ON_ADDR, fanOn);
  EEPROM.get(EEPROM_FAN_SPEED_ADDR, fanSpeed);
  EEPROM.get(EEPROM_UPDATE_RATE_ADDR, updateRate);
  EEPROM.get(EEPROM_TIME_FORMAT_ADDR, use24HourFormat);
  EEPROM.get(EEPROM_DARK_MODE_ADDR, darkMode);

  // Load and validate sort mode
  int sortMode;
  EEPROM.get(EEPROM_SORT_MODE_ADDR, sortMode);
  if (sortMode >= 0 && sortMode <= 2) {
    currentSortMode = (SortMode)sortMode;
  } else {
    currentSortMode = SORT_NAME;
  }

  // Validate loaded values
  if (fanSpeed < 0 || fanSpeed > 255) fanSpeed = 255;
  if (updateRate < 10 || updateRate > 5000) updateRate = 100;
}

// ==================== Network Implementation ====================

/**
 * @brief Initialize network hardware in non-blocking mode
 */
void initNetworkBackground() {
  if (!networkConfig.enableEthernet) {
    Serial.println("Ethernet disabled in settings");
    networkInitialized = false;
    ethernetConnected = false;
    networkInitState = NET_FAILED;
    return;
  }

  Serial.println("Initializing Ethernet...");
  networkInitState = NET_CHECKING_LINK;
  networkInitStartTime = millis();

  if (!Ethernet.begin()) {
    Serial.println("Failed to initialize Ethernet hardware");
    networkInitState = NET_FAILED;
    ethernetConnected = false;
    networkInitialized = false;
    return;
  }

  networkInitState = NET_INITIALIZING;
}

/**
 * @brief Update network initialization state machine
 */
void updateNetworkInit() {
  if (networkInitState != NET_CHECKING_LINK && networkInitState != NET_INITIALIZING && networkInitState != NET_DHCP_WAIT) return;

  // Check for initialization timeout
  if (millis() - networkInitStartTime > networkConfig.networkTimeout) {
    Serial.println("Network initialization timed out");
    networkInitState = NET_FAILED;
    networkInitialized = false;
    ethernetConnected = false;
    return;
  }

  // Check physical link status
  if (networkInitState == NET_CHECKING_LINK) {
    if (Ethernet.linkStatus() == LinkOFF) {
      Serial.println("No ethernet cable detected");
      networkInitState = NET_FAILED;
      networkInitialized = false;
      ethernetConnected = false;
      return;
    }
    networkInitState = NET_INITIALIZING;
  }

  // Configure network parameters
  if (networkInitState == NET_INITIALIZING) {
    if (networkConfig.useDHCP) {
      Serial.println("Starting DHCP...");
      networkInitState = NET_DHCP_WAIT;
    } else {
      Serial.println("Using static IP configuration...");
      Ethernet.begin(uint32ToIP(networkConfig.staticIP), uint32ToIP(networkConfig.subnet), uint32ToIP(networkConfig.gateway));
      Ethernet.setDNSServerIP(uint32ToIP(networkConfig.dns));
      networkInitState = NET_INITIALIZED;
    }
  }

  // Monitor DHCP acquisition
  if (networkInitState == NET_DHCP_WAIT) {
    if (Ethernet.localIP() != INADDR_NONE) {
      networkInitState = NET_INITIALIZED;
    } else if (millis() - networkInitStartTime > networkConfig.dhcpTimeout) {
      Serial.println("DHCP timeout, falling back to static IP");
      Ethernet.begin(uint32ToIP(networkConfig.staticIP), uint32ToIP(networkConfig.subnet), uint32ToIP(networkConfig.gateway));
      Ethernet.setDNSServerIP(uint32ToIP(networkConfig.dns));
      networkInitState = NET_INITIALIZED;
    }
  }

  // Complete initialization
  if (networkInitState == NET_INITIALIZED) {
    tcpServer.begin(networkConfig.tcpPort);
    udp.begin(networkConfig.udpPort);
    ethernetConnected = true;
    networkInitialized = true;

    Serial.print("Ethernet initialized. IP: ");
    Serial.println(ipToString(Ethernet.localIP()));
    Serial.print("TCP Server listening on port: ");
    Serial.println(networkConfig.tcpPort);
    Serial.print("UDP listening on port: ");
    Serial.println(networkConfig.udpPort);
  }
}

/**
 * @brief Monitor network connection status
 */
void checkNetworkStatus() {
  if (!networkConfig.enableEthernet || !networkInitialized) return;

  unsigned long currentMillis = millis();
  if (currentMillis - lastNetworkCheck < NETWORK_CHECK_INTERVAL) return;

  lastNetworkCheck = currentMillis;

  if (Ethernet.linkStatus() == LinkOFF) {
    if (ethernetConnected) {
      Serial.println("Ethernet cable disconnected");
      ethernetConnected = false;
    }
    return;
  }

  if (!ethernetConnected) {
    Serial.println("Ethernet cable connected - reinitializing...");
    initNetworkBackground();
  }
}

/**
 * @brief Handle all network communication tasks
 */
void handleNetworkCommunication() {
  if (!networkInitialized || !ethernetConnected) return;

  handleTCPClients();
  handleUDPCommunication();

  // Send periodic heartbeat
  if (heartbeatEnabled && (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL)) {
    sendHeartbeat();
    lastHeartbeat = millis();
  }
}

/**
 * @brief Handle TCP client connections and data
 */
void handleTCPClients() {
  // Accept new client connections
  EthernetClient newClient = tcpServer.accept();
  if (newClient) {
    // Find available slot for new client
    for (int i = 0; i < 5; i++) {
      if (!tcpClients[i] || !tcpClients[i].connected()) {
        tcpClients[i] = newClient;
        Serial.print("New TCP client connected: ");
        Serial.println(ipToString(newClient.remoteIP()));

        // Send connection acknowledgment
        JsonDocument welcomeDoc;
        welcomeDoc["type"] = "connection";
        welcomeDoc["status"] = "connected";
        welcomeDoc["version"] = SOFTWARE_VERSION;
        welcomeDoc["timestamp"] = getCurrentTimeString();
        serializeJson(welcomeDoc, tcpClients[i]);
        tcpClients[i].println();
        break;
      }
    }
  }

  // Process data from existing clients
  for (int i = 0; i < 5; i++) {
    if (tcpClients[i] && tcpClients[i].connected()) {
      while (tcpClients[i].available()) {
        char c = tcpClients[i].read();
        if (c == '\n' || c == '\r') {
          if (networkCommandBuffer.length() > 0) {
            processNetworkCommand(networkCommandBuffer, &tcpClients[i]);
            networkCommandBuffer = "";
          }
        } else {
          networkCommandBuffer += c;
          if (networkCommandBuffer.length() > 512) {
            networkCommandBuffer = "";
          }
        }
      }
    }
  }
}

/**
 * @brief Handle UDP packet communication
 */
void handleUDPCommunication() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char packetBuffer[512];
    int len = udp.read(packetBuffer, sizeof(packetBuffer) - 1);
    if (len > 0) {
      packetBuffer[len] = '\0';
      String command = String(packetBuffer);
      command.trim();

      // Store sender information for response
      IPAddress remoteIP = udp.remoteIP();
      uint16_t remotePort = udp.remotePort();

      // Send response back to sender
      udp.beginPacket(remoteIP, remotePort);
      processNetworkCommand(command, &udp);
      udp.endPacket();
    }
  }
}

/**
 * @brief Process network commands (JSON format)
 * @param command Command string to process
 * @param responseOutput Output stream for response
 */
void processNetworkCommand(String command, Print* responseOutput) {
  command.trim();

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, command);

  if (error) {
    // Fall back to legacy text commands
    handleCommand(command);
    return;
  }

  String cmd = doc["cmd"].as<String>();

  // Device control commands
  if (cmd == "set_output") {
    String device = doc["device"].as<String>();
    bool state = doc["state"].as<bool>();
    setOutputState(device, state);

    JsonDocument response;
    response["type"] = "command_response";
    response["cmd"] = "set_output";
    response["device"] = device;
    response["state"] = state;
    response["success"] = true;
    serializeJson(response, *responseOutput);
    responseOutput->println();
  }
  else if (cmd == "all_outputs") {
    bool state = doc["state"].as<bool>();
    if (isScriptRunning) {
      sendResponse("{\"type\":\"error\",\"message\":\"Cannot change outputs - script is running\"}", responseOutput);
      return;
    }

    if (state) {
      lock = true;
      updateLockButton();
      for (int i = 0; i < numSwitches; i++) {
        digitalWrite(switchOutputs[i].outputPin, HIGH);
        switchOutputs[i].state = HIGH;
      }
    } else {
      setAllOutputsOff();
    }

    JsonDocument response;
    response["type"] = "command_response";
    response["cmd"] = "all_outputs";
    response["state"] = state;
    response["success"] = true;
    serializeJson(response, *responseOutput);
    responseOutput->println();
  }
  // System control commands
  else if (cmd == "lock") {
    bool lockState = doc["state"].as<bool>();
    bool prevLock = lock;
    lock = lockState;
    updateLockButton();
    if (!lock && prevLock) syncOutputsToSwitches();

    JsonDocument response;
    response["type"] = "command_response";
    response["cmd"] = "lock";
    response["state"] = lock;
    response["success"] = true;
    serializeJson(response, *responseOutput);
    responseOutput->println();
  }
  else if (cmd == "safety_stop") {
    bool stopState = doc["state"].as<bool>();
    if (stopState && !safetyStop) {
      lockBeforeStop = lock;
      safetyStop = true;
      setAllOutputsOff();
      if (isScriptRunning) stopScript(true);
      if (recording) stopRecording();
    } else if (!stopState && safetyStop) {
      safetyStop = false;
      bool prevLock = lock;
      lock = lockBeforeStop;
      if (!lock && prevLock) syncOutputsToSwitches();
    }

    JsonDocument response;
    response["type"] = "command_response";
    response["cmd"] = "safety_stop";
    response["state"] = safetyStop;
    response["success"] = true;
    serializeJson(response, *responseOutput);
    responseOutput->println();
  }
  // Recording control
  else if (cmd == "start_recording") {
    if (!recording) {
      startRecording(false);
      sendResponse("{\"type\":\"command_response\",\"cmd\":\"start_recording\",\"success\":true}", responseOutput);
    } else {
      sendResponse("{\"type\":\"error\",\"message\":\"Already recording\"}", responseOutput);
    }
  }
  else if (cmd == "stop_recording") {
    if (recording) {
      stopRecording();
      sendResponse("{\"type\":\"command_response\",\"cmd\":\"stop_recording\",\"success\":true}", responseOutput);
    } else {
      sendResponse("{\"type\":\"error\",\"message\":\"Not currently recording\"}", responseOutput);
    }
  }
  // Script control
  else if (cmd == "load_script") {
    String scriptName = doc["name"].as<String>();
    loadScriptFromFile((scriptName + ".json").c_str());

    JsonDocument response;
    response["type"] = "command_response";
    response["cmd"] = "load_script";
    response["script_name"] = currentScript.scriptName;
    response["success"] = true;
    serializeJson(response, *responseOutput);
    responseOutput->println();
  }
  else if (cmd == "start_script") {
    if (!isScriptRunning && !safetyStop) {
      startScript();
      sendResponse("{\"type\":\"command_response\",\"cmd\":\"start_script\",\"success\":true}", responseOutput);
    } else {
      sendResponse("{\"type\":\"error\",\"message\":\"Cannot start script\"}", responseOutput);
    }
  }
  else if (cmd == "pause_script") {
    if (isScriptRunning && !isScriptPaused) {
      pauseScript();
      sendResponse("{\"type\":\"command_response\",\"cmd\":\"pause_script\",\"success\":true}", responseOutput);
    } else {
      sendResponse("{\"type\":\"error\",\"message\":\"Cannot pause script\"}", responseOutput);
    }
  }
  else if (cmd == "stop_script") {
    if (isScriptRunning) {
      stopScript(true);
      sendResponse("{\"type\":\"command_response\",\"cmd\":\"stop_script\",\"success\":true}", responseOutput);
    } else {
      sendResponse("{\"type\":\"error\",\"message\":\"No script running\"}", responseOutput);
    }
  }
  // Settings control
  else if (cmd == "set_fan_speed") {
    int speed = doc["value"].as<int>();
    speed = constrain(speed, 0, 255);
    fanSpeed = speed;
    fanOn = (speed > 0);
    saveSettingsToEEPROM();
    applyFanSettings();

    JsonDocument response;
    response["type"] = "command_response";
    response["cmd"] = "set_fan_speed";
    response["value"] = fanSpeed;
    response["success"] = true;
    serializeJson(response, *responseOutput);
    responseOutput->println();
  }
  else if (cmd == "set_update_rate") {
    unsigned long rate = doc["value"].as<unsigned long>();
    rate = constrain(rate, 10UL, 5000UL);
    updateRate = rate;
    saveSettingsToEEPROM();

    JsonDocument response;
    response["type"] = "command_response";
    response["cmd"] = "set_update_rate";
    response["value"] = updateRate;
    response["success"] = true;
    serializeJson(response, *responseOutput);
    responseOutput->println();
  }
  // Data requests
  else if (cmd == "get_status") {
    if (generateStatusJSON(responseBuffer, sizeof(responseBuffer))) {
      responseOutput->println(responseBuffer);
    }
  }
  else if (cmd == "get_scripts") {
    if (generateScriptListJSON(responseBuffer, sizeof(responseBuffer))) {
      responseOutput->println(responseBuffer);
    }
  }
  else if (cmd == "start_stream") {
    unsigned long interval = doc["interval"].as<unsigned long>();
    if (interval < 50) interval = 50;
    if (interval > 5000) interval = 5000;

    streamConfig.streamInterval = interval;

    // Configure UDP target if specified
    if (!doc["udp_target_ip"].isNull()) {
      String targetIP = doc["udp_target_ip"].as<String>();
      IPAddress ip;
      if (ip.fromString(targetIP)) {
        networkConfig.udpTargetIP = ipToUint32(ip);
      }
    }
    if (!doc["udp_target_port"].isNull()) {
      networkConfig.udpTargetPort = doc["udp_target_port"].as<uint16_t>();
    }

    // Configure stream type based on output interface
    if (responseOutput == &Serial) {
      streamConfig.usbStreamEnabled = true;
      streamConfig.tcpStreamEnabled = false;
      streamConfig.udpStreamEnabled = false;
    } else if (responseOutput == &udp) {
      streamConfig.usbStreamEnabled = false;
      streamConfig.tcpStreamEnabled = false;
      streamConfig.udpStreamEnabled = true;
    } else {
      streamConfig.usbStreamEnabled = false;
      streamConfig.tcpStreamEnabled = true;
      streamConfig.udpStreamEnabled = false;
    }
    streamingActive = true;

    JsonDocument response;
    response["type"] = "command_response";
    response["cmd"] = "start_stream";
    response["interval"] = streamConfig.streamInterval;
    response["success"] = true;
    serializeJson(response, *responseOutput);
    responseOutput->println();
  }
  else if (cmd == "stop_stream") {
    streamingActive = false;
    streamConfig.usbStreamEnabled = false;
    streamConfig.tcpStreamEnabled = false;
    streamConfig.udpStreamEnabled = false;

    sendResponse("{\"type\":\"command_response\",\"cmd\":\"stop_stream\",\"success\":true}", responseOutput);
  }
  else {
    sendResponse("{\"type\":\"error\",\"message\":\"Unknown command\"}", responseOutput);
  }
}

/**
 * @brief Send live data stream to configured outputs
 */
void sendLiveDataStream() {
  if (!generateLiveDataJSON(responseBuffer, sizeof(responseBuffer))) return;

  // Send to USB Serial if enabled
  if (streamConfig.usbStreamEnabled && Serial) {
    Serial.println(responseBuffer);
  }

  // Send to TCP clients if enabled
  if (streamConfig.tcpStreamEnabled) {
    for (int i = 0; i < 5; i++) {
      if (tcpClients[i] && tcpClients[i].connected()) {
        tcpClients[i].println(responseBuffer);
      }
    }
  }

  // Send to UDP if enabled
  if (streamConfig.udpStreamEnabled && networkInitialized) {
    udp.beginPacket(uint32ToIP(networkConfig.udpTargetIP), networkConfig.udpTargetPort);
    udp.print(responseBuffer);
    udp.endPacket();
  }
}

/**
 * @brief Generate JSON formatted live data
 * @param buffer Output buffer for JSON data
 * @param bufferSize Size of output buffer
 * @return true if successful, false if buffer too small
 */
bool generateLiveDataJSON(char* buffer, size_t bufferSize) {
  JsonDocument doc;

  doc["type"] = "live_data";
  doc["timestamp"] = getCurrentTimeString();
  doc["script_running"] = isScriptRunning;
  doc["script_time"] = scriptTimeSeconds;
  doc["recording"] = recording;
  doc["locked"] = lock;
  doc["safety_stop"] = safetyStop;

  JsonArray devices = doc["devices"].to<JsonArray>();

  for (int i = 0; i < numSwitches; i++) {
    JsonObject device = devices.add<JsonObject>();
    device["name"] = switchOutputs[i].name;
    device["state"] = (switchOutputs[i].state == HIGH);

    int inaIdx = getInaIndexForSwitch(i);
    if (inaIdx >= 0) {
      device["voltage"] = deviceVoltage[inaIdx];
      device["current"] = deviceCurrent[inaIdx] / 1000.0;
      device["power"] = devicePower[inaIdx];
    } else {
      device["voltage"] = 0.0;
      device["current"] = 0.0;
      device["power"] = 0.0;
    }
  }

  // Add bus totals
  JsonObject total = devices.add<JsonObject>();
  total["name"] = "Bus";
  total["state"] = false;
  total["voltage"] = deviceVoltage[6];
  total["current"] = deviceCurrent[6] / 1000.0;
  total["power"] = devicePower[6];

  size_t len = serializeJson(doc, buffer, bufferSize);
  return (len > 0 && len < bufferSize);
}

/**
 * @brief Generate JSON formatted system status
 * @param buffer Output buffer for JSON data
 * @param bufferSize Size of output buffer
 * @return true if successful, false if buffer too small
 */
bool generateStatusJSON(char* buffer, size_t bufferSize) {
  JsonDocument doc;

  doc["type"] = "status";
  doc["timestamp"] = getCurrentTimeString();
  doc["version"] = SOFTWARE_VERSION;
  doc["locked"] = lock;
  doc["safety_stop"] = safetyStop;
  doc["recording"] = recording;
  doc["script_running"] = isScriptRunning;
  doc["script_paused"] = isScriptPaused;
  doc["current_script"] = currentScript.scriptName;
  doc["dark_mode"] = darkMode;
  doc["external_sd"] = sdAvailable;
  doc["internal_sd"] = internalSdAvailable;
  doc["ethernet_connected"] = ethernetConnected;
  doc["fan_speed"] = fanSpeed;
  doc["update_rate"] = updateRate;
  doc["stream_active"] = streamingActive;
  doc["stream_interval"] = streamConfig.streamInterval;

  if (ethernetConnected) {
    doc["ip_address"] = ipToString(Ethernet.localIP());
    doc["tcp_port"] = networkConfig.tcpPort;
    doc["udp_port"] = networkConfig.udpPort;
  }

  size_t len = serializeJson(doc, buffer, bufferSize);
  return (len > 0 && len < bufferSize);
}

/**
 * @brief Generate JSON formatted script list
 * @param buffer Output buffer for JSON data
 * @param bufferSize Size of output buffer
 * @return true if successful, false if buffer too small
 */
bool generateScriptListJSON(char* buffer, size_t bufferSize) {
  JsonDocument doc;

  doc["type"] = "script_list";
  doc["count"] = numScripts;

  JsonArray scripts = doc["scripts"].to<JsonArray>();
  for (int i = 0; i < numScripts; i++) {
    JsonObject script = scripts.add<JsonObject>();
    script["name"] = scriptList[i].name;
    script["filename"] = scriptList[i].filename;
    script["date_created"] = scriptList[i].dateCreated;
    script["last_used"] = scriptList[i].lastUsed;
  }

  size_t len = serializeJson(doc, buffer, bufferSize);
  return (len > 0 && len < bufferSize);
}

/**
 * @brief Send heartbeat to all connected TCP clients
 */
void sendHeartbeat() {
  JsonDocument doc;
  doc["type"] = "heartbeat";
  doc["timestamp"] = getCurrentTimeString();
  doc["uptime"] = millis();

  for (int i = 0; i < 5; i++) {
    if (tcpClients[i] && tcpClients[i].connected()) {
      serializeJson(doc, tcpClients[i]);
      tcpClients[i].println();
    }
  }
}

/**
 * @brief Send response string to output
 * @param response Response string to send
 * @param output Output stream
 */
void sendResponse(const char* response, Print* output) {
  output->println(response);
}

/**
 * @brief Save network configuration to EEPROM
 */
void saveNetworkConfig() {
  EEPROM.put(EEPROM_NETWORK_CONFIG_ADDR, networkConfig);
}

/**
 * @brief Load network configuration from EEPROM with validation
 */
void loadNetworkConfig() {
  NetworkConfig defaultConfig;
  EEPROM.get(EEPROM_NETWORK_CONFIG_ADDR, networkConfig);

  // Validate loaded configuration
  if (networkConfig.tcpPort < 1024 || networkConfig.tcpPort > 65535) {
    networkConfig.tcpPort = defaultConfig.tcpPort;
  }
  if (networkConfig.udpPort < 1024 || networkConfig.udpPort > 65535) {
    networkConfig.udpPort = defaultConfig.udpPort;
  }
  if (networkConfig.networkTimeout < 1000 || networkConfig.networkTimeout > 30000) {
    networkConfig.networkTimeout = defaultConfig.networkTimeout;
  }
  if (networkConfig.dhcpTimeout < 1000 || networkConfig.dhcpTimeout > 20000) {
    networkConfig.dhcpTimeout = defaultConfig.dhcpTimeout;
  }
}

/**
 * @brief Save network field to configuration
 * @param fieldIndex Index of field to save
 * @param value String value to save
 */
void saveNetworkFieldToConfig(int fieldIndex, const char* value) {
  switch (fieldIndex) {
    case 0: { // Static IP
      IPAddress ip;
      if (ip.fromString(value)) {
        networkConfig.staticIP = ipToUint32(ip);
      }
      break;
    }
    case 1: { // Subnet
      IPAddress ip;
      if (ip.fromString(value)) {
        networkConfig.subnet = ipToUint32(ip);
      }
      break;
    }
    case 2: { // Gateway
      IPAddress ip;
      if (ip.fromString(value)) {
        networkConfig.gateway = ipToUint32(ip);
      }
      break;
    }
    case 3: { // DNS
      IPAddress ip;
      if (ip.fromString(value)) {
        networkConfig.dns = ipToUint32(ip);
      }
      break;
    }
    case 4: { // TCP Port
      int port = atoi(value);
      if (port >= 1024 && port <= 65535) {
        networkConfig.tcpPort = port;
      }
      break;
    }
    case 5: { // UDP Port
      int port = atoi(value);
      if (port >= 1024 && port <= 65535) {
        networkConfig.udpPort = port;
      }
      break;
    }
    case 6: { // Network Timeout
      unsigned long timeout = atol(value);
      if (timeout >= 1000 && timeout <= 30000) {
        networkConfig.networkTimeout = timeout;
      }
      break;
    }
    case 7: { // DHCP Timeout
      unsigned long timeout = atol(value);
      if (timeout >= 1000 && timeout <= 20000) {
        networkConfig.dhcpTimeout = timeout;
      }
      break;
    }
  }
}

/**
 * @brief Load network configuration into edit fields
 */
void loadNetworkFieldsFromConfig() {
  numNetworkFields = 0;

  // Configure edit fields with current network settings
  struct FieldConfig {
    int x, y, w, h, type;
    const char* configValue;
  };

  FieldConfig fields[] = {
    {200, 80, 120, 25, 0, ipToString(uint32ToIP(networkConfig.staticIP)).c_str()},
    {200, 110, 120, 25, 0, ipToString(uint32ToIP(networkConfig.subnet)).c_str()},
    {200, 140, 120, 25, 0, ipToString(uint32ToIP(networkConfig.gateway)).c_str()},
    {200, 170, 120, 25, 0, ipToString(uint32ToIP(networkConfig.dns)).c_str()},
    {200, 200, 80, 25, 1, String(networkConfig.tcpPort).c_str()},
    {200, 230, 80, 25, 1, String(networkConfig.udpPort).c_str()},
    {200, 260, 80, 25, 2, String(networkConfig.networkTimeout).c_str()},
    {340, 260, 80, 25, 2, String(networkConfig.dhcpTimeout).c_str()}
  };

  for (int i = 0; i < 8; i++) {
    networkFields[numNetworkFields].x = fields[i].x;
    networkFields[numNetworkFields].y = fields[i].y;
    networkFields[numNetworkFields].w = fields[i].w;
    networkFields[numNetworkFields].h = fields[i].h;
    networkFields[numNetworkFields].fieldType = fields[i].type;
    strcpy(networkFields[numNetworkFields].value, fields[i].configValue);
    numNetworkFields++;
  }
}

// ==================== SD Card Management ====================

/**
 * @brief Ensure external SD card context is active
 */
void ensureExternalSDContext() {
  if (currentSDContext != false) {
    SD.begin(SD_CS);
    currentSDContext = false;
  }
}

/**
 * @brief Ensure internal SD card context is active
 */
void ensureInternalSDContext() {
  if (currentSDContext != true) {
    SD.begin(BUILTIN_SDCARD);
    currentSDContext = true;
  }
}

/**
 * @brief Check external SD card availability and update UI
 */
void smartCheckSDCard() {
  ensureExternalSDContext();

  bool nowAvailable = false;
  if (SD.begin(SD_CS)) {
    File root = SD.open("/");
    if (root) {
      nowAvailable = true;
      root.close();
    }
  }

  sdAvailable = nowAvailable;
  btnRecord.enabled = sdAvailable && !isScriptRunning;

  if (currentMode == MODE_MAIN) {
    drawButton(btnRecord,
               !sdAvailable ? COLOR_GRAY : (recording ? COLOR_RECORDING : COLOR_RECORD),
               COLOR_WHITE,
               recording ? "RECORDING" : "RECORD",
               false,
               btnRecord.enabled);

    drawButton(btnSDRefresh,
               sdAvailable ? COLOR_GREEN : COLOR_RED,
               COLOR_WHITE,
               "SD",
               false,
               true);
  }
}

/**
 * @brief Check internal SD card availability
 */
void checkInternalSD() {
  if (!recording) {
    ensureInternalSDContext();
    bool nowAvailable = false;
    if (SD.begin(BUILTIN_SDCARD)) {
      nowAvailable = SD.exists("/");
      if (nowAvailable && !SD.exists(SCRIPTS_DIR)) {
        SD.mkdir(SCRIPTS_DIR);
      }
    }
    internalSdAvailable = nowAvailable;
  }
}

// ==================== Data Acquisition ====================

/**
 * @brief Read all INA226 sensor data
 */
void updateSensorData() {
  for (int i = 0; i < numIna; i++) {
    deviceVoltage[i] = inaDevices[i]->getBusVoltage();
    deviceCurrent[i] = inaDevices[i]->getCurrent_mA();
    devicePower[i]   = inaDevices[i]->getPower_mW() / 1000.0;
  }
}

/**
 * @brief Update display elements with current sensor data
 */
void updateDisplayElements() {
  if (currentMode == MODE_MAIN || currentMode == MODE_SCRIPT) {
    for (int i = 0; i < numSwitches; i++) {
      updateLiveValueRow(i);
    }
    if (currentMode == MODE_MAIN) {
      drawTotalRow();
    }
  }
}

// ==================== Data Logging ====================

/**
 * @brief Record sensor data to SD card
 */
void recordDataDirect() {
  if (!recording || !logFile) return;

  ensureExternalSDContext();

  // Validate log file integrity
  if (!logFile.available() && logFile.size() == 0) {
    Serial.println("Log file became invalid - stopping recording");
    stopRecording();
    return;
  }

  unsigned long t = (millis() - recordStartMillis);

  if (csvOutput) {
    // CSV format output
    logFile.print(t);
    for (int i = 0; i < numSwitches; i++) {
      int inaIdx = getInaIndexForSwitch(i);
      logFile.print(",");
      logFile.print(switchOutputs[i].state ? "1" : "0");
      logFile.print(",");
      logFile.print(inaIdx >= 0 ? deviceVoltage[inaIdx] : 0.0f, 4);
      logFile.print(",");
      logFile.print(inaIdx >= 0 ? (deviceCurrent[inaIdx] / 1000.0f) : 0.0f, 4);
      logFile.print(",");
      logFile.print(inaIdx >= 0 ? devicePower[inaIdx] : 0.0f, 4);
    }
    logFile.println();
  } else {
    // JSON format output
    if (!firstDataPoint) {
      logFile.print(",\n");
    } else {
      firstDataPoint = false;
    }

    time_t nowT = now();
    logFile.print("{\"time\":");
    logFile.print(t);
    logFile.print(",\"timestamp\":\"");
    logFile.print(formatTimeHHMMSS(nowT));
    logFile.print("\"");

    for (int i = 0; i < numSwitches; i++) {
      int inaIdx = getInaIndexForSwitch(i);

      logFile.print(",\"");
      logFile.print(switchOutputs[i].name);
      logFile.print("_volt\":");
      logFile.print(inaIdx >= 0 ? deviceVoltage[inaIdx] : 0.0f, 4);

      logFile.print(",\"");
      logFile.print(switchOutputs[i].name);
      logFile.print("_curr\":");
      logFile.print(inaIdx >= 0 ? (deviceCurrent[inaIdx] / 1000.0f) : 0.0f, 4);

      logFile.print(",\"");
      logFile.print(switchOutputs[i].name);
      logFile.print("_pow\":");
      logFile.print(inaIdx >= 0 ? devicePower[inaIdx] : 0.0f, 4);

      logFile.print(",\"");
      logFile.print(switchOutputs[i].name);
      logFile.print("_stat\":");
      logFile.print(switchOutputs[i].state ? "1" : "0");
    }
    logFile.print("}");
  }

  // Periodic flush to ensure data integrity
  static int flushCounter = 0;
  if (++flushCounter >= 3) {
    logFile.flush();
    flushCounter = 0;
  }
}

/**
 * @brief Start data recording to SD card
 * @param scriptRequested true if recording requested by script
 */
void startRecording(bool scriptRequested) {
  if (recording) return;

  ensureExternalSDContext();

  if (!SD.begin(SD_CS)) {
    Serial.println("Cannot initialize external SD card");
    drawButton(btnRecord, COLOR_GRAY, COLOR_WHITE, "NO SD", false, false);
    delay(100);
    drawButton(btnRecord, COLOR_GRAY, COLOR_WHITE, "RECORD", false, false);
    return;
  }

  File root = SD.open("/");
  if (!root) {
    Serial.println("External SD card not accessible");
    drawButton(btnRecord, COLOR_GRAY, COLOR_WHITE, "SD ERR", false, false);
    delay(100);
    drawButton(btnRecord, COLOR_GRAY, COLOR_WHITE, "RECORD", false, false);
    return;
  }
  root.close();

  // Generate appropriate filename
  if (scriptRequested && strlen(currentScript.scriptName) > 0) {
    generateScriptFilename(recordFilename, sizeof(recordFilename), currentScript.scriptName);
  } else {
    if (csvOutput) {
      nextAvailableFilename(recordFilename, sizeof(recordFilename));
      strcpy(recordFilename + strlen(recordFilename) - 5, ".csv");
    } else {
      nextAvailableFilename(recordFilename, sizeof(recordFilename));
    }
  }

  logFile = SD.open(recordFilename, FILE_WRITE);
  if (!logFile) {
    Serial.println("Failed to create log file on external SD");
    drawButton(btnRecord, COLOR_GRAY, COLOR_WHITE, "SD ERR", false, false);
    delay(100);
    drawButton(btnRecord, COLOR_GRAY, COLOR_WHITE, "RECORD", false, false);
    return;
  }

  recordingScript = scriptRequested;

  // Write file header
  if (csvOutput) {
    logFile.print("Time");
    for (int i = 0; i < numSwitches; i++) {
      logFile.print(",");
      logFile.print(switchOutputs[i].name);
      logFile.print("_State,");
      logFile.print(switchOutputs[i].name);
      logFile.print("_Voltage,");
      logFile.print(switchOutputs[i].name);
      logFile.print("_Current,");
      logFile.print(switchOutputs[i].name);
      logFile.print("_Power");
    }
    logFile.println();
  } else {
    // JSON header with metadata
    logFile.print("{\n");
    logFile.print("\"using_script\":");
    logFile.print(scriptRequested ? "1" : "0");
    logFile.print(",\n");

    if (scriptRequested) {
      logFile.print("\"script_config\":{\n");
      logFile.print("\"name\":\"");
      logFile.print(currentScript.scriptName);
      logFile.print("\",\n\"tstart\":");
      logFile.print(currentScript.tStart);
      logFile.print(",\"tend\":");
      logFile.print(currentScript.tEnd);
      logFile.print(",\"record\":");
      logFile.print(currentScript.useRecord ? "true" : "false");
      logFile.print(",\n\"devices\":[\n");

      // Include device configuration
      for (int i = 0; i < 6; i++) {
        if (i > 0) logFile.print(",\n");
        logFile.print("{\"name\":\"");
        logFile.print(switchOutputs[i].name);
        logFile.print("\",\"enabled\":");
        logFile.print(currentScript.devices[i].enabled ? "true" : "false");
        logFile.print(",\"onTime\":");
        logFile.print(currentScript.devices[i].onTime);
        logFile.print(",\"offTime\":");
        logFile.print(currentScript.devices[i].offTime);
        logFile.print("}");
      }

      logFile.print("\n],\n\"script_ended_early\":false");
      logFile.print("\n},\n");
    } else {
      logFile.print("\"script_config\":null,\n");
    }

    time_t nowT = now();
    logFile.print("\"timestamp\":\"");
    logFile.print(formatTimeHHMMSS(nowT));
    logFile.print("\",\n");
    logFile.print("\"data\":[\n");
  }

  logFile.flush();

  recording = true;
  recordStartMillis = millis();
  firstDataPoint = true;
  btnRecord.label = "RECORDING";

  if (currentMode == MODE_MAIN) {
    drawButton(btnRecord, COLOR_RECORDING, COLOR_WHITE, "RECORDING", false, true);
  }

  Serial.print("Recording started: ");
  Serial.println(recordFilename);
}

/**
 * @brief Stop data recording and close file
 */
void stopRecording() {
  if (!recording) return;

  Serial.println("Stopping recording...");

  ensureExternalSDContext();

  recording = false;
  bool wasScriptRecording = recordingScript;
  recordingScript = false;

  if (logFile) {
    if (!csvOutput) {
      long durationSec = (millis() - recordStartMillis) / 1000;
      logFile.print("\n],\n");
      logFile.print("\"duration_sec\":");
      logFile.print(durationSec);

      if (wasScriptRecording) {
        logFile.print(",\n\"script_ended_early\":");
        logFile.print(scriptEndedEarly ? "true" : "false");
      }

      logFile.print("\n}");
    }
    logFile.flush();
    logFile.close();
    Serial.println("Recording stopped and file closed successfully");
  }

  btnRecord.label = "RECORD";
  if (currentMode == MODE_MAIN) {
    drawButton(btnRecord, sdAvailable ? COLOR_RECORD : COLOR_GRAY, COLOR_WHITE, "RECORD", false, sdAvailable);
  }
}

// ==================== Script System ====================

/**
 * @brief Execute active script logic
 */
void handleScripts() {
  unsigned long totalPausedTime = scriptPausedTime;
  if (isScriptPaused) {
    totalPausedTime += (millis() - pauseStartMillis);
  }

  unsigned long msSinceStart = millis() - scriptStartMillis - totalPausedTime;
  long currentSecond = currentScript.tStart + (long)(msSinceStart / 1000);
  scriptTimeSeconds = currentSecond;

  // Check for script completion
  if (currentSecond >= currentScript.tEnd) {
    stopScript(false);
    return;
  }

  // Process device timing events
  for (int i = 0; i < 6; i++) {
    if (!currentScript.devices[i].enabled) continue;

    // Handle device turn-on
    if (currentSecond >= currentScript.devices[i].onTime &&
        !deviceOnTriggered[i] && switchOutputs[i].state == LOW) {
      digitalWrite(switchOutputs[i].outputPin, HIGH);
      switchOutputs[i].state = HIGH;
      deviceOnTriggered[i] = true;
      if (currentMode == MODE_SCRIPT) {
        updateLiveValueRow(i);
      }
    }

    // Handle device turn-off
    if (currentSecond >= currentScript.devices[i].offTime &&
        !deviceOffTriggered[i] && switchOutputs[i].state == HIGH) {
      digitalWrite(switchOutputs[i].outputPin, LOW);
      switchOutputs[i].state = LOW;
      deviceOffTriggered[i] = true;
      if (currentMode == MODE_SCRIPT) {
        updateLiveValueRow(i);
      }
    }
  }
}

/**
 * @brief Start script execution
 */
void startScript() {
  if (isScriptRunning || safetyStop) return;

  lockStateBeforeScript = lock;
  setAllOutputsOff();

  // Reset trigger flags
  for (int i = 0; i < 6; i++) {
    deviceOnTriggered[i] = false;
    deviceOffTriggered[i] = false;
  }

  lock = true;
  updateLockButton();

  scriptStartMillis = millis();
  scriptPausedTime = 0;
  isScriptRunning   = true;
  isScriptPaused    = false;
  scriptEndedEarly  = false;

  // Update script usage timestamp
  currentScript.lastUsed = now();
  char filename[64];
  snprintf(filename, sizeof(filename), "%s.json", currentScript.scriptName);
  updateScriptLastUsed(filename);

  if (currentScript.useRecord) {
    startRecording(true);
  }

  if (currentMode == MODE_SCRIPT) {
    drawScriptPage();
  }
}

/**
 * @brief Pause script execution
 */
void pauseScript() {
  if (!isScriptRunning || isScriptPaused) return;

  isScriptPaused = true;
  pauseStartMillis = millis();

  if (currentMode == MODE_SCRIPT) {
    drawScriptPage();
  }
}

/**
 * @brief Resume paused script
 */
void resumeScript() {
  if (!isScriptRunning || !isScriptPaused) return;

  scriptPausedTime += (millis() - pauseStartMillis);
  isScriptPaused = false;

  if (currentMode == MODE_SCRIPT) {
    drawScriptPage();
  }
}

/**
 * @brief Stop script execution
 * @param userEnded true if user manually stopped script
 */
void stopScript(bool userEnded) {
  if (!isScriptRunning) return;
  
  isScriptRunning = false;
  isScriptPaused = false;

  // Reset trigger flags
  for (int i = 0; i < 6; i++) {
    deviceOnTriggered[i] = false;
    deviceOffTriggered[i] = false;
  }

  lock = lockStateBeforeScript;
  updateLockButton();
  scriptEndedEarly = userEnded;

  if (recordingScript) {
    stopRecording();
  }

  // Synchronize switch states after script completion
  if (!safetyStop) {
    syncSwitchesToOutputs();
  }

  if (currentMode == MODE_SCRIPT) {
    drawScriptPage();
  }
}

/**
 * @brief Synchronize switches to current output states
 */
void syncSwitchesToOutputs() {
  for (int i = 0; i < numSwitches; i++) {
    if (switchOutputs[i].switchPin == -1) continue;

    int switchState = digitalRead(switchOutputs[i].switchPin);
    bool switchOn = (switchState == LOW);

    if (switchOn && switchOutputs[i].state == LOW) {
      digitalWrite(switchOutputs[i].outputPin, HIGH);
      switchOutputs[i].state = HIGH;
      if (currentMode == MODE_MAIN) drawDeviceRow(i);
      if (currentMode == MODE_SCRIPT) updateLiveValueRow(i);
    }
    else if (!switchOn && switchOutputs[i].state == HIGH) {
      digitalWrite(switchOutputs[i].outputPin, LOW);
      switchOutputs[i].state = LOW;
      if (currentMode == MODE_MAIN) drawDeviceRow(i);
      if (currentMode == MODE_SCRIPT) updateLiveValueRow(i);
    }
  }
}

/**
 * @brief Create new script with default values
 */
void createNewScript() {
  memset(&currentScript, 0, sizeof(Script));
  strcpy(currentScript.scriptName, "Untitled");
  currentScript.useRecord = true;
  currentScript.tStart = 0;
  currentScript.tEnd = 120;
  currentScript.dateCreated = now();
  currentScript.lastUsed = now();

  for (int i = 0; i < 6; i++) {
    currentScript.devices[i].enabled = false;
    currentScript.devices[i].onTime = 0;
    currentScript.devices[i].offTime = 10;
  }
}

/**
 * @brief Generate filename for script-based recording
 * @param buf Output buffer for filename
 * @param buflen Size of output buffer
 * @param scriptName Name of script
 */
void generateScriptFilename(char* buf, size_t buflen, const char* scriptName) {
  ensureExternalSDContext();

  const char* ext = csvOutput ? ".csv" : ".json";

  // Clean script name for filesystem compatibility
  char cleanName[32];
  int cleanPos = 0;
  size_t nameLen = strlen(scriptName);
  for (size_t i = 0; i < nameLen && cleanPos < 31; i++) {
    char c = scriptName[i];
    if (isalnum(c) || c == '-' || c == '_') {
      cleanName[cleanPos++] = c;
    } else if (c == ' ') {
      cleanName[cleanPos++] = '_';
    }
  }
  cleanName[cleanPos] = '\0';

  // Find next available filename
  int idx = 0;
  while (true) {
    if (idx == 0) {
      snprintf(buf, buflen, "%s%s", cleanName, ext);
    } else {
      snprintf(buf, buflen, "%s_%d%s", cleanName, idx, ext);
    }

    if (!SD.exists(buf)) break;
    idx++;
    if (idx > 999) break;
  }
}

/**
 * @brief Generate next available filename for data logging
 * @param buf Output buffer for filename
 * @param buflen Size of output buffer
 */
void nextAvailableFilename(char* buf, size_t buflen) {
  int idx = 0;
  const char* ext = csvOutput ? ".csv" : ".json";
  while (true) {
    if (idx == 0)
      snprintf(buf, buflen, "power_data%s", ext);
    else
      snprintf(buf, buflen, "power_data%d%s", idx, ext);

    ensureExternalSDContext();
    if (!SD.exists(buf)) break;
    idx++;
    if (idx > 999) break;
  }
}

/**
 * @brief Load all script names from internal SD
 * @return true if scripts found, false otherwise
 */
bool loadAllScriptNames() {
  ensureInternalSDContext();

  if (!SD.exists(SCRIPTS_DIR)) {
    SD.mkdir(SCRIPTS_DIR);
  }

  File dir = SD.open(SCRIPTS_DIR);
  if (!dir) {
    Serial.println("Failed to open scripts directory");
    return false;
  }

  numScripts = 0;
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;

    if (strstr(entry.name(), ".json") != NULL) {
      strncpy(scriptList[numScripts].filename, entry.name(), 31);
      scriptList[numScripts].filename[31] = '\0';

      char nameOnly[32];
      strncpy(nameOnly, entry.name(), 31);
      nameOnly[31] = '\0';
      char* ext = strstr(nameOnly, ".json");
      if (ext) *ext = '\0';
      strncpy(scriptList[numScripts].name, nameOnly, 31);
      scriptList[numScripts].name[31] = '\0';

      // Read script metadata
      char filePath[64];
      snprintf(filePath, sizeof(filePath), "%s/%s", SCRIPTS_DIR, entry.name());
      File scriptFile = SD.open(filePath, FILE_READ);
      if (scriptFile) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, scriptFile);
        if (!error) {
          scriptList[numScripts].dateCreated = doc["dateCreated"] | now();
          scriptList[numScripts].lastUsed = doc["lastUsed"] | 946684800;
        } else {
          scriptList[numScripts].dateCreated = now();
          scriptList[numScripts].lastUsed = 946684800;
        }
        scriptFile.close();
      } else {
        scriptList[numScripts].dateCreated = now();
        scriptList[numScripts].lastUsed = now();
      }

      numScripts++;
      if (numScripts >= MAX_SCRIPTS) break;
    }
    entry.close();
  }

  dir.close();
  sortScripts();
  return (numScripts > 0);
}

/**
 * @brief Sort script list based on current sort mode
 */
void sortScripts() {
  for (int i = 0; i < numScripts - 1; i++) {
    for (int j = 0; j < numScripts - i - 1; j++) {
      bool shouldSwap = false;

      switch (currentSortMode) {
        case SORT_NAME:
          shouldSwap = strcmp(scriptList[j].name, scriptList[j + 1].name) > 0;
          break;
        case SORT_LAST_USED:
          shouldSwap = scriptList[j].lastUsed < scriptList[j + 1].lastUsed;
          break;
        case SORT_DATE_CREATED:
          shouldSwap = scriptList[j].dateCreated < scriptList[j + 1].dateCreated;
          break;
      }

      if (shouldSwap) {
        ScriptMetadata temp = scriptList[j];
        scriptList[j] = scriptList[j + 1];
        scriptList[j + 1] = temp;
      }
    }
  }
}

/**
 * @brief Delete script file
 * @param scriptName Name of script to delete
 */
void deleteScript(const char* scriptName) {
  ensureInternalSDContext();

  char filePath[64];
  snprintf(filePath, sizeof(filePath), "%s/%s", SCRIPTS_DIR, scriptName);

  if (SD.exists(filePath)) {
    SD.remove(filePath);
    loadAllScriptNames();
  }
}

/**
 * @brief Update script last used timestamp
 * @param scriptName Name of script file
 */
void updateScriptLastUsed(const char* scriptName) {
  ensureInternalSDContext();

  char filePath[64];
  snprintf(filePath, sizeof(filePath), "%s/%s", SCRIPTS_DIR, scriptName);

  File scriptFile = SD.open(filePath, FILE_READ);
  if (!scriptFile) {
    Serial.print("Failed to open script file for update: ");
    Serial.println(filePath);
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, scriptFile);
  scriptFile.close();

  if (error) {
    Serial.print("JSON parsing failed during lastUsed update: ");
    Serial.println(error.c_str());
    return;
  }

  doc["lastUsed"] = now();

  if (SD.exists(filePath)) {
    SD.remove(filePath);
  }

  scriptFile = SD.open(filePath, FILE_WRITE);
  if (!scriptFile) {
    Serial.print("Failed to open script file for writing: ");
    Serial.println(filePath);
    return;
  }

  if (serializeJson(doc, scriptFile) == 0) {
    Serial.println("Failed to write updated lastUsed to script file");
  }

  scriptFile.close();
}

/**
 * @brief Load script from file
 * @param scriptName Name of script file to load
 */
void loadScriptFromFile(const char* scriptName) {
  ensureInternalSDContext();

  char filePath[64];
  snprintf(filePath, sizeof(filePath), "%s/%s", SCRIPTS_DIR, scriptName);

  File scriptFile = SD.open(filePath, FILE_READ);
  if (!scriptFile) {
    Serial.print("Failed to open script file: ");
    Serial.println(filePath);
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, scriptFile);
  scriptFile.close();

  if (error) {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    return;
  }

  memset(&currentScript, 0, sizeof(Script));

  char nameOnly[32];
  strncpy(nameOnly, scriptName, 31);
  nameOnly[31] = '\0';
  char* ext = strstr(nameOnly, ".json");
  if (ext) *ext = '\0';

  strncpy(currentScript.scriptName, nameOnly, sizeof(currentScript.scriptName) - 1);
  currentScript.scriptName[sizeof(currentScript.scriptName) - 1] = '\0';
  currentScript.useRecord = doc["useRecord"] | true;
  currentScript.tStart = doc["tStart"] | 0;
  currentScript.tEnd = doc["tEnd"] | 120;
  currentScript.dateCreated = doc["dateCreated"] | now();
  currentScript.lastUsed = now();

  JsonArray devices = doc["devices"];
  for (int i = 0; i < 6 && i < (int)devices.size(); i++) {
    currentScript.devices[i].enabled = devices[i]["enabled"] | false;
    currentScript.devices[i].onTime = devices[i]["onTime"] | 0;
    currentScript.devices[i].offTime = devices[i]["offTime"] | 10;
  }

  updateScriptLastUsed(scriptName);

  Serial.print("Loaded script: ");
  Serial.println(currentScript.scriptName);
  loadAllScriptNames();
}

/**
 * @brief Save current script to file
 */
void saveCurrentScript() {
  ensureInternalSDContext();

  if (!SD.exists(SCRIPTS_DIR)) {
    SD.mkdir(SCRIPTS_DIR);
  }

  char filePath[64];
  snprintf(filePath, sizeof(filePath), "%s/%s.json", SCRIPTS_DIR, currentScript.scriptName);

  if (SD.exists(filePath)) {
    SD.remove(filePath);
  }

  File scriptFile = SD.open(filePath, FILE_WRITE);
  if (!scriptFile) {
    Serial.print("Failed to create script file: ");
    Serial.println(filePath);
    return;
  }

  if (currentScript.dateCreated == 0) {
    currentScript.dateCreated = now();
  }
  currentScript.lastUsed = now();

  JsonDocument doc;
  doc["name"] = currentScript.scriptName;
  doc["useRecord"] = currentScript.useRecord;
  doc["tStart"] = currentScript.tStart;
  doc["tEnd"] = currentScript.tEnd;
  doc["dateCreated"] = currentScript.dateCreated;
  doc["lastUsed"] = currentScript.lastUsed;

  JsonArray devices = doc["devices"].to<JsonArray>();
  for (int i = 0; i < 6; i++) {
    JsonObject device = devices.add<JsonObject>();
    device["enabled"] = currentScript.devices[i].enabled;
    device["onTime"] = currentScript.devices[i].onTime;
    device["offTime"] = currentScript.devices[i].offTime;
  }

  if (serializeJson(doc, scriptFile) == 0) {
    Serial.println("Failed to write to script file");
  } else {
    Serial.print("Saved script: ");
    Serial.println(currentScript.scriptName);
  }

  scriptFile.close();
  loadAllScriptNames();
}

// ==================== Touch Interface ====================

/**
 * @brief Check if touch coordinates are within button bounds
 * @param x X coordinate
 * @param y Y coordinate
 * @param btn Button region to check
 * @return true if touch is within button
 */
bool touchInButton(int16_t x, int16_t y, const ButtonRegion& btn) {
  return btn.enabled && (x >= btn.x && x <= btn.x+btn.w && y >= btn.y && y <= btn.y+btn.h);
}

/**
 * @brief Handle touch events on main screen
 * @param x Touch X coordinate
 * @param y Touch Y coordinate
 */
void handleTouchMain(int16_t x, int16_t y) {
  ButtonRegion* buttons[] = {
    &btnRecord, &btnSDRefresh, &btnStop, &btnLock, &btnAllOn,
    &btnAllOff, &btnScript, &btnEdit, &btnSettings
  };
  int btnCount = sizeof(buttons) / sizeof(buttons[0]);
  int pressedIdx = -1;

  for (int i=0; i<btnCount; i++) {
    if (touchInButton(x, y, *buttons[i])) {
      pressedIdx = i;
      break;
    }
  }
  
  // Show button press feedback
  if (pressedIdx >= 0) {
    ButtonRegion* btn = buttons[pressedIdx];
    drawButton(*btn, btn->color, COLOR_WHITE, btn->label, true, btn->enabled);
    delay(80);
    drawButton(*btn, btn->color, COLOR_BLACK, btn->label, false, btn->enabled);
  }

  // Process button actions
  if (touchInButton(x, y, btnRecord)) {
    if (!btnRecord.enabled) return;
    if (!recording) {
      startRecording(false);
    } else {
      stopRecording();
    }
  }
  else if (touchInButton(x, y, btnSDRefresh)) {
    smartCheckSDCard();
    checkInternalSD();
  }
  else if (touchInButton(x, y, btnStop)) {
    if (!safetyStop) {
      lockBeforeStop = lock;
      safetyStop = true;
      setAllOutputsOff();
      drawButton(btnStop, COLOR_PURPLE, COLOR_WHITE, "RELEASE", false, btnStop.enabled);

      if (isScriptRunning) {
        stopScript(true);
      }
      if (recording) {
        stopRecording();
      }
    } else {
      safetyStop = false;
      bool prevLock = lock;
      lock = lockBeforeStop;
      drawButton(btnStop, COLOR_YELLOW, COLOR_BLACK, "STOP", false, btnStop.enabled);
      if (!lock && prevLock) syncOutputsToSwitches();
    }
  }
  else if (touchInButton(x, y, btnLock)) {
    bool prevLock = lock;
    lock = !lock;
    updateLockButton();
    if (!lock && prevLock) syncOutputsToSwitches();
  }
  else if (touchInButton(x, y, btnAllOn)) {
    if (isScriptRunning) return;
    if (!safetyStop) {
      lock = true;
      updateLockButton();
      for (int i=0; i<numSwitches; i++) {
        digitalWrite(switchOutputs[i].outputPin, HIGH);
        switchOutputs[i].state = HIGH;
        drawDeviceRow(i);
      }
    }
  }
  else if (touchInButton(x, y, btnAllOff)) {
    if (isScriptRunning) return;
    if (!safetyStop) {
      lock = true;
      updateLockButton();
      setAllOutputsOff();
      for (int i=0; i<numSwitches; i++) drawDeviceRow(i);
    }
  }
  else if (touchInButton(x, y, btnScript)) {
    currentMode = MODE_SCRIPT;
    drawScriptPage();
    return;
  }
  else if (touchInButton(x, y, btnEdit)) {
    currentMode = MODE_EDIT;
    drawEditPage();
    return;
  }
  else if (touchInButton(x, y, btnSettings)) {
    currentMode = MODE_SETTINGS;
    drawSettingsPanel();
    return;
  }
}

/**
 * @brief Handle touch events on settings screen
 * @param x Touch X coordinate
 * @param y Touch Y coordinate
 */
void handleTouchSettings(int16_t x, int16_t y) {
  ButtonRegion* buttons[] = {
    &btnSettingsBack, &btnSettingsStop, &btnFanSpeedInput, &btnUpdateRateInput,
    &btnSetTimeDate, &btnTimeFormatToggle, &btnDarkModeToggle, &btnNetwork, &btnAbout
  };
  int btnCount = sizeof(buttons) / sizeof(buttons[0]);

  for (int i = 0; i < btnCount; i++) {
    if (touchInButton(x, y, *buttons[i])) {
      ButtonRegion* btn = buttons[i];

      drawButton(*btn, COLOR_BTN_PRESS, COLOR_WHITE, btn->label, true, btn->enabled);
      delay(150);

      if (btn == &btnSettingsBack) {
        currentMode = MODE_MAIN;
        drawMainScreen();
        return;
      }
      else if (btn == &btnSettingsStop) {
        if (!safetyStop) {
          lockBeforeStop = lock;
          safetyStop = true;
          setAllOutputsOff();
          if (isScriptRunning) stopScript(true);
          if (recording) stopRecording();
        } else {
          safetyStop = false;
          bool prevLock = lock;
          lock = lockBeforeStop;
          if (!lock && prevLock) syncOutputsToSwitches();
        }
        drawSettingsPanel();
        return;
      }
      else if (btn == &btnFanSpeedInput) {
        currentMode = MODE_KEYPAD;
        keypadMode = KEYPAD_FAN_SPEED;
        keypadPos = 0;
        keypadBuffer[0] = 0;
        drawKeypadPanel();
        return;
      }
      else if (btn == &btnUpdateRateInput) {
        currentMode = MODE_KEYPAD;
        keypadMode = KEYPAD_UPDATE_RATE;
        keypadPos = 0;
        keypadBuffer[0] = 0;
        drawKeypadPanel();
        return;
      }
      else if (btn == &btnSetTimeDate) {
        time_t t = now();
        breakTime(t, tmSet);
        currentMode = MODE_DATE_TIME;
        drawDateTimePanel();
        return;
      }
      else if (btn == &btnTimeFormatToggle) {
        use24HourFormat = !use24HourFormat;
        saveSettingsToEEPROM();
        drawSettingsPanel();
        return;
      }
      else if (btn == &btnDarkModeToggle) {
        darkMode = !darkMode;
        saveSettingsToEEPROM();
        applyDarkMode();
        drawSettingsPanel();
        return;
      }
      else if (btn == &btnNetwork) {
        currentMode = MODE_NETWORK;
        drawNetworkPanel();
        return;
      }
      else if (btn == &btnAbout) {
        currentMode = MODE_ABOUT;
        drawAboutPage();
        return;
      }

      drawSettingsPanel();
      return;
    }
  }
}

/**
 * @brief Handle touch events on network settings screen
 * @param x Touch X coordinate
 * @param y Touch Y coordinate
 */
void handleTouchNetwork(int16_t x, int16_t y) {
  ButtonRegion* buttons[] = {
    &btnNetworkBack, &btnNetworkStop, &btnEnableLanToggle, &btnNetworkEdit
  };
  int btnCount = sizeof(buttons) / sizeof(buttons[0]);

  for (int i = 0; i < btnCount; i++) {
    if (touchInButton(x, y, *buttons[i])) {
      ButtonRegion* btn = buttons[i];

      drawButton(*btn, COLOR_BTN_PRESS, COLOR_WHITE, btn->label, true, btn->enabled);
      delay(150);

      if (btn == &btnNetworkBack) {
        currentMode = MODE_SETTINGS;
        drawSettingsPanel();
        return;
      }
      else if (btn == &btnNetworkStop) {
        if (!safetyStop) {
          lockBeforeStop = lock;
          safetyStop = true;
          setAllOutputsOff();
          if (isScriptRunning) stopScript(true);
          if (recording) stopRecording();
        } else {
          safetyStop = false;
          bool prevLock = lock;
          lock = lockBeforeStop;
          if (!lock && prevLock) syncOutputsToSwitches();
        }
        drawNetworkPanel();
        return;
      }
      else if (btn == &btnEnableLanToggle) {
        networkConfig.enableEthernet = !networkConfig.enableEthernet;
        saveNetworkConfig();
        drawNetworkPanel();
        return;
      }
      else if (btn == &btnNetworkEdit) {
        currentMode = MODE_NETWORK_EDIT;
        loadNetworkFieldsFromConfig();
        drawNetworkEditPanel();
        return;
      }

      drawNetworkPanel();
      return;
    }
  }
}

/**
 * @brief Handle touch events on network edit screen
 * @param x Touch X coordinate
 * @param y Touch Y coordinate
 */
void handleTouchNetworkEdit(int16_t x, int16_t y) {
  ButtonRegion* buttons[] = {
    &btnNetworkEditBack, &btnNetworkEditStop, &btnDhcpToggle, &btnNetworkEditSave
  };
  int btnCount = sizeof(buttons) / sizeof(buttons[0]);

  for (int i = 0; i < btnCount; i++) {
    if (touchInButton(x, y, *buttons[i])) {
      ButtonRegion* btn = buttons[i];

      drawButton(*btn, COLOR_BTN_PRESS, COLOR_WHITE, btn->label, true, btn->enabled);
      delay(150);

      if (btn == &btnNetworkEditBack) {
        currentMode = MODE_NETWORK;
        drawNetworkPanel();
        return;
      }
      else if (btn == &btnNetworkEditStop) {
        if (!safetyStop) {
          lockBeforeStop = lock;
          safetyStop = true;
          setAllOutputsOff();
          if (isScriptRunning) stopScript(true);
          if (recording) stopRecording();
        } else {
          safetyStop = false;
          bool prevLock = lock;
          lock = lockBeforeStop;
          if (!lock && prevLock) syncOutputsToSwitches();
        }
        drawNetworkEditPanel();
        return;
      }
      else if (btn == &btnDhcpToggle) {
        networkConfig.useDHCP = !networkConfig.useDHCP;
        drawNetworkEditPanel();
        return;
      }
      else if (btn == &btnNetworkEditSave) {
        saveNetworkConfig();
        currentMode = MODE_NETWORK;
        drawNetworkPanel();
        return;
      }

      drawNetworkEditPanel();
      return;
    }
  }

  // Handle network field touch
  for (int i = 0; i < numNetworkFields; i++) {
    if (x >= networkFields[i].x && x <= (networkFields[i].x + networkFields[i].w) &&
        y >= networkFields[i].y && y <= (networkFields[i].y + networkFields[i].h)) {

      selectedNetworkField = i;
      strcpy(keypadBuffer, networkFields[i].value);
      keypadPos = strlen(keypadBuffer);

      if (networkFields[i].fieldType == 0) {
        keypadMode = KEYPAD_NETWORK_IP;
      } else if (networkFields[i].fieldType == 1) {
        keypadMode = KEYPAD_NETWORK_PORT;
      } else {
        keypadMode = KEYPAD_NETWORK_TIMEOUT;
      }

      currentMode = MODE_KEYPAD;
      drawKeypadPanel();
      return;
    }
  }
}

/**
 * @brief Handle touch events on script screen
 * @param x Touch X coordinate
 * @param y Touch Y coordinate
 */
void handleTouchScript(int16_t x, int16_t y) {
  ButtonRegion* buttons[] = {
    &btnScriptBack, &btnScriptStop,
    &btnScriptLoad, &btnScriptEdit,
    &btnScriptStart, &btnScriptEnd, &btnScriptRecord
  };
  int btnCount = sizeof(buttons)/sizeof(buttons[0]);
  int pressedIdx = -1;

  for (int i=0; i<btnCount; i++) {
    if (touchInButton(x, y, *buttons[i])) {
      pressedIdx = i;
      break;
    }
  }
  if (pressedIdx >= 0) {
    ButtonRegion* btn = buttons[pressedIdx];
    drawButton(*btn, btn->color, COLOR_WHITE, btn->label, true, btn->enabled);
    delay(80);
    drawButton(*btn, btn->color, COLOR_BLACK, btn->label, false, btn->enabled);
  }

  if (touchInButton(x, y, btnScriptBack)) {
    currentMode = MODE_MAIN;
    drawMainScreen();
    return;
  }
  if (touchInButton(x, y, btnScriptStop)) {
    if (!safetyStop) {
      lockBeforeStop = lock;
      safetyStop = true;
      setAllOutputsOff();
      drawButton(btnScriptStop, COLOR_PURPLE, COLOR_WHITE, "RELEASE",
                 false, btnScriptStop.enabled);
      if (isScriptRunning) {
        stopScript(true);
      }
      if (recording) {
        stopRecording();
      }
    } else {
      safetyStop = false;
      bool prevLock = lock;
      lock = lockBeforeStop;
      drawButton(btnScriptStop, COLOR_YELLOW, COLOR_BLACK, "STOP",
                 false, btnScriptStop.enabled);
      if (!lock && prevLock) syncOutputsToSwitches();
    }
    return;
  }
  if (touchInButton(x, y, btnScriptLoad)) {
    previousMode = MODE_SCRIPT;
    currentMode = MODE_SCRIPT_LOAD;
    selectedScript = -1;
    highlightedScript = -1;
    drawScriptLoadPage();
    return;
  }
  if (touchInButton(x, y, btnScriptEdit)) {
    currentMode = MODE_EDIT;
    drawEditPage();
    return;
  }
  if (touchInButton(x, y, btnScriptStart)) {
    if (!isScriptRunning && !safetyStop) {
      startScript();
      drawScriptPage();
    } else if (isScriptRunning && !isScriptPaused) {
      pauseScript();
      drawScriptPage();
    } else if (isScriptRunning && isScriptPaused) {
      resumeScript();
      drawScriptPage();
    }
    return;
  }
  if (touchInButton(x, y, btnScriptEnd)) {
    if (isScriptRunning) {
      stopScript(true);
      setAllOutputsOff();
      drawScriptPage();
    }
    return;
  }
  if (touchInButton(x, y, btnScriptRecord)) {
    if (recording && recordingScript) {
      stopRecording();
    } else {
      currentScript.useRecord = !currentScript.useRecord;
    }
    drawScriptPage();
    return;
  }
}

/**
 * @brief Handle touch events on edit screen
 * @param x Touch X coordinate
 * @param y Touch Y coordinate
 */
void handleTouchEdit(int16_t x, int16_t y) {
  // Handle script name editing
  if (y >= 10 && y <= 35 && x >= 100 && x <= 380) {
    strcpy(keypadBuffer, currentScript.scriptName);
    keypadPos = strlen(keypadBuffer);
    alphaMode = true;
    shiftMode = false;
    capsMode = false;
    currentMode = MODE_EDIT_NAME;
    keypadMode = KEYPAD_SCRIPT_NAME;
    drawEditSavePage();
    return;
  }

  ButtonRegion* buttons[] = {
    &btnEditBack, &btnEditStop, &btnEditLoad, &btnEditSave, &btnEditNew
  };
  int btnCount = sizeof(buttons)/sizeof(buttons[0]);
  int pressedIdx = -1;

  for(int i=0; i<btnCount; i++) {
    if(touchInButton(x, y, *buttons[i])) {
      pressedIdx = i;
      break;
    }
  }
  if (pressedIdx >= 0) {
    ButtonRegion* btn = buttons[pressedIdx];
    drawButton(*btn, btn->color, COLOR_WHITE, btn->label, true, btn->enabled);
    delay(80);
    drawButton(*btn, btn->color, COLOR_BLACK, btn->label, false, btn->enabled);
  }

  if (touchInButton(x, y, btnEditBack)) {
    currentMode = MODE_MAIN;
    drawMainScreen();
    return;
  }
  if (touchInButton(x, y, btnEditStop)) {
    if (!safetyStop) {
      lockBeforeStop = lock;
      safetyStop = true;
      setAllOutputsOff();
      drawButton(btnEditStop, COLOR_PURPLE, COLOR_BLACK, "RELEASE",
                 false, btnEditStop.enabled);
      if (isScriptRunning) {
        stopScript(true);
      }
      if (recording) {
        stopRecording();
      }
    } else {
      safetyStop = false;
      bool prevLock = lock;
      lock = lockBeforeStop;
      drawButton(btnEditStop, COLOR_YELLOW, COLOR_BLACK, "STOP",
                 false, btnEditStop.enabled);
      if (!lock && prevLock) syncOutputsToSwitches();
    }
    return;
  }
  if (touchInButton(x, y, btnEditLoad)) {
    previousMode = MODE_EDIT;
    currentMode = MODE_EDIT_LOAD;
    selectedScript = -1;
    highlightedScript = -1;
    drawScriptLoadPage();
    return;
  }
  if (touchInButton(x, y, btnEditSave)) {
    previousMode = MODE_EDIT;
    currentMode = MODE_EDIT_SAVE;
    strcpy(keypadBuffer, currentScript.scriptName);
    keypadPos = strlen(keypadBuffer);
    isEditingName = true;
    alphaMode = true;
    shiftMode = false;
    capsMode = false;
    keypadMode = KEYPAD_SCRIPT_NAME;
    drawEditSavePage();
    return;
  }
  if (touchInButton(x, y, btnEditNew)) {
    createNewScript();
    drawEditPage();
    return;
  }

  // Handle edit field touches
  for (int i = 0; i < numEditFields; i++) {
    if (x >= editFields[i].x && x <= (editFields[i].x + editFields[i].w) &&
        y >= editFields[i].y && y <= (editFields[i].y + editFields[i].h)) {

      selectedField = i;
      if (i == 0) {
        keypadMode = KEYPAD_SCRIPT_TSTART;
        strcpy(keypadBuffer, editFields[i].value);
        keypadPos = strlen(keypadBuffer);
        currentMode = MODE_KEYPAD;
        drawKeypadPanel();
      }
      else if (i == 1) {
        keypadMode = KEYPAD_SCRIPT_TEND;
        strcpy(keypadBuffer, editFields[i].value);
        keypadPos = strlen(keypadBuffer);
        currentMode = MODE_KEYPAD;
        drawKeypadPanel();
      }
      else if (i == 2) {
        currentScript.useRecord = !currentScript.useRecord;
        drawEditPage();
      }
      return;
    }
  }

  // Handle device field touches
  for (int i = 0; i < numDeviceFields; i++) {
    if (x >= deviceFields[i].x && x <= (deviceFields[i].x + deviceFields[i].w) &&
        y >= deviceFields[i].y && y <= (deviceFields[i].y + deviceFields[i].h)) {

      selectedDeviceField = i;
      int deviceIdx = deviceFields[i].deviceIndex;

      if (deviceFields[i].fieldType == 0) {
        keypadMode = KEYPAD_DEVICE_ON_TIME;
        snprintf(keypadBuffer, sizeof(keypadBuffer), "%d", currentScript.devices[deviceIdx].onTime);
        keypadPos = strlen(keypadBuffer);
        currentMode = MODE_KEYPAD;
        drawKeypadPanel();
      }
      else if (deviceFields[i].fieldType == 1) {
        keypadMode = KEYPAD_DEVICE_OFF_TIME;
        snprintf(keypadBuffer, sizeof(keypadBuffer), "%d", currentScript.devices[deviceIdx].offTime);
        keypadPos = strlen(keypadBuffer);
        currentMode = MODE_KEYPAD;
        drawKeypadPanel();
      }
      else if (deviceFields[i].fieldType == 2) {
        currentScript.devices[deviceIdx].enabled = !currentScript.devices[deviceIdx].enabled;
        drawEditPage();
      }
      return;
    }
  }
}

/**
 * @brief Handle touch events on script load screen
 * @param x Touch X coordinate
 * @param y Touch Y coordinate
 */
void handleTouchScriptLoad(int16_t x, int16_t y) {
  if (x < 80 && y < 40) {
    currentMode = previousMode;
    selectedScript = -1;
    highlightedScript = -1;
    showDeleteConfirm = false;
    if (previousMode == MODE_SCRIPT) {
      drawScriptPage();
    } else {
      drawEditPage();
    }
    return;
  }

  if (touchInButton(x, y, btnSortDropdown)) {
    currentSortMode = (SortMode)((currentSortMode + 1) % 3);
    saveSettingsToEEPROM();
    sortScripts();
    selectedScript = -1;
    highlightedScript = -1;
    scriptListOffset = 0;
    drawScriptLoadPage();
    return;
  }

  if (selectedScript >= 0 && touchInButton(x, y, btnScriptSelect)) {
    loadScriptFromFile(scriptList[selectedScript].filename);

    currentMode = previousMode;
    selectedScript = -1;
    highlightedScript = -1;
    if (previousMode == MODE_SCRIPT) {
      drawScriptPage();
    } else {
      drawEditPage();
    }
    return;
  }

  if (selectedScript >= 0 && touchInButton(x, y, btnScriptDelete)) {
    strcpy(deleteScriptName, scriptList[selectedScript].name);
    showDeleteConfirm = true;
    currentMode = MODE_DELETE_CONFIRM;
    drawDeleteConfirmDialog();
    return;
  }

  // Handle script list selection
  int yOffset = 60;
  int lineHeight = 22;
  int visibleScripts = min(numScripts - scriptListOffset, 10);

  for (int i = 0; i < visibleScripts; i++) {
    int yPos = yOffset + i * lineHeight;
    if (y >= yPos && y < (yPos + lineHeight)) {
      int scriptIdx = scriptListOffset + i;
      if (scriptIdx < numScripts) {
        highlightedScript = scriptIdx;
        selectedScript = scriptIdx;
        drawScriptLoadPage();
        return;
      }
    }
  }

  // Handle scroll controls
  if (numScripts > 10) {
    if (x >= 440 && x <= 470 && y >= 60 && y <= 90 && scriptListOffset > 0) {
      scriptListOffset--;
      if (selectedScript >= 0 && selectedScript < scriptListOffset) {
        selectedScript = -1;
        highlightedScript = -1;
      }
      drawScriptLoadPage();
      return;
    }

    if (x >= 440 && x <= 470 && y >= 230 && y <= 260 &&
        scriptListOffset < (numScripts - 10)) {
      scriptListOffset++;
      if (selectedScript >= 0 && selectedScript >= scriptListOffset + 10) {
        selectedScript = -1;
        highlightedScript = -1;
      }
      drawScriptLoadPage();
      return;
    }
  }
}

/**
 * @brief Handle touch events on delete confirmation dialog
 * @param x Touch X coordinate
 * @param y Touch Y coordinate
 */
void handleTouchDeleteConfirm(int16_t x, int16_t y) {
  if (touchInButton(x, y, btnDeleteYes)) {
    if (selectedScript >= 0) {
      deleteScript(scriptList[selectedScript].filename);
      selectedScript = -1;
      highlightedScript = -1;
    }
    showDeleteConfirm = false;
    currentMode = (previousMode == MODE_SCRIPT) ? MODE_SCRIPT_LOAD : MODE_EDIT_LOAD;
    drawScriptLoadPage();
    return;
  }

  if (touchInButton(x, y, btnDeleteNo)) {
    showDeleteConfirm = false;
    currentMode = (previousMode == MODE_SCRIPT) ? MODE_SCRIPT_LOAD : MODE_EDIT_LOAD;
    drawScriptLoadPage();
    return;
  }
}

/**
 * @brief Handle touch events on edit field screen
 * @param x Touch X coordinate
 * @param y Touch Y coordinate
 */
void handleTouchEditField(int16_t x, int16_t y) {
  if (touchInButton(x, y, btnEditFieldBack)) {
    currentMode = MODE_EDIT;
    drawEditPage();
    return;
  }
  currentMode = MODE_EDIT;
  drawEditPage();
}

/**
 * @brief Handle touch events on edit save screen
 * @param x Touch X coordinate
 * @param y Touch Y coordinate
 */
void handleTouchEditSave(int16_t x, int16_t y) {
  if (touchInButton(x, y, btnEditSaveBack)) {
    currentMode = MODE_EDIT;
    drawEditPage();
    return;
  }
}

/**
 * @brief Handle touch events on edit name screen
 * @param x Touch X coordinate
 * @param y Touch Y coordinate
 */
void handleTouchEditName(int16_t x, int16_t y) {
  if (touchInButton(x, y, btnEditNameBack)) {
    currentMode = MODE_EDIT;
    drawEditPage();
    return;
  }
}

/**
 * @brief Handle touch events on about screen
 * @param x Touch X coordinate
 * @param y Touch Y coordinate
 */
void handleTouchAbout(int16_t x, int16_t y) {
  if (touchInButton(x, y, btnAboutBack)) {
    currentMode = MODE_SETTINGS;
    drawSettingsPanel();
    return;
  }
  if (touchInButton(x, y, btnAboutStop)) {
    if (!safetyStop) {
      lockBeforeStop = lock;
      safetyStop = true;
      setAllOutputsOff();
      drawButton(btnAboutStop, COLOR_PURPLE, COLOR_BLACK, "RELEASE",
                 false, btnAboutStop.enabled);
      if (isScriptRunning) {
        stopScript(true);
      }
      if (recording) {
        stopRecording();
      }
    } else {
      safetyStop = false;
      bool prevLock = lock;
      lock = lockBeforeStop;
      drawButton(btnAboutStop, COLOR_YELLOW, COLOR_BLACK, "STOP",
                 false, btnAboutStop.enabled);
      if (!lock && prevLock) syncOutputsToSwitches();
    }
    return;
  }
}

/**
 * @brief Handle touch events on date/time screen
 * @param x Touch X coordinate
 * @param y Touch Y coordinate
 */
void handleTouchDateTime(int16_t x, int16_t y) {
  if (x < 80 && y < 40) {
    currentMode = MODE_SETTINGS;
    drawSettingsPanel();
    return;
  }

  if (x > 400 && y < 40) {
    setDateTime(tmSet);
    currentMode = MODE_SETTINGS;
    drawSettingsPanel();
    return;
  }

  int fieldX = 180;
  int fieldWidth = 60;
  int fieldHeight = 30;

  // Handle field adjustments
  struct DateTimeField {
    int minY, maxY;
    void (*increment)();
    void (*decrement)();
  };

  DateTimeField fields[] = {
    {70, 70+fieldHeight, [](){tmSet.Year = constrain(tmSet.Year + 1, 25, 99);}, [](){tmSet.Year = constrain(tmSet.Year - 1, 25, 99);}},
    {110, 110+fieldHeight, [](){tmSet.Month = tmSet.Month % 12 + 1;}, [](){tmSet.Month = (tmSet.Month + 10) % 12 + 1;}},
    {150, 150+fieldHeight, [](){tmSet.Day = tmSet.Day % 31 + 1;}, [](){tmSet.Day = (tmSet.Day + 29) % 31 + 1;}},
    {190, 190+fieldHeight, [](){tmSet.Hour = (tmSet.Hour + 1) % 24;}, [](){tmSet.Hour = (tmSet.Hour + 23) % 24;}},
    {230, 230+fieldHeight, [](){tmSet.Minute = (tmSet.Minute + 1) % 60;}, [](){tmSet.Minute = (tmSet.Minute + 59) % 60;}},
    {270, 270+fieldHeight, [](){tmSet.Second = (tmSet.Second + 1) % 60;}, [](){tmSet.Second = (tmSet.Second + 59) % 60;}}
  };

  for (int i = 0; i < 6; i++) {
    if (y >= fields[i].minY && y <= fields[i].maxY) {
      if (x >= fieldX && x <= (fieldX + fieldWidth)) {
        fields[i].increment();
        drawDateTimePanel();
        return;
      }
      if (x >= (fieldX - 30) && x <= fieldX) {
        fields[i].decrement();
        drawDateTimePanel();
        return;
      }
    }
  }
}

/**
 * @brief Handle keypad input for various modes
 * @param key Key pressed
 */
void handleKeypadInput(char key) {
  if (currentMode == MODE_KEYPAD) {
    if (keypadMode == KEYPAD_DEVICE_ON_TIME || keypadMode == KEYPAD_DEVICE_OFF_TIME ||
        keypadMode == KEYPAD_SCRIPT_TSTART || keypadMode == KEYPAD_SCRIPT_TEND ||
        keypadMode == KEYPAD_UPDATE_RATE || keypadMode == KEYPAD_FAN_SPEED ||
        keypadMode == KEYPAD_NETWORK_IP || keypadMode == KEYPAD_NETWORK_PORT ||
        keypadMode == KEYPAD_NETWORK_TIMEOUT) {

      if (key >= '0' && key <= '9') {
        if (keypadPos < (int)sizeof(keypadBuffer) - 1) {
          keypadBuffer[keypadPos++] = key;
          keypadBuffer[keypadPos] = 0;
        }
      }
      else if (key == '.' && keypadMode == KEYPAD_NETWORK_IP) {
        if (keypadPos < (int)sizeof(keypadBuffer) - 1) {
          keypadBuffer[keypadPos++] = key;
          keypadBuffer[keypadPos] = 0;
        }
      }
      else if (key == '*') {
        if (keypadPos > 0) {
          keypadBuffer[--keypadPos] = 0;
        }
      }
      else if (key == '#') {
        if (keypadMode != KEYPAD_NETWORK_IP) {
          if (keypadBuffer[0] == '-') {
            memmove(keypadBuffer, keypadBuffer + 1, keypadPos);
            keypadPos--;
          } else {
            memmove(keypadBuffer + 1, keypadBuffer, keypadPos + 1);
            keypadBuffer[0] = '-';
            keypadPos++;
          }
        }
      }
      else if (key == 'A') {
        switch (keypadMode) {
          case KEYPAD_UPDATE_RATE: {
            unsigned long val = atol(keypadBuffer);
            if (val < 10) val = 10;
            if (val > 5000) val = 5000;
            updateRate = val;
            saveSettingsToEEPROM();
            applyUpdateRate();
            currentMode = MODE_SETTINGS;
            keypadMode = KEYPAD_NONE;
            drawSettingsPanel();
            break;
          }
          case KEYPAD_FAN_SPEED: {
            int val = atoi(keypadBuffer);
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            fanSpeed = val;
            fanOn = (val > 0);
            saveSettingsToEEPROM();
            applyFanSettings();
            currentMode = MODE_SETTINGS;
            keypadMode = KEYPAD_NONE;
            drawSettingsPanel();
            break;
          }
          case KEYPAD_SCRIPT_TSTART: {
            int val = atoi(keypadBuffer);
            currentScript.tStart = val;
            currentMode = MODE_EDIT;
            keypadMode = KEYPAD_NONE;
            drawEditPage();
            break;
          }
          case KEYPAD_SCRIPT_TEND: {
            int val = atoi(keypadBuffer);
            if (val <= currentScript.tStart) val = currentScript.tStart + 10;
            currentScript.tEnd = val;
            currentMode = MODE_EDIT;
            keypadMode = KEYPAD_NONE;
            drawEditPage();
            break;
          }
          case KEYPAD_DEVICE_ON_TIME: {
            int val = atoi(keypadBuffer);
            if (selectedDeviceField >= 0) {
              int deviceIdx = deviceFields[selectedDeviceField].deviceIndex;
              currentScript.devices[deviceIdx].onTime = val;
            }
            currentMode = MODE_EDIT;
            keypadMode = KEYPAD_NONE;
            drawEditPage();
            break;
          }
          case KEYPAD_DEVICE_OFF_TIME: {
            int val = atoi(keypadBuffer);
            if (selectedDeviceField >= 0) {
              int deviceIdx = deviceFields[selectedDeviceField].deviceIndex;
              currentScript.devices[deviceIdx].offTime = val;
            }
            currentMode = MODE_EDIT;
            keypadMode = KEYPAD_NONE;
            drawEditPage();
            break;
          }
          case KEYPAD_NETWORK_IP:
          case KEYPAD_NETWORK_PORT:
          case KEYPAD_NETWORK_TIMEOUT: {
            if (selectedNetworkField >= 0) {
              saveNetworkFieldToConfig(selectedNetworkField, keypadBuffer);
              strcpy(networkFields[selectedNetworkField].value, keypadBuffer);
            }
            currentMode = MODE_NETWORK_EDIT;
            keypadMode = KEYPAD_NONE;
            drawNetworkEditPanel();
            break;
          }
          default:
            break;
        }
        return;
      }
      else if (key == 'B') {
        if (keypadMode == KEYPAD_UPDATE_RATE || keypadMode == KEYPAD_FAN_SPEED) {
          currentMode = MODE_SETTINGS;
          drawSettingsPanel();
        } else if (keypadMode == KEYPAD_NETWORK_IP || keypadMode == KEYPAD_NETWORK_PORT || keypadMode == KEYPAD_NETWORK_TIMEOUT) {
          currentMode = MODE_NETWORK_EDIT;
          drawNetworkEditPanel();
        } else {
          currentMode = MODE_EDIT;
          drawEditPage();
        }
        keypadMode = KEYPAD_NONE;
        return;
      }
      else if (key == 'C') {
        keypadPos = 0;
        keypadBuffer[0] = 0;
      }

      drawKeypadPanel();
    }
  }
  else if (currentMode == MODE_EDIT_SAVE || currentMode == MODE_EDIT_NAME) {
    if (alphaMode) {
      if (key >= '1' && key <= '9') {
        unsigned long currentTime = millis();

        if (key == lastKey && (currentTime - lastKeyTime) < T9_TIMEOUT) {
          const char* letters = t9Letters[key - '0'];
          int numLetters = strlen(letters);

          if (numLetters > 0) {
            currentLetterIndex = (currentLetterIndex + 1) % numLetters;

            if (keypadPos > 0) {
              char newChar = letters[currentLetterIndex];
              if (capsMode || shiftMode) {
                newChar = toupper(newChar);
              }
              keypadBuffer[keypadPos - 1] = newChar;
            }
          }
        } else {
          const char* letters = t9Letters[key - '0'];
          if (strlen(letters) > 0 && keypadPos < (int)sizeof(keypadBuffer) - 1) {
            currentLetterIndex = 0;
            char newChar = letters[currentLetterIndex];
            if (capsMode || shiftMode) {
              newChar = toupper(newChar);
            }
            keypadBuffer[keypadPos++] = newChar;
            keypadBuffer[keypadPos] = '\0';
          }
        }

        lastKey = key;
        lastKeyTime = currentTime;

        if (shiftMode) shiftMode = false;
      }
      else if (key == '0') {
        const char* chars = t9Letters[0];
        if (keypadPos < (int)sizeof(keypadBuffer) - 1) {
          keypadBuffer[keypadPos++] = chars[0];
          keypadBuffer[keypadPos] = '\0';
        }
        lastKey = '\0';
      }
    } else {
      if (key >= '0' && key <= '9') {
        if (keypadPos < (int)sizeof(keypadBuffer) - 1) {
          keypadBuffer[keypadPos++] = key;
          keypadBuffer[keypadPos] = '\0';
        }
        lastKey = '\0';
      }
    }

    if (key == '#') {
      alphaMode = !alphaMode;
      lastKey = '\0';
    }
    else if (key == 'A') {
      strncpy(currentScript.scriptName, keypadBuffer, sizeof(currentScript.scriptName) - 1);
      currentScript.scriptName[sizeof(currentScript.scriptName) - 1] = '\0';
      if (currentMode == MODE_EDIT_SAVE) {
        saveCurrentScript();
      }
      currentMode = MODE_EDIT;
      drawEditPage();
      return;
    }
    else if (key == 'B') {
      currentMode = MODE_EDIT;
      drawEditPage();
      return;
    }
    else if (key == 'C') {
      shiftMode = !shiftMode;
      lastKey = '\0';
    }
    else if (key == 'D') {
      capsMode = !capsMode;
      lastKey = '\0';
    }
    else if (key == '*') {
      if (keypadPos > 0) {
        keypadBuffer[--keypadPos] = '\0';
      }
      lastKey = '\0';
    }

    drawEditSavePage();
  }
}

/**
 * @brief Handle touch events on keypad screen
 * @param x Touch X coordinate
 * @param y Touch Y coordinate
 */
void handleTouchKeypad(int16_t x, int16_t y) {
  if (touchInButton(x, y, btnKeypadBack)) {
    if (keypadMode == KEYPAD_UPDATE_RATE || keypadMode == KEYPAD_FAN_SPEED) {
      currentMode = MODE_SETTINGS;
      drawSettingsPanel();
    } else if (keypadMode == KEYPAD_NETWORK_IP || keypadMode == KEYPAD_NETWORK_PORT || keypadMode == KEYPAD_NETWORK_TIMEOUT) {
      currentMode = MODE_NETWORK_EDIT;
      drawNetworkEditPanel();
    } else {
      currentMode = MODE_EDIT;
      drawEditPage();
    }
    keypadMode = KEYPAD_NONE;
    return;
  }
}

/**
 * @brief Handle universal back button navigation
 */
void handleUniversalBackButton() {
  switch(currentMode) {
    case MODE_SETTINGS:
      currentMode = MODE_MAIN;
      drawMainScreen();
      break;
    case MODE_NETWORK:
      currentMode = MODE_SETTINGS;
      drawSettingsPanel();
      break;
    case MODE_SCRIPT:
      currentMode = MODE_MAIN;
      drawMainScreen();
      break;
    case MODE_EDIT:
      currentMode = MODE_MAIN;
      drawMainScreen();
      break;
    case MODE_SCRIPT_LOAD:
    case MODE_EDIT_LOAD:
      currentMode = previousMode;
      if (previousMode == MODE_SCRIPT) {
        drawScriptPage();
      } else {
        drawEditPage();
      }
      break;
    case MODE_DATE_TIME:
      currentMode = MODE_SETTINGS;
      drawSettingsPanel();
      break;
    case MODE_DELETE_CONFIRM:
      currentMode = (previousMode == MODE_SCRIPT) ? MODE_SCRIPT_LOAD : MODE_EDIT_LOAD;
      drawScriptLoadPage();
      break;
    case MODE_ABOUT:
      currentMode = MODE_SETTINGS;
      drawSettingsPanel();
      break;
    default:
      break;
  }
}

// ==================== Display Functions ====================

/**
 * @brief Draw button with specified appearance
 * @param btn Button region structure
 * @param bgColor Background color
 * @param textColor Text color
 * @param label Button label text
 * @param pressed Button pressed state
 * @param enabled Button enabled state
 */
void drawButton(ButtonRegion& btn, uint16_t bgColor, uint16_t textColor,
                const char* label, bool pressed, bool enabled) {
  btn.color = bgColor;
  btn.pressed = pressed;
  btn.enabled = enabled;
  uint16_t fill = !enabled ? COLOR_GRAY : (pressed ? COLOR_BTN_PRESS : bgColor);
  tft.fillRect(btn.x, btn.y, btn.w, btn.h, fill);
  tft.drawRect(btn.x, btn.y, btn.w, btn.h, COLOR_BLACK);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextSize(1);
  tft.setTextColor(textColor);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(label, btn.x, btn.y, &x1, &y1, &w, &h);
  int tx = btn.x + (btn.w - w) / 2;
  int ty = btn.y + (btn.h + h) / 2;
  tft.setCursor(tx, ty);
  tft.print(label);
}

/**
 * @brief Draw system initialization screen
 */
void drawInitializationScreen() {
  tft.fillScreen(COLOR_BLACK);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  int16_t x1, y1;
  uint16_t w, h;

  String title = "Mini Chris Box V5.1";
  tft.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_WIDTH - w) / 2, 80);
  tft.print(title);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_YELLOW);

  String status = "Initializing...";
  tft.getTextBounds(status, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_WIDTH - w) / 2, 120);
  tft.print(status);

  // Show initialization progress
  tft.setTextColor(COLOR_CYAN);
  tft.setCursor(50, 160);
  tft.print("• Sensors initialized");
  tft.setCursor(50, 180);
  tft.print("• Display ready");
  tft.setCursor(50, 200);
  tft.print("• SD cards checked");

  updateInitializationScreen();
}

/**
 * @brief Update initialization screen with network status
 */
void updateInitializationScreen() {
  unsigned long currentTime = millis();
  if (currentTime - lastInitScreenUpdate < 250) return;
  lastInitScreenUpdate = currentTime;

  tft.setFont(&FreeSans9pt7b);

  String statusText = "";
  uint16_t statusColor = COLOR_GRAY;

  if (networkConfig.enableEthernet) {
    unsigned long elapsed = currentTime - networkInitStartTime;
    String timeStr = "";
    if (elapsed < 10000) {
      timeStr = "[" + String(elapsed / 1000) + "s]";
    } else {
      timeStr = "[" + String(elapsed) + "ms]";
    }

    switch (networkInitState) {
      case NET_IDLE:
        statusText = "• Network: Starting... " + timeStr;
        statusColor = COLOR_GRAY;
        break;
      case NET_CHECKING_LINK:
        statusText = "• Network: Checking cable... " + timeStr;
        statusColor = COLOR_YELLOW;
        break;
      case NET_INITIALIZING:
        statusText = "• Network: Initializing... " + timeStr;
        statusColor = COLOR_YELLOW;
        break;
      case NET_DHCP_WAIT:
        statusText = "• Network: Getting IP... " + timeStr;
        statusColor = COLOR_YELLOW;
        break;
      case NET_INITIALIZED:
        statusText = "• Network: Ready";
        statusColor = COLOR_GREEN;
        break;
      case NET_FAILED:
        statusText = "• Network: Failed";
        statusColor = COLOR_RED;
        break;
    }
  } else {
    statusText = "• Network: Disabled";
    statusColor = COLOR_GRAY;
  }

  // Update status text only if changed
  if (statusText != lastInitStatusText) {
    tft.fillRect(50, 210, 400, 25, COLOR_BLACK);
    tft.setTextColor(statusColor);
    tft.setCursor(50, 230);
    tft.print(statusText);
    lastInitStatusText = statusText;
  }

  // Show completion message
  if (networkInitState == NET_INITIALIZED || networkInitState == NET_FAILED || !networkConfig.enableEthernet) {
    tft.fillRect(50, 250, 400, 60, COLOR_BLACK);

    tft.setTextColor(COLOR_GREEN);
    tft.setCursor(50, 270);
    tft.print("System Ready!");

    if (networkInitState == NET_INITIALIZED) {
      tft.setTextColor(COLOR_CYAN);
      tft.setCursor(50, 290);
      tft.print("IP: " + ipToString(Ethernet.localIP()));
      tft.setCursor(50, 310);
      tft.print("TCP: " + String(networkConfig.tcpPort) + "  UDP: " + String(networkConfig.udpPort));
    }
  }
}

/**
 * @brief Draw main control screen
 */
void drawMainScreen() {
  tft.fillScreen(COLOR_BLACK);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  int16_t x1, y1;
  uint16_t w, h;

  // Display time or script time
  if (!isScriptRunning) {
    String nowStr = getCurrentTimeString();
    tft.getTextBounds(nowStr, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((SCREEN_WIDTH - w) / 2, 30);
    tft.print(nowStr);
  } else {
    char buff[32];
    if (scriptTimeSeconds < 0) {
      snprintf(buff, sizeof(buff), "T-%ld", labs(scriptTimeSeconds));
    } else {
      snprintf(buff, sizeof(buff), "T+%ld", scriptTimeSeconds);
    }
    tft.getTextBounds(buff, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((SCREEN_WIDTH - w) / 2, 30);
    tft.print(buff);
  }

  // Draw control buttons
  drawButton(btnRecord,
             !sdAvailable ? COLOR_GRAY : (recording ? COLOR_RECORDING : COLOR_RECORD),
             COLOR_WHITE,
             recording ? "RECORDING" : "RECORD",
             false,
             sdAvailable);

  drawButton(btnSDRefresh,
             sdAvailable ? COLOR_GREEN : COLOR_RED,
             COLOR_WHITE,
             "SD",
             false,
             true);

  drawButton(btnStop, COLOR_YELLOW, COLOR_BLACK, "STOP");
  drawButton(btnAllOn, isScriptRunning ? COLOR_GRAY : COLOR_YELLOW,
                        COLOR_BLACK,
                        "ALL ON", false, !isScriptRunning);
  drawButton(btnAllOff, isScriptRunning ? COLOR_GRAY : COLOR_YELLOW,
                         COLOR_BLACK,
                         "ALL OFF", false, !isScriptRunning);
  drawButton(btnScript, COLOR_YELLOW, COLOR_BLACK, "Script");
  drawButton(btnEdit, COLOR_YELLOW, COLOR_BLACK, "Edit");
  updateLockButton();
  drawButton(btnSettings, COLOR_YELLOW, COLOR_BLACK, "Settings", false, btnSettings.enabled);

  // Draw data table header
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_WHITE);

  tft.setCursor(10, 60);
  tft.print("Output");
  tft.setCursor(100, 60);
  tft.print("V");
  tft.setCursor(180, 60);
  tft.print("I (A)");
  tft.setCursor(260, 60);
  tft.print("P (W)");

  tft.drawLine(5, 65, SCREEN_WIDTH - 5, 65, COLOR_GRAY);

  // Draw device rows
  for (int i = 0; i < numSwitches; i++) {
    drawDeviceRow(i);
  }

  // Draw total row
  int totalRowY = 85 + numSwitches * 25 + 10;
  tft.drawLine(5, totalRowY - 5, SCREEN_WIDTH - 5, totalRowY - 5, COLOR_GRAY);
  drawTotalRow();
}

/**
 * @brief Draw settings configuration panel
 */
void drawSettingsPanel() {
  tft.fillScreen(COLOR_BLACK);

  drawButton(btnSettingsBack, COLOR_YELLOW, COLOR_BLACK, "Back", false, true);
  drawButton(btnSettingsStop, COLOR_YELLOW, COLOR_BLACK, "STOP", false, true);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds("Settings", 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_WIDTH - w) / 2, 32);
  tft.print("Settings");

  tft.setFont(&FreeSans9pt7b);

  // Fan speed setting
  tft.fillRect(20, 70, 460, 30, COLOR_DARK_ROW1);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 90);
  tft.print("Fan Speed (0-255):");
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", fanSpeed);
  drawButton(btnFanSpeedInput, COLOR_YELLOW, COLOR_BLACK, buf, false, true);

  // Update rate setting
  tft.fillRect(20, 110, 460, 30, COLOR_DARK_ROW2);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 130);
  tft.print("Update Rate (ms):");
  char buf2[12];
  snprintf(buf2, sizeof(buf2), "%lu", updateRate);
  drawButton(btnUpdateRateInput, COLOR_YELLOW, COLOR_BLACK, buf2, false, true);

  // RTC clock setting
  tft.fillRect(20, 150, 460, 30, COLOR_DARK_ROW1);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 170);
  tft.print("RTC Clock:");
  drawButton(btnSetTimeDate, COLOR_YELLOW, COLOR_BLACK, "Set", false, true);

  // Time format setting
  tft.fillRect(20, 190, 460, 30, COLOR_DARK_ROW2);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 210);
  tft.print("Time Format:");
  drawButton(btnTimeFormatToggle, COLOR_YELLOW, COLOR_BLACK, use24HourFormat ? "24H" : "12H", false, true);

  // Dark mode setting
  tft.fillRect(20, 230, 460, 30, COLOR_DARK_ROW1);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 250);
  tft.print("Dark Mode:");
  drawButton(btnDarkModeToggle, COLOR_YELLOW, COLOR_BLACK, darkMode ? "ON" : "OFF", false, true);

  // Additional settings buttons
  drawButton(btnNetwork, COLOR_YELLOW, COLOR_BLACK, "Network", false, true);
  drawButton(btnAbout, COLOR_YELLOW, COLOR_BLACK, "About", false, true);

  // Show current date/time
  tft.setTextColor(COLOR_GRAY);
  tft.setCursor(30, 280);
  tft.print(formatDateString(now()));
  tft.print(" ");
  tft.print(getCurrentTimeString());
}

/**
 * @brief Draw network configuration panel
 */
void drawNetworkPanel() {
  tft.fillScreen(COLOR_BLACK);

  drawButton(btnNetworkBack, COLOR_YELLOW, COLOR_BLACK, "Back", false, true);
  drawButton(btnNetworkStop, COLOR_YELLOW, COLOR_BLACK, "STOP", false, true);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds("Network Settings", 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_WIDTH - w) / 2, 32);
  tft.print("Network Settings");

  tft.setFont(&FreeSans9pt7b);

  // Enable LAN setting
  tft.fillRect(20, 70, 460, 30, COLOR_DARK_ROW1);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 90);
  tft.print("Enable LAN:");
  drawButton(btnEnableLanToggle, COLOR_YELLOW, COLOR_BLACK, networkConfig.enableEthernet ? "ON" : "OFF", false, true);

  // Connection status
  tft.fillRect(20, 110, 460, 30, COLOR_DARK_ROW2);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 130);
  tft.print("Connection:");
  tft.setTextColor(ethernetConnected ? COLOR_GREEN : COLOR_RED);
  tft.setCursor(130, 130);
  tft.print(ethernetConnected ? "Connected" : "Disconnected");

  // Network information (if connected)
  if (ethernetConnected) {
    tft.fillRect(20, 150, 460, 30, COLOR_DARK_ROW1);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(30, 170);
    tft.print("IP Address:");
    tft.setTextColor(COLOR_CYAN);
    tft.setCursor(125, 170);
    tft.print(ipToString(Ethernet.localIP()));

    tft.fillRect(20, 190, 460, 30, COLOR_DARK_ROW2);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(30, 210);
    tft.print("TCP Port:");
    tft.setTextColor(COLOR_CYAN);
    tft.setCursor(120, 210);
    tft.print(networkConfig.tcpPort);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(200, 210);
    tft.print("UDP Port:");
    tft.setTextColor(COLOR_CYAN);
    tft.setCursor(290, 210);
    tft.print(networkConfig.udpPort);
  }

  drawButton(btnNetworkEdit, COLOR_YELLOW, COLOR_BLACK, "Edit", false, true);
}

/**
 * @brief Draw network edit configuration panel
 */
void drawNetworkEditPanel() {
  tft.fillScreen(COLOR_BLACK);

  drawButton(btnNetworkEditBack, COLOR_YELLOW, COLOR_BLACK, "Back", false, true);
  drawButton(btnNetworkEditStop, COLOR_YELLOW, COLOR_BLACK, "STOP", false, true);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds("Network Configuration", 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_WIDTH - w) / 2, 32);
  tft.print("Network Configuration");

  tft.setFont(&FreeSans9pt7b);

  // DHCP toggle
  tft.fillRect(20, 70, 460, 30, COLOR_DARK_ROW1);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 90);
  tft.print("Use DHCP:");
  drawButton(btnDhcpToggle, COLOR_YELLOW, COLOR_BLACK, networkConfig.useDHCP ? "ON" : "OFF", false, true);

  // Static IP configuration (if DHCP disabled)
  if (!networkConfig.useDHCP) {
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(30, 110);
    tft.print("Static IP:");
    tft.drawRect(networkFields[0].x, networkFields[0].y, networkFields[0].w, networkFields[0].h, COLOR_YELLOW);
    tft.setCursor(networkFields[0].x + 5, networkFields[0].y + 18);
    tft.print(networkFields[0].value);

    tft.setCursor(30, 140);
    tft.print("Subnet:");
    tft.drawRect(networkFields[1].x, networkFields[1].y, networkFields[1].w, networkFields[1].h, COLOR_YELLOW);
    tft.setCursor(networkFields[1].x + 5, networkFields[1].y + 18);
    tft.print(networkFields[1].value);

    tft.setCursor(30, 170);
    tft.print("Gateway:");
    tft.drawRect(networkFields[2].x, networkFields[2].y, networkFields[2].w, networkFields[2].h, COLOR_YELLOW);
    tft.setCursor(networkFields[2].x + 5, networkFields[2].y + 18);
    tft.print(networkFields[2].value);

    tft.setCursor(30, 200);
    tft.print("DNS:");
    tft.drawRect(networkFields[3].x, networkFields[3].y, networkFields[3].w, networkFields[3].h, COLOR_YELLOW);
    tft.setCursor(networkFields[3].x + 5, networkFields[3].y + 18);
    tft.print(networkFields[3].value);
  }

  // Port configuration
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 230);
  tft.print("TCP Port:");
  tft.drawRect(networkFields[4].x, networkFields[4].y, networkFields[4].w, networkFields[4].h, COLOR_YELLOW);
  tft.setCursor(networkFields[4].x + 5, networkFields[4].y + 18);
  tft.print(networkFields[4].value);

  tft.setCursor(30, 260);
  tft.print("UDP Port:");
  tft.drawRect(networkFields[5].x, networkFields[5].y, networkFields[5].w, networkFields[5].h, COLOR_YELLOW);
  tft.setCursor(networkFields[5].x + 5, networkFields[5].y + 18);
  tft.print(networkFields[5].value);

  // Timeout configuration
  tft.setCursor(30, 290);
  tft.print("Timeout (ms):");
  tft.drawRect(networkFields[6].x, networkFields[6].y, networkFields[6].w, networkFields[6].h, COLOR_YELLOW);
  tft.setCursor(networkFields[6].x + 5, networkFields[6].y + 18);
  tft.print(networkFields[6].value);

  tft.setCursor(170, 290);
  tft.print("DHCP Timeout:");
  tft.drawRect(networkFields[7].x, networkFields[7].y, networkFields[7].w, networkFields[7].h, COLOR_YELLOW);
  tft.setCursor(networkFields[7].x + 5, networkFields[7].y + 18);
  tft.print(networkFields[7].value);

  drawButton(btnNetworkEditSave, COLOR_GREEN, COLOR_BLACK, "Save", false, true);
}

/**
 * @brief Draw about/information page
 */
void drawAboutPage() {
  tft.fillScreen(COLOR_BLACK);

  drawButton(btnAboutBack, COLOR_YELLOW, COLOR_BLACK, "Back", false, true);
  drawButton(btnAboutStop, COLOR_YELLOW, COLOR_BLACK, "STOP", false, true);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds("About", 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_WIDTH - w) / 2, 32);
  tft.print("About");

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_WHITE);

  tft.setCursor(30, 70);
  tft.print(SOFTWARE_VERSION);

  tft.setCursor(30, 95);
  tft.print("Designed by Aram Aprahamian");

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_GRAY);

  tft.setCursor(30, 125);
  tft.print("Copyright (c) 2025 Aram Aprahamian");

  tft.setCursor(30, 145);
  tft.print("Permission is hereby granted, free of charge, to any ");
  tft.setCursor(30, 160);
  tft.print("person obtaining a copy of this software and associated");
  tft.setCursor(30, 175);
  tft.print("documentation files (the \"Software\"), to deal in the");
  tft.setCursor(30, 190);
  tft.print("Software without restriction, including without limitation");
  tft.setCursor(30, 205);
  tft.print("the rights to use, copy, modify, merge, publish,");
  tft.setCursor(30, 220);
  tft.print("distribute, sublicense, and/or sell copies of the Software,");
  tft.setCursor(30, 235);
  tft.print("and to permit persons to whom the Software is");
  tft.setCursor(30, 250);
  tft.print("furnished to do so, subject to the above copyright");
  tft.setCursor(30, 265);
  tft.print("notice being included in all copies.");
}

/**
 * @brief Draw keypad input panel
 */
void drawKeypadPanel() {
  tft.fillScreen(COLOR_BLACK);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(40, 50);

  // Display appropriate prompt based on keypad mode
  switch (keypadMode) {
    case KEYPAD_UPDATE_RATE:
      tft.print("Enter Update Rate (ms):");
      break;
    case KEYPAD_FAN_SPEED:
      tft.print("Enter Fan Speed (0-255):");
      break;
    case KEYPAD_SCRIPT_TSTART:
      tft.print("Enter Start Time (can be negative):");
      break;
    case KEYPAD_SCRIPT_TEND:
      tft.print("Enter End Time:");
      break;
    case KEYPAD_DEVICE_ON_TIME:
      tft.print("Enter ON Time (seconds):");
      break;
    case KEYPAD_DEVICE_OFF_TIME:
      tft.print("Enter OFF Time (seconds):");
      break;
    case KEYPAD_SCRIPT_SEARCH:
      tft.print("Enter Script Number:");
      break;
    case KEYPAD_NETWORK_IP:
      tft.print("Enter IP Address:");
      break;
    case KEYPAD_NETWORK_PORT:
      tft.print("Enter Port Number:");
      break;
    case KEYPAD_NETWORK_TIMEOUT:
      tft.print("Enter Timeout (ms):");
      break;
    default:
      tft.print("Enter Value:");
      break;
  }

  // Display current input
  tft.setFont(&FreeMonoBold9pt7b);
  tft.setCursor(40, 90);
  tft.print(keypadBuffer);

  // Display help text
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(40, 160);

  if (keypadMode == KEYPAD_DEVICE_ON_TIME || keypadMode == KEYPAD_DEVICE_OFF_TIME ||
      keypadMode == KEYPAD_SCRIPT_TSTART || keypadMode == KEYPAD_SCRIPT_TEND ||
      keypadMode == KEYPAD_UPDATE_RATE || keypadMode == KEYPAD_FAN_SPEED ||
      keypadMode == KEYPAD_NETWORK_PORT || keypadMode == KEYPAD_NETWORK_TIMEOUT) {
    tft.print("*=Backspace, #=+/-, A=Enter, B=Back, C=Clear");
  } else if (keypadMode == KEYPAD_NETWORK_IP) {
    tft.print("*=Backspace, .=Decimal, A=Enter, B=Back, C=Clear");
  } else {
    tft.print("A=Enter, B=Back, *=Clear");
  }

  if (keypadMode == KEYPAD_SCRIPT_SEARCH) {
    tft.setCursor(40, 190);
    tft.print("Enter script number (1-");
    tft.print(numScripts);
    tft.print(")");
  }

  drawButton(btnKeypadBack, COLOR_YELLOW, COLOR_BLACK, "Back");
}

/**
 * @brief Draw script execution page
 */
void drawScriptPage() {
  tft.fillScreen(COLOR_BLACK);

  drawButton(btnScriptBack, COLOR_YELLOW, COLOR_BLACK, "Back");
  drawButton(btnScriptStop, COLOR_YELLOW, COLOR_BLACK, "STOP");

  tft.setTextColor(COLOR_WHITE);
  char buff[64];

  // Display script name and status
  if (!isScriptRunning) {
    tft.setFont(&FreeSansBold12pt7b);
    snprintf(buff, sizeof(buff), "%s", currentScript.scriptName);
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(buff, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((SCREEN_WIDTH - w) / 2, 30);
    tft.print(buff);
  } else {
    tft.setFont(&FreeSans9pt7b);
    long sTime = scriptTimeSeconds;
    if (sTime < 0) {
      snprintf(buff, sizeof(buff), "%s - T-%ld", currentScript.scriptName, labs(sTime));
    } else {
      snprintf(buff, sizeof(buff), "%s - T+%ld", currentScript.scriptName, sTime);
    }
    if (isScriptPaused) {
      strcat(buff, " (PAUSED)");
    }
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(buff, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((SCREEN_WIDTH - w) / 2, 25);
    tft.print(buff);
  }

  int divX = SCREEN_WIDTH * 2 / 3;
  tft.drawLine(divX, 45, divX, SCREEN_HEIGHT - 45, COLOR_GRAY);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_WHITE);

  // Script configuration table
  tft.setCursor(10, 70);
  tft.print("Name");
  tft.setCursor(80, 70);
  tft.print("On (s)");
  tft.setCursor(140, 70);
  tft.print("Off (s)");
  tft.setCursor(200, 70);
  tft.print("Use");

  int baseY = 90;
  int rowHeight = 25;

  for (int i = 0; i < 6; i++) {
    int yPos = baseY + i * rowHeight;

    tft.setCursor(10, yPos + 15);
    tft.print(switchOutputs[i].name);

    tft.setCursor(80, yPos + 15);
    if (currentScript.devices[i].enabled) {
      tft.print(currentScript.devices[i].onTime);
    } else {
      tft.print("-");
    }

    tft.setCursor(140, yPos + 15);
    if (currentScript.devices[i].enabled) {
      tft.print(currentScript.devices[i].offTime);
    } else {
      tft.print("-");
    }

    tft.setCursor(200, yPos + 15);
    tft.print(currentScript.devices[i].enabled ? "Y" : "N");
  }

  tft.drawLine(10, baseY + 6 * rowHeight + 10, divX - 10, baseY + 6 * rowHeight + 10, COLOR_GRAY);

  // Script parameters
  int configY = baseY + 6 * rowHeight + 25;
  tft.setCursor(10, configY);
  tft.print("Start: ");
  tft.print(currentScript.tStart);
  tft.print("  Stop: ");
  tft.print(currentScript.tEnd);
  tft.print("  Record: ");
  tft.print(currentScript.useRecord ? "Yes" : "No");

  // Device status display
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_YELLOW);
  tft.setCursor(divX + 10, 60);
  tft.print("Device Status");

  for (int i = 0; i < numSwitches; i++) {
    int yPos = 85 + i * 35;
    if (yPos > SCREEN_HEIGHT - 50) break;

    tft.setCursor(divX + 10, yPos);
    bool st = switchOutputs[i].state == HIGH;
    tft.setTextColor(st ? COLOR_GREEN : COLOR_RED);
    tft.print(switchOutputs[i].name);
    tft.print(": ");
    tft.print(st ? "ON" : "OFF");

    int inaIdx = getInaIndexForSwitch(i);
    if (inaIdx >= 0) {
      tft.setCursor(divX + 10, yPos + 15);
      tft.setTextColor(COLOR_CYAN);
      tft.printf("%.1fV %.2fA", deviceVoltage[inaIdx], deviceCurrent[inaIdx] / 1000.0);
    }
  }

  // Control buttons
  drawButton(btnScriptLoad, COLOR_YELLOW, COLOR_BLACK, "Load");
  drawButton(btnScriptEdit, COLOR_YELLOW, COLOR_BLACK, "Edit");

  if (!isScriptRunning) {
    drawButton(btnScriptStart, COLOR_GREEN, COLOR_BLACK, "Start", false, !safetyStop);
  } else if (isScriptPaused) {
    drawButton(btnScriptStart, COLOR_ORANGE, COLOR_BLACK, "Resume", false, true);
  } else {
    drawButton(btnScriptStart, COLOR_ORANGE, COLOR_BLACK, "Pause", false, true);
  }

  drawButton(btnScriptEnd, COLOR_RED, COLOR_BLACK, "Stop", false, isScriptRunning);

  if (recording && recordingScript) {
    drawButton(btnScriptRecord, COLOR_RED, COLOR_WHITE, "Stop Rec", false, true);
  } else {
    drawButton(btnScriptRecord, currentScript.useRecord ? COLOR_BLUE : COLOR_GRAY, COLOR_WHITE, "Record", false, true);
  }
}

/**
 * @brief Draw script edit page
 */
void drawEditPage() {
  tft.fillScreen(COLOR_BLACK);

  drawButton(btnEditBack, COLOR_YELLOW, COLOR_BLACK, "Back");
  drawButton(btnEditStop, COLOR_YELLOW, COLOR_BLACK, "STOP");

  // Script name display
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(currentScript.scriptName, 0, 0, &x1, &y1, &w, &h);
  int nameX = (SCREEN_WIDTH - w) / 2;
  tft.drawRect(nameX - 5, 10, w + 10, 25, COLOR_YELLOW);
  tft.setCursor(nameX, 30);
  tft.print(currentScript.scriptName);

  int divX = SCREEN_WIDTH * 2 / 3;
  tft.drawLine(divX, 45, divX, SCREEN_HEIGHT - 45, COLOR_GRAY);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_WHITE);

  // Device configuration table
  tft.setCursor(10, 70);
  tft.print("Name");
  tft.setCursor(80, 70);
  tft.print("On (s)");
  tft.setCursor(140, 70);
  tft.print("Off (s)");
  tft.setCursor(200, 70);
  tft.print("Use");

  numDeviceFields = 0;

  int baseY = 90;
  int rowHeight = 30;

  for (int i = 0; i < 6; i++) {
    int yPos = baseY + i * rowHeight;

    tft.setCursor(10, yPos + 15);
    tft.print(switchOutputs[i].name);

    // ON time field
    int fieldX = 80;
    int fieldW = 50;
    int fieldH = 25;
    tft.drawRect(fieldX, yPos, fieldW, fieldH, COLOR_YELLOW);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", currentScript.devices[i].onTime);
    tft.setCursor(fieldX + 5, yPos + 18);
    tft.print(buf);

    deviceFields[numDeviceFields].x = fieldX;
    deviceFields[numDeviceFields].y = yPos;
    deviceFields[numDeviceFields].w = fieldW;
    deviceFields[numDeviceFields].h = fieldH;
    deviceFields[numDeviceFields].deviceIndex = i;
    deviceFields[numDeviceFields].fieldType = 0;
    numDeviceFields++;

    // OFF time field
    fieldX = 140;
    tft.drawRect(fieldX, yPos, fieldW, fieldH, COLOR_YELLOW);
    snprintf(buf, sizeof(buf), "%d", currentScript.devices[i].offTime);
    tft.setCursor(fieldX + 5, yPos + 18);
    tft.print(buf);

    deviceFields[numDeviceFields].x = fieldX;
    deviceFields[numDeviceFields].y = yPos;
    deviceFields[numDeviceFields].w = fieldW;
    deviceFields[numDeviceFields].h = fieldH;
    deviceFields[numDeviceFields].deviceIndex = i;
    deviceFields[numDeviceFields].fieldType = 1;
    numDeviceFields++;

    // Enable checkbox
    fieldX = 200;
    fieldW = 25;
    tft.drawRect(fieldX, yPos, fieldW, fieldH, COLOR_YELLOW);
    if (currentScript.devices[i].enabled) {
      tft.fillRect(fieldX + 5, yPos + 5, fieldW - 10, fieldH - 10, COLOR_YELLOW);
    }

    deviceFields[numDeviceFields].x = fieldX;
    deviceFields[numDeviceFields].y = yPos;
    deviceFields[numDeviceFields].w = fieldW;
    deviceFields[numDeviceFields].h = fieldH;
    deviceFields[numDeviceFields].deviceIndex = i;
    deviceFields[numDeviceFields].fieldType = 2;
    numDeviceFields++;
  }

  numEditFields = 0;

  // Script parameters
  tft.setCursor(divX + 10, 70);
  tft.print("T_START:");

  int fieldX = divX + 10;
  int fieldY = 85;
  int fieldW = 60;
  int fieldH = 25;
  tft.drawRect(fieldX, fieldY, fieldW, fieldH, COLOR_YELLOW);
  snprintf(editFields[numEditFields].value, sizeof(editFields[numEditFields].value),
           "%d", currentScript.tStart);
  editFields[numEditFields].x = fieldX;
  editFields[numEditFields].y = fieldY;
  editFields[numEditFields].w = fieldW;
  editFields[numEditFields].h = fieldH;
  tft.setCursor(fieldX + 5, fieldY + 18);
  tft.print(editFields[numEditFields].value);
  numEditFields++;

  tft.setCursor(divX + 10, 130);
  tft.print("T_END:");
  fieldY = 145;
  tft.drawRect(fieldX, fieldY, fieldW, fieldH, COLOR_YELLOW);
  snprintf(editFields[numEditFields].value, sizeof(editFields[numEditFields].value),
           "%d", currentScript.tEnd);
  editFields[numEditFields].x = fieldX;
  editFields[numEditFields].y = fieldY;
  editFields[numEditFields].w = fieldW;
  editFields[numEditFields].h = fieldH;
  tft.setCursor(fieldX + 5, fieldY + 18);
  tft.print(editFields[numEditFields].value);
  numEditFields++;

  tft.setCursor(divX + 10, 190);
  tft.print("Record:");
  fieldY = 205;
  fieldW = 30;
  tft.drawRect(fieldX, fieldY, fieldW, fieldH, COLOR_YELLOW);
  if (currentScript.useRecord) {
    tft.fillRect(fieldX + 5, fieldY + 5, fieldW - 10, fieldH - 10, COLOR_YELLOW);
  }
  editFields[numEditFields].x = fieldX;
  editFields[numEditFields].y = fieldY;
  editFields[numEditFields].w = fieldW;
  editFields[numEditFields].h = fieldH;
  snprintf(editFields[numEditFields].value, sizeof(editFields[numEditFields].value),
           "%s", currentScript.useRecord ? "Yes" : "No");
  tft.setCursor(fieldX + 40, fieldY + 18);
  tft.print(editFields[numEditFields].value);
  numEditFields++;

  drawButton(btnEditLoad, COLOR_YELLOW, COLOR_BLACK, "Load");
  drawButton(btnEditSave, COLOR_YELLOW, COLOR_BLACK, "Save");
  drawButton(btnEditNew, COLOR_YELLOW, COLOR_BLACK, "New");
}

/**
 * @brief Draw script load page with alternating row backgrounds
 */
void drawScriptLoadPage() {
  tft.fillScreen(COLOR_BLACK);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(100, 30);
  tft.print("Select Script");

  tft.fillRect(5, 5, 80, 35, COLOR_YELLOW);
  tft.drawRect(5, 5, 80, 35, COLOR_BLACK);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_BLACK);
  tft.setCursor(25, 28);
  tft.print("Back");

  const char* sortLabels[] = {"Name", "Recent", "Created"};
  drawButton(btnSortDropdown, COLOR_YELLOW, COLOR_BLACK, sortLabels[currentSortMode], false, true);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_WHITE);

  int yOffset = 60;
  int lineHeight = 22;
  int visibleScripts = min(numScripts - scriptListOffset, 10);

  for (int i = 0; i < visibleScripts; i++) {
    int yPos = yOffset + i * lineHeight;
    int scriptIdx = scriptListOffset + i;

    // Alternating row backgrounds
    uint16_t rowColor = (i % 2 == 0) ? COLOR_LIST_ROW1 : COLOR_LIST_ROW2;
    tft.fillRect(15, yPos - 2, 400, lineHeight, rowColor);

    // Highlight selected script
    if (scriptIdx == highlightedScript) {
      tft.fillRect(15, yPos - 2, 400, lineHeight, COLOR_BLUE);
    }

    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(20, yPos + 15);
    tft.print(scriptIdx + 1);
    tft.print(".");

    tft.setCursor(50, yPos + 15);
    tft.print(scriptList[scriptIdx].name);

    tft.setCursor(250, yPos + 15);
    tft.setTextColor(COLOR_GRAY);
    tft.print(formatShortDateTime(scriptList[scriptIdx].dateCreated));
    tft.setTextColor(COLOR_WHITE);
  }

  // Scroll indicators
  if (numScripts > 10) {
    if (scriptListOffset > 0) {
      tft.fillTriangle(450, 70, 440, 80, 460, 80, COLOR_YELLOW);
    }

    if (scriptListOffset < (numScripts - 10)) {
      tft.fillTriangle(450, 240, 440, 230, 460, 230, COLOR_YELLOW);
    }
  }

  tft.setTextColor(COLOR_GRAY);
  tft.setCursor(80, 300);
  tft.print("Press 1-9 to select, A to load script");

  if (selectedScript >= 0) {
    drawButton(btnScriptSelect, COLOR_GREEN, COLOR_BLACK, "Select", false, true);
    drawButton(btnScriptDelete, COLOR_RED, COLOR_BLACK, "Delete", false, true);
  }
}

/**
 * @brief Draw delete confirmation dialog
 */
void drawDeleteConfirmDialog() {
  tft.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);

  tft.fillRect(100, 100, 280, 120, COLOR_GRAY);
  tft.drawRect(100, 100, 280, 120, COLOR_WHITE);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(110, 130);
  tft.print("Delete script:");
  tft.setCursor(110, 150);
  tft.print(deleteScriptName);
  tft.setCursor(110, 170);
  tft.print("Are you sure?");

  drawButton(btnDeleteYes, COLOR_RED, COLOR_WHITE, "Yes", false, true);
  drawButton(btnDeleteNo, COLOR_YELLOW, COLOR_BLACK, "No", false, true);
}

/**
 * @brief Draw script save page with T9 input
 */
void drawEditSavePage() {
  tft.fillScreen(COLOR_BLACK);
  drawButton(btnEditSaveBack, COLOR_YELLOW, COLOR_BLACK, "Back");

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(100, 50);
  if (currentMode == MODE_EDIT_NAME) {
    tft.print("Edit Script Name");
  } else {
    tft.print("Save Script");
  }

  tft.drawRect(50, 80, 380, 40, COLOR_YELLOW);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(60, 105);
  tft.print(keypadBuffer);

  tft.setCursor(50, 150);
  tft.print("T9 Text Input:");
  tft.setCursor(50, 170);
  tft.print("1=abc 2=def 3=ghi 4=jkl 5=mno");
  tft.setCursor(50, 190);
  tft.print("6=pqrs 7=tuv 8=wxyz 0=-,_,space");
  tft.setCursor(50, 220);
  tft.print("#=Alpha/Num A=Save B=Back C=Shift D=Caps");
  tft.setCursor(50, 240);
  tft.print("*=Backspace");

  tft.setCursor(50, 270);
  tft.print("Alpha: ");
  tft.setTextColor(alphaMode ? COLOR_GREEN : COLOR_RED);
  tft.print(alphaMode ? "ON" : "OFF");

  tft.setTextColor(COLOR_WHITE);
  tft.print("  Shift: ");
  tft.setTextColor(shiftMode ? COLOR_GREEN : COLOR_RED);
  tft.print(shiftMode ? "ON" : "OFF");

  tft.setTextColor(COLOR_WHITE);
  tft.print("  Caps: ");
  tft.setTextColor(capsMode ? COLOR_GREEN : COLOR_RED);
  tft.print(capsMode ? "ON" : "OFF");
}

/**
 * @brief Draw date and time setting panel
 */
void drawDateTimePanel() {
  tft.fillScreen(COLOR_BLACK);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(120, 30);
  tft.print("Set Date & Time");

  tft.fillRect(5, 5, 80, 35, COLOR_YELLOW);
  tft.drawRect(5, 5, 80, 35, COLOR_BLACK);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_BLACK);
  tft.setCursor(25, 28);
  tft.print("Back");

  tft.fillRect(SCREEN_WIDTH - 85, 5, 80, 35, COLOR_GREEN);
  tft.drawRect(SCREEN_WIDTH - 85, 5, 80, 35, COLOR_BLACK);
  tft.setCursor(SCREEN_WIDTH - 65, 28);
  tft.print("Save");

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_WHITE);

  // Date/time fields with +/- buttons
  struct DateTimeControl {
    int yPos;
    const char* label;
    int value;
  };

  DateTimeControl controls[] = {
    {70, "Year:", 2000 + tmSet.Year},
    {110, "Month:", tmSet.Month},
    {150, "Day:", tmSet.Day},
    {190, "Hour:", tmSet.Hour},
    {230, "Minute:", tmSet.Minute},
    {270, "Second:", tmSet.Second}
  };

  for (int i = 0; i < 6; i++) {
    tft.setCursor(50, controls[i].yPos + 10);
    tft.print(controls[i].label);
    
    // Decrement button
    tft.fillRect(150, controls[i].yPos, 30, 30, COLOR_GRAY);
    tft.drawRect(150, controls[i].yPos, 30, 30, COLOR_BLACK);
    tft.setTextColor(COLOR_BLACK);
    tft.setCursor(158, controls[i].yPos + 20);
    tft.print("-");

    // Value display
    tft.fillRect(180, controls[i].yPos, 60, 30, COLOR_YELLOW);
    tft.drawRect(180, controls[i].yPos, 60, 30, COLOR_BLACK);
    tft.setCursor(190, controls[i].yPos + 20);
    if (i == 0) {
      tft.print(controls[i].value);
    } else {
      tft.print(controls[i].value);
    }

    // Increment button
    tft.fillRect(240, controls[i].yPos, 30, 30, COLOR_GRAY);
    tft.drawRect(240, controls[i].yPos, 30, 30, COLOR_BLACK);
    tft.setCursor(248, controls[i].yPos + 20);
    tft.print("+");

    tft.setTextColor(COLOR_WHITE);
  }
}

/**
 * @brief Draw individual device row in main display
 * @param row Device row index
 */
void drawDeviceRow(int row) {
  int yPos = 85 + row * 25;
  bool isOn = (switchOutputs[row].state == HIGH);

  tft.fillRect(0, yPos - 17, SCREEN_WIDTH, 25, COLOR_BLACK);

  if (isOn) {
    tft.fillRect(0, yPos - 17, SCREEN_WIDTH, 25, COLOR_PURPLE);
  }

  uint16_t textColor = COLOR_WHITE;

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(textColor);

  tft.setCursor(10, yPos);
  tft.print(switchOutputs[row].name);

  int inaIdx = getInaIndexForSwitch(row);
  if (inaIdx >= 0) {
    tft.setCursor(100, yPos);
    tft.printf("%.2fV", deviceVoltage[inaIdx]);

    tft.setCursor(180, yPos);
    tft.printf("%.4fA", deviceCurrent[inaIdx] / 1000.0);

    tft.setCursor(260, yPos);
    tft.printf("%.3fW", devicePower[inaIdx]);
  }
}

/**
 * @brief Draw total/bus power row
 */
void drawTotalRow() {
  int totalRowY = 85 + numSwitches * 25 + 15;

  tft.fillRect(0, totalRowY - 17, SCREEN_WIDTH, 25, COLOR_BLACK);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_YELLOW);

  tft.setCursor(10, totalRowY);
  tft.print("Bus");

  // Use ina_all device (index 6) for total readings
  tft.setCursor(100, totalRowY);
  tft.printf("%.2fV", deviceVoltage[6]);

  tft.setCursor(180, totalRowY);
  tft.printf("%.4fA", deviceCurrent[6] / 1000.0);

  tft.setCursor(260, totalRowY);
  tft.printf("%.3fW", devicePower[6]);
}

/**
 * @brief Update live value display for specific device row
 * @param row Device row index
 */
void updateLiveValueRow(int row) {
  if (currentMode != MODE_MAIN && currentMode != MODE_SCRIPT) return;

  if (currentMode == MODE_MAIN) {
    drawDeviceRow(row);
  }
  else if (currentMode == MODE_SCRIPT) {
    int divX = SCREEN_WIDTH * 2 / 3;
    int yPos = 85 + row * 35;

    if (yPos > SCREEN_HEIGHT - 50) return;

    tft.fillRect(divX + 10, yPos - 15, 150, 30, COLOR_BLACK);

    bool isOn = (switchOutputs[row].state == HIGH);
    int inaIdx = getInaIndexForSwitch(row);

    tft.setFont(&FreeSans9pt7b);
    tft.setCursor(divX + 10, yPos);
    tft.setTextColor(isOn ? COLOR_GREEN : COLOR_RED);
    tft.print(switchOutputs[row].name);
    tft.print(": ");
    tft.print(isOn ? "ON" : "OFF");

    if (inaIdx >= 0) {
      tft.setCursor(divX + 10, yPos + 15);
      tft.setTextColor(COLOR_CYAN);
      tft.printf("%.1fV %.2fA", deviceVoltage[inaIdx], deviceCurrent[inaIdx] / 1000.0);
    }
  }
}

/**
 * @brief Refresh header clock display
 */
void refreshHeaderClock() {
  if (currentMode != MODE_MAIN && currentMode != MODE_SCRIPT) return;

  char buff[64];
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE, COLOR_BLACK);

  if (currentMode == MODE_MAIN) {
    if (!isScriptRunning) {
      String nowStr = getCurrentTimeString();
      nowStr.toCharArray(buff, sizeof(buff));
    } else {
      if (scriptTimeSeconds < 0) {
        snprintf(buff, sizeof(buff), "T-%ld", labs(scriptTimeSeconds));
      } else {
        snprintf(buff, sizeof(buff), "T+%ld", scriptTimeSeconds);
      }
    }
  } else {
    if (!isScriptRunning) {
      strncpy(buff, currentScript.scriptName, sizeof(buff)-1);
      buff[sizeof(buff)-1] = 0;
    } else {
      tft.setFont(&FreeSans9pt7b);
      long sTime = scriptTimeSeconds;
      if (sTime < 0) {
        snprintf(buff, sizeof(buff), "%.15s - T-%ld", currentScript.scriptName, labs(sTime));
      } else {
        snprintf(buff, sizeof(buff), "%.15s - T+%ld", currentScript.scriptName, sTime);
      }
      if (isScriptPaused) {
        strncat(buff, " (PAUSED)", sizeof(buff) - strlen(buff) - 1);
      }
    }
  }

  int16_t x1,y1; 
  uint16_t w,h;
  tft.getTextBounds(buff, 0, 0, &x1, &y1, &w, &h);
  int tx = (SCREEN_WIDTH - w) / 2;
  int clearX = max(0, tx - 10);
  int clearW = min(SCREEN_WIDTH, w + 20);
  tft.fillRect(clearX, 10, clearW, 25, COLOR_BLACK);
  tft.setCursor(tx, currentMode == MODE_SCRIPT && isScriptRunning ? 25 : 30);
  tft.print(buff);
}

/**
 * @brief Update lock button appearance
 */
void updateLockButton() {
  drawButton(btnLock,
             lock ? COLOR_PURPLE : COLOR_YELLOW,
             lock ? COLOR_WHITE : COLOR_BLACK,
             "LOCK",
             btnLock.pressed,
             btnLock.enabled);
}

// ==================== Utility Functions ====================

/**
 * @brief Get INA226 index for switch index
 * @param switchIdx Switch index
 * @return INA226 index or -1 if not found
 */
int getInaIndexForSwitch(int switchIdx) {
  const char* name = switchOutputs[switchIdx].name;
  for (int i=0; i<numIna; i++) {
    if (strcasecmp(name, inaNames[i]) == 0) return i;
  }
  return -1;
}

/**
 * @brief Turn off all output relays
 */
void setAllOutputsOff() {
  for (int i=0; i<numSwitches; i++) {
    digitalWrite(switchOutputs[i].outputPin, LOW);
    switchOutputs[i].state = LOW;
    if (currentMode == MODE_MAIN) drawDeviceRow(i);
    if (currentMode == MODE_SCRIPT) updateLiveValueRow(i);
  }
}

/**
 * @brief Synchronize outputs to physical switch positions
 */
void syncOutputsToSwitches() {
  for (int i=0; i<numSwitches; i++) {
    if (switchOutputs[i].switchPin == -1) {
      continue;
    }
    int sw = digitalRead(switchOutputs[i].switchPin);
    if (sw == LOW) {
      digitalWrite(switchOutputs[i].outputPin, HIGH);
      switchOutputs[i].state = HIGH;
    } else {
      digitalWrite(switchOutputs[i].outputPin, LOW);
      switchOutputs[i].state = LOW;
    }
    if (currentMode == MODE_MAIN) drawDeviceRow(i);
    if (currentMode == MODE_SCRIPT) updateLiveValueRow(i);
  }
}

/**
 * @brief Apply fan settings to hardware
 */
void applyFanSettings() {
  analogWrite(FAN_PWM_PIN, fanOn ? fanSpeed : 0);
}

/**
 * @brief Apply update rate (placeholder function)
 */
void applyUpdateRate() {
  // Update rate is used directly in main loop
}

/**
 * @brief Apply dark mode display setting
 */
void applyDarkMode() {
  tft.invertDisplay(darkMode);
  Serial.print("Dark mode: ");
  Serial.println(darkMode ? "ON" : "OFF");
}

/**
 * @brief Find switch index by device name
 * @param deviceName Device name string
 * @return Switch index or -1 if not found
 */
int findSwitchIndex(String deviceName) {
  if (deviceName == "gse1") deviceName = "gse-1";
  else if (deviceName == "gse2") deviceName = "gse-2";
  else if (deviceName == "ter") deviceName = "te-r";
  else if (deviceName == "te1") deviceName = "te-1";
  else if (deviceName == "te2") deviceName = "te-2";
  else if (deviceName == "te3") deviceName = "te-3";

  for (int i=0; i<numSwitches; i++) {
    String swName = String(switchOutputs[i].name);
    swName.toLowerCase();
    if (swName == deviceName) {
      return i;
    }
  }
  return -1;
}

/**
 * @brief Set output state by device name
 * @param deviceName Device name
 * @param state Desired output state
 */
void setOutputState(String deviceName, bool state) {
  if (lock || safetyStop || isScriptRunning) {
    Serial.println("Cannot change outputs - system is locked, in safety stop, or script is running");
    return;
  }
  int switchIndex = findSwitchIndex(deviceName);
  if (switchIndex >= 0) {
    digitalWrite(switchOutputs[switchIndex].outputPin, state ? HIGH : LOW);
    switchOutputs[switchIndex].state = state ? HIGH : LOW;
    if (currentMode == MODE_MAIN) drawDeviceRow(switchIndex);
    if (currentMode == MODE_SCRIPT) updateLiveValueRow(switchIndex);

    Serial.print(switchOutputs[switchIndex].name);
    Serial.print(" turned ");
    Serial.println(state ? "ON" : "OFF");
  } else {
    Serial.print("Unknown device: ");
    Serial.println(deviceName);
    Serial.println("Available devices: gse1, gse2, ter, te1, te2, te3");
  }
}

/**
 * @brief Print current system status to serial
 */
void printCurrentStatus() {
  if (csvOutput) {
    if (!csvHeaderWritten) {
      Serial.print("Time,");
      for (int i=0; i<numSwitches; i++) {
        Serial.print(switchOutputs[i].name);
        Serial.print("_State,");
        Serial.print(switchOutputs[i].name);
        Serial.print("_Voltage,");
        Serial.print(switchOutputs[i].name);
        Serial.print("_Current,");
        Serial.print(switchOutputs[i].name);
        Serial.print("_Power");
        if (i < numSwitches-1) Serial.print(",");
      }
      Serial.println();
      csvHeaderWritten = true;
    }
    Serial.print(millis());
    Serial.print(",");
    for (int i=0; i<numSwitches; i++) {
      int inaIdx = getInaIndexForSwitch(i);
      Serial.print(switchOutputs[i].state ? "1":"0");
      Serial.print(",");
      Serial.print(inaIdx>=0?deviceVoltage[inaIdx]:0,4);
      Serial.print(",");
      Serial.print(inaIdx>=0?(deviceCurrent[inaIdx]/1000.0):0,4);
      Serial.print(",");
      Serial.print(inaIdx>=0?devicePower[inaIdx]:0,4);
      if (i < numSwitches-1) Serial.print(",");
    }
    Serial.println();
  } else {
    Serial.println("=== Current Status ===");
    Serial.print("System Lock: ");
    Serial.println(lock ? "LOCKED":"UNLOCKED");
    Serial.print("Safety Stop: ");
    Serial.println(safetyStop ? "ACTIVE":"INACTIVE");
    Serial.print("Recording: ");
    Serial.println(recording ? "ACTIVE":"INACTIVE");
    Serial.print("Script Running: ");
    Serial.println(isScriptRunning ? "YES":"NO");
    Serial.print("Output Format: ");
    Serial.println(csvOutput ? "CSV":"Human Readable");
    Serial.print("Dark Mode: ");
    Serial.println(darkMode ? "ON":"OFF");
    Serial.print("External SD: ");
    Serial.println(sdAvailable ? "Available":"Not Available");
    Serial.print("Internal SD: ");
    Serial.println(internalSdAvailable ? "Available":"Not Available");
    Serial.print("SD Check Interval: ");
    Serial.print(SD_CHECK_INTERVAL);
    Serial.println("ms");
    Serial.print("Ethernet Enabled: ");
    Serial.println(networkConfig.enableEthernet ? "YES":"NO");
    Serial.print("Ethernet Connected: ");
    Serial.println(ethernetConnected ? "YES":"NO");
    if (ethernetConnected) {
      Serial.print("IP Address: ");
      Serial.println(ipToString(Ethernet.localIP()));
      Serial.print("TCP Port: ");
      Serial.println(networkConfig.tcpPort);
      Serial.print("UDP Port: ");
      Serial.println(networkConfig.udpPort);
    }
    Serial.println();

    for (int i=0; i<numSwitches; i++) {
      int inaIdx = getInaIndexForSwitch(i);
      Serial.print(switchOutputs[i].name);
      Serial.print(": ");
      Serial.print(switchOutputs[i].state ? "ON":"OFF");
      if (inaIdx >= 0) {
        Serial.print(" | V=");
        Serial.print(deviceVoltage[inaIdx], 2);
        Serial.print("V | I=");
        Serial.print(deviceCurrent[inaIdx] / 1000.0, 3);
        Serial.print("A | P=");
        Serial.print(devicePower[inaIdx], 3);
        Serial.print("W");
      }
      Serial.println();
    }
    Serial.println("===================");
  }
}

/**
 * @brief Print help information to serial
 */
void printHelp() {
  Serial.println("=== Available Commands ===");
  Serial.println("Output Control:");
  Serial.println("  gse1 on/off  - Control GSE-1 output");
  Serial.println("  gse2 on/off  - Control GSE-2 output");
  Serial.println("  ter on/off   - Control TE-R output");
  Serial.println("  te1 on/off   - Control TE-1 output");
  Serial.println("  te2 on/off   - Control TE-2 output");
  Serial.println("  te3 on/off   - Control TE-3 output");
  Serial.println();
  Serial.println("System Control:");
  Serial.println("  lock         - Lock all outputs");
  Serial.println("  unlock       - Unlock outputs");
  Serial.println("  start log    - Start data logging");
  Serial.println("  stop log     - Stop data logging");
  Serial.println("  refresh sd   - Manually refresh SD card status");
  Serial.println();
  Serial.println("Output Format:");
  Serial.println("  csv on       - Enable CSV output format");
  Serial.println("  csv off      - Enable human readable format");
  Serial.println();
  Serial.println("Information:");
  Serial.println("  status       - Show current system status");
  Serial.println("  help         - Show this help message");
  Serial.println();
  Serial.println("Network Commands (JSON format):");
  Serial.println("  {\"cmd\":\"get_status\"}");
  Serial.println("  {\"cmd\":\"start_stream\",\"interval\":100}");
  Serial.println("  {\"cmd\":\"set_output\",\"device\":\"GSE-1\",\"state\":true}");
  Serial.println("========================");
}

/**
 * @brief Handle legacy text commands
 * @param command Command string
 */
void handleCommand(String command) {
  command.trim();
  command.toLowerCase();

  Serial.print("Command received: ");
  Serial.println(command);

  if (command == "help") {
    printHelp();
  }
  else if (command == "status") {
    printCurrentStatus();
  }
  else if (command == "lock") {
    lock = true;
    updateLockButton();
    Serial.println("System LOCKED");
  }
  else if (command == "unlock") {
    bool prevLock = lock;
    lock = false;
    updateLockButton();
    if (prevLock) syncOutputsToSwitches();
    Serial.println("System UNLOCKED");
  }
  else if (command == "start log") {
    if (!recording) {
      startRecording(false);
      Serial.println("Logging STARTED");
    } else {
      Serial.println("Already logging");
    }
  }
  else if (command == "stop log") {
    if (recording) {
      stopRecording();
      Serial.println("Logging STOPPED");
    } else {
      Serial.println("Not currently logging");
    }
  }
  else if (command == "csv on") {
    csvOutput = true;
    csvHeaderWritten = false;
    Serial.println("CSV output format ENABLED");
  }
  else if (command == "csv off") {
    csvOutput = false;
    Serial.println("Human readable output format ENABLED");
  }
  else if (command == "refresh sd") {
    smartCheckSDCard();
    checkInternalSD();
    Serial.println("SD card status refreshed manually");
  }
  else if (command.indexOf(" on") != -1) {
    String deviceName = command.substring(0, command.indexOf(" on"));
    setOutputState(deviceName, true);
  }
  else if (command.indexOf(" off") != -1) {
    String deviceName = command.substring(0, command.indexOf(" off"));
    setOutputState(deviceName, false);
  }
  else {
    Serial.println("Unknown command. Type 'help' for available commands.");
  }
}

/**
 * @brief Process serial command input
 */
void processSerialCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, serialBuffer);

        if (!error) {
          processNetworkCommand(serialBuffer, &Serial);
        } else {
          handleCommand(serialBuffer);
        }
        serialBuffer = "";
      }
    } else {
      serialBuffer += c;
    }
  }
}

/**
 * @brief Print message to serial if available
 * @param message Message to print
 */
void serialPrint(String message) {
  if (serialAvailable && message.trim().length() > 0) {
    String temp = message;
    temp.trim();
    Serial.println(temp);
  }
}

// ==================== Time Functions ====================

/**
 * @brief Initialize real-time clock
 */
void initRTC() {
  setSyncProvider(getTeensyTime);

  if (timeStatus() != timeSet) {
    Serial.println("Unable to sync with the RTC");
    setTime(0, 0, 0, 1, 1, 2025);
  } else {
    Serial.println("RTC has set the system time");
  }
}

/**
 * @brief Get time from Teensy RTC
 * @return Current time as time_t
 */
time_t getTeensyTime() {
  return Teensy3Clock.get();
}

/**
 * @brief Set date and time
 * @param tm Time elements structure
 */
void setDateTime(tmElements_t tm) {
  time_t t = makeTime(tm);
  setTime(t);
  Teensy3Clock.set(t);
}

/**
 * @brief Get current time as formatted string
 * @return Time string
 */
String getCurrentTimeString() {
  time_t t = now();
  return formatTimeHHMMSS(t);
}

/**
 * @brief Format time as HH:MM:SS string
 * @param t Time value
 * @return Formatted time string
 */
String formatTimeHHMMSS(time_t t) {
  char buf[12];
  if (use24HourFormat) {
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

/**
 * @brief Format date as YYYY-MM-DD string
 * @param t Time value
 * @return Formatted date string
 */
String formatDateString(time_t t) {
  char buf[12];
  sprintf(buf, "20%02d-%02d-%02d", year(t) % 100, month(t), day(t));
  return String(buf);
}

/**
 * @brief Format short date/time string
 * @param t Time value
 * @return Formatted short date/time string
 */
String formatShortDateTime(time_t t) {
  char buf[16];
  if (use24HourFormat) {
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

// ==================== System Setup ====================

/**
 * @brief Main system setup function
 */
void setup() {
  Serial.begin(2000000);
  Wire.begin();

  // Initialize display first for status messages
  tft.init(320, 480, 0, 0, ST7796S_BGR);
  tft.setRotation(1);
  ts.begin();
  ts.setRotation(1);

  // Initialize EEPROM and settings
  initializeEEPROM();
  applyDarkMode();

  // Show initialization screen
  drawInitializationScreen();

  // Initialize RTC
  initRTC();

  // Initialize INA226 sensors
  for (int i = 0; i < numIna; i++) {
    inaDevices[i]->begin();
    inaDevices[i]->setMaxCurrentShunt(8, 0.01);
  }

  // Initialize switch inputs and outputs
  for (int i = 0; i < numSwitches; i++) {
    if (switchOutputs[i].switchPin == -1) {
      continue;
    }
    pinMode(switchOutputs[i].switchPin, INPUT_PULLUP);
    pinMode(switchOutputs[i].outputPin, OUTPUT);
    switchOutputs[i].debouncer.attach(switchOutputs[i].switchPin);
    switchOutputs[i].debouncer.interval(10);
  }

  // Set initial output states based on switch positions
  for (int i = 0; i < numSwitches; i++) {
    if (switchOutputs[i].switchPin == -1) {
      continue;
    }
    int initialState = digitalRead(switchOutputs[i].switchPin);
    if (initialState == LOW) {
      digitalWrite(switchOutputs[i].outputPin, HIGH);
      switchOutputs[i].state = HIGH;
    } else {
      digitalWrite(switchOutputs[i].outputPin, LOW);
      switchOutputs[i].state = LOW;
    }
  }

  // Initialize LED outputs
  pinMode(FAN_PWM_PIN, OUTPUT);
  pinMode(pwrLed, OUTPUT);
  pinMode(lockLed, OUTPUT);
  pinMode(stopLed, OUTPUT);

  digitalWrite(pwrLed, HIGH);
  digitalWrite(lockLed, LOW);
  digitalWrite(stopLed, LOW);

  // Check SD card availability
  smartCheckSDCard();
  checkInternalSD();

  // Apply saved settings
  applyFanSettings();
  applyUpdateRate();

  // Initialize script system
  createNewScript();
  loadAllScriptNames();
  sortScripts();

  // Initialize network (non-blocking)
  initNetworkBackground();

  // Wait for network initialization to complete
  while (networkInitState == NET_CHECKING_LINK || networkInitState == NET_INITIALIZING || networkInitState == NET_DHCP_WAIT) {
    updateNetworkInit();
    updateInitializationScreen();
    delay(50);
  }

  // Show final initialization status
  updateInitializationScreen();
  if (networkInitState == NET_INITIALIZED) {
    delay(1000);
  } else {
    delay(500);
  }

  // Switch to main screen
  drawMainScreen();

  // Send startup message if serial is available
  if (Serial.available()) {
    serialAvailable = true;
    Serial.println("Teensy 4.1 Power Controller Ready - Network Enabled");
    Serial.println("Type 'help' for available commands");
    if (networkInitialized) {
      Serial.print("Network ready. IP: ");
      Serial.println(ipToString(Ethernet.localIP()));
    } else {
      Serial.println("Network initialization failed or disabled");
    }
  }
}

// ==================== Main Loop ====================

/**
 * @brief Main system loop
 */
void loop() {
  unsigned long currentMillis = millis();

  // Update network initialization state
  updateNetworkInit();

  // Process serial commands
  processSerialCommands();

  // Handle network communication
  handleNetworkCommunication();

  // Check network status periodically
  checkNetworkStatus();

  // Handle data streaming
  if (streamingActive && (currentMillis - lastStreamTime >= streamConfig.streamInterval)) {
    sendLiveDataStream();
    lastStreamTime = currentMillis;
  }

  // Check SD card status (only when not recording)
  if (!recording && (currentMillis - lastSDCheck > SD_CHECK_INTERVAL)) {
    smartCheckSDCard();
    checkInternalSD();
    lastSDCheck = currentMillis;
  }

  // Handle hardware keypad input
  char key = keypad.getKey();
  if (key == 'B') {
    if (currentMode != MODE_KEYPAD && currentMode != MODE_EDIT_SAVE && currentMode != MODE_EDIT_NAME && currentMode != MODE_NETWORK_EDIT) {
      handleUniversalBackButton();
    } else {
      handleKeypadInput(key);
    }
  }

  // Handle script selection shortcuts
  if (currentMode == MODE_SCRIPT_LOAD && key >= '1' && key <= '9') {
    int scriptNum = key - '0';
    if (scriptNum <= numScripts) {
      selectedScript = scriptNum - 1;
      highlightedScript = scriptNum - 1;
      scriptListOffset = max(0, (scriptNum - 1) - 5);
      drawScriptLoadPage();
      return;
    }
  }

  // Handle script load confirmation
  if (currentMode == MODE_SCRIPT_LOAD && key == 'A' && selectedScript >= 0) {
    loadScriptFromFile(scriptList[selectedScript].filename);
    currentMode = previousMode;
    selectedScript = -1;
    highlightedScript = -1;
    if (previousMode == MODE_SCRIPT) {
      drawScriptPage();
    } else {
      drawEditPage();
    }
    return;
  }

  // Handle recording LED blinking
  if (recording && (currentMillis - lastPowerLedBlink >= 500)) {
    lastPowerLedBlink = currentMillis;
    powerLedState = !powerLedState;
    digitalWrite(pwrLed, powerLedState ? HIGH : LOW);
  } else if (!recording) {
    digitalWrite(pwrLed, HIGH);
  }

  // Handle keypad input for text modes
  if (currentMode == MODE_KEYPAD || currentMode == MODE_EDIT_SAVE || currentMode == MODE_EDIT_NAME || currentMode == MODE_NETWORK_EDIT) {
    if (key && key != 'B') {
      handleKeypadInput(key);
    }
  }

  // Update sensor data
  if (currentMillis - lastSensorUpdate >= SENSOR_UPDATE_INTERVAL) {
    updateSensorData();
    lastSensorUpdate = currentMillis;
  }

  // Update display elements
  if (currentMillis - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    updateDisplayElements();
    lastDisplayUpdate = currentMillis;
  }

  // Write log data
  if (recording && (currentMillis - lastLogWrite >= LOG_WRITE_INTERVAL)) {
    recordDataDirect();
    lastLogWrite = currentMillis;
  }

  // Handle script execution
  if (isScriptRunning && !isScriptPaused) {
    handleScripts();

    // Blink lock LED during script execution
    if (currentMillis - lastLockBlink >= 750) {
      lastLockBlink = currentMillis;
      lockLedState = !lockLedState;
      digitalWrite(lockLed, lockLedState ? HIGH : LOW);
    }
  }
  else {
    digitalWrite(lockLed, lock ? HIGH : LOW);
  }

  // Update clock display
  if (currentMillis - lastClockRefresh >= 1000) {
    lastClockRefresh = currentMillis;
    refreshHeaderClock();
  }

  // Handle physical switch inputs
  for (int i = 0; i < numSwitches; i++) {
    switchOutputs[i].debouncer.update();
    if (!lock && !safetyStop && !isScriptRunning) {
      if (switchOutputs[i].debouncer.fell()) {
        digitalWrite(switchOutputs[i].outputPin, HIGH);
        switchOutputs[i].state = HIGH;
        if (currentMode == MODE_MAIN) drawDeviceRow(i);
        if (currentMode == MODE_SCRIPT) updateLiveValueRow(i);
      }
      else if (switchOutputs[i].debouncer.rose()) {
        digitalWrite(switchOutputs[i].outputPin, LOW);
        switchOutputs[i].state = LOW;
        if (currentMode == MODE_MAIN) drawDeviceRow(i);
        if (currentMode == MODE_SCRIPT) updateLiveValueRow(i);
      }
    }
  }

  // Handle touch screen input
  if (ts.touched()) {
    if (currentMillis - lastTouchTime > touchDebounceMs) {
      TS_Point p = ts.getPoint();
      int16_t x = map(p.x, 200, 3800, 0, SCREEN_WIDTH);
      x = SCREEN_WIDTH - x;
      int16_t y = map(p.y, 200, 3800, SCREEN_HEIGHT, 0);

      // Route touch handling based on current mode
      switch(currentMode) {
        case MODE_MAIN:
          handleTouchMain(x, y);
          break;
        case MODE_SETTINGS:
          handleTouchSettings(x, y);
          break;
        case MODE_NETWORK:
          handleTouchNetwork(x, y);
          break;
        case MODE_NETWORK_EDIT:
          handleTouchNetworkEdit(x, y);
          break;
        case MODE_SCRIPT:
          handleTouchScript(x, y);
          break;
        case MODE_SCRIPT_LOAD:
          handleTouchScriptLoad(x, y);
          break;
        case MODE_EDIT:
          handleTouchEdit(x, y);
          break;
        case MODE_EDIT_LOAD:
          handleTouchScriptLoad(x, y);
          break;
        case MODE_EDIT_FIELD:
          handleTouchEditField(x, y);
          break;
        case MODE_DATE_TIME:
          handleTouchDateTime(x, y);
          break;
        case MODE_EDIT_SAVE:
          handleTouchEditSave(x,y);
          break;
        case MODE_EDIT_NAME:
          handleTouchEditName(x, y);
          break;
        case MODE_KEYPAD:
          handleTouchKeypad(x,y);
          break;
        case MODE_DELETE_CONFIRM:
          handleTouchDeleteConfirm(x, y);
          break;
        case MODE_ABOUT:
          handleTouchAbout(x, y);
          break;
        default:
          break;
      }
      lastTouchTime = currentMillis;
    }
  }

  // Update safety stop LED
  digitalWrite(stopLed, safetyStop ? HIGH : LOW);
}
