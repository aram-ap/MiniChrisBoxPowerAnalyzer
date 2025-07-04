#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
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
#include <NativeEthernet.h>
#include <NativeEthernetUdp.h>

// ==================== Version Information ====================
const char* SOFTWARE_VERSION = "Mini Chris Box V5.1 - Network Enabled";

// ==================== Network Configuration ====================
struct NetworkConfig {
  bool enableEthernet = true;
  bool useDHCP = true;
  uint32_t staticIP = 0xC0A80164;  // 192.168.1.100 as uint32_t
  uint32_t subnet = 0xFFFFFF00;   // 255.255.255.0 as uint32_t
  uint32_t gateway = 0xC0A80101;  // 192.168.1.1 as uint32_t
  uint32_t dns = 0x08080808;      // 8.8.8.8 as uint32_t
  uint16_t tcpPort = 8080;
  uint16_t udpPort = 8081;
} networkConfig;

// Network state
EthernetServer tcpServer(8080);
EthernetUDP udp;
EthernetClient tcpClients[5]; // Support up to 5 concurrent TCP connections
bool networkInitialized = false;
bool ethernetConnected = false;
unsigned long lastNetworkCheck = 0;
const unsigned long NETWORK_CHECK_INTERVAL = 5000; // Check every 5 seconds

// Data streaming configuration
struct StreamConfig {
  bool usbStreamEnabled = false;
  bool tcpStreamEnabled = false;
  bool udpStreamEnabled = false;
  unsigned long streamInterval = 100; // Default 100ms
  bool streamActiveOnly = false; // Stream only when devices are active
} streamConfig;

unsigned long lastStreamTime = 0;
bool streamingActive = false;

// Connection heartbeat
unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL = 10000; // 10 seconds
bool heartbeatEnabled = true;

// Command response buffer
char responseBuffer[1024];

// ==================== FIXED: Global SD Check Interval ====================
unsigned long SD_CHECK_INTERVAL = 2000; // 2 seconds - global variable

// ==================== LED Pins ====================
const int pwrLed = 22;
const int lockLed = 21;
const int stopLed = 20;

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
#define COLOR_DARK_ROW1 0x2104  // Dark blue-gray for alternating rows
#define COLOR_DARK_ROW2 0x18C3  // Slightly lighter dark blue-gray
#define COLOR_LIST_ROW1 0x1082  // FIXED: Alternating list backgrounds
#define COLOR_LIST_ROW2 0x0841  // FIXED: Darker alternating background

// ==================== Display/Touch Pins ====================
#define TFT_CS 10
#define TFT_DC 9
#define TFT_RST 7
#define TOUCH_CS 8
#define TOUCH_IRQ 14

Adafruit_ST7796S tft = Adafruit_ST7796S(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

const int SCREEN_WIDTH = 480;
const int SCREEN_HEIGHT = 320;

// ==================== SD Card Context Management ====================
#define SD_CS 36
#define BUILTIN_SDCARD 254
File logFile;
bool sdAvailable = false;
bool internalSdAvailable = false;
bool currentSDContext = false; // false = external, true = internal
unsigned long lastSDCheck = 0; // FIXED: Now used for smart auto-check
#define SCRIPTS_DIR "/scripts"

// ==================== Performance Timing ====================
#define SENSOR_UPDATE_INTERVAL 50    // Read sensors every 50ms
#define DISPLAY_UPDATE_INTERVAL 200  // Update display every 200ms
#define LOG_WRITE_INTERVAL 50        // Write logs every 50ms

unsigned long lastSensorUpdate = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastLogWrite = 0;

// Power LED flashing for recording
unsigned long lastPowerLedBlink = 0;
bool powerLedState = false;

// ==================== Serial Command Processing ====================
String serialBuffer = "";
bool csvOutput = false;
bool csvHeaderWritten = false;

// ==================== Network Command Buffer ====================
String networkCommandBuffer = "";

// ==================== Button Geometry ====================
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
ButtonRegion btnAbout = { 390, SCREEN_HEIGHT - 40, 80, 35, "About", false, COLOR_YELLOW, true };

// Settings input buttons
ButtonRegion btnFanSpeedInput = { 320, 80, 80, 30, "", false, COLOR_YELLOW, true };
ButtonRegion btnUpdateRateInput = { 320, 120, 80, 30, "", false, COLOR_YELLOW, true };
ButtonRegion btnSetTimeDate = { 320, 160, 80, 30, "Set", false, COLOR_YELLOW, true };
ButtonRegion btnTimeFormatToggle = { 320, 200, 80, 30, "24H", false, COLOR_YELLOW, true };
ButtonRegion btnDarkModeToggle = { 320, 240, 80, 30, "ON", false, COLOR_YELLOW, true };

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

// Script editing fields for device timing
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

// Script editing fields for T_START, T_END, Record
struct EditField {
  int x, y, w, h;
  char value[32];
  bool isSelected;
};

#define MAX_EDIT_FIELDS 10
EditField editFields[MAX_EDIT_FIELDS];
int numEditFields = 0;
int selectedField = -1;

// Script metadata structure
struct ScriptMetadata {
  char name[32];
  char filename[32];
  time_t dateCreated;
  time_t lastUsed;
};

// For script load/save dialogs
#define MAX_SCRIPTS 50
ScriptMetadata scriptList[MAX_SCRIPTS];
int numScripts = 0;
int scriptListOffset = 0;
int selectedScript = -1;
int highlightedScript = -1;

// Script sorting
enum SortMode { SORT_NAME, SORT_LAST_USED, SORT_DATE_CREATED };
SortMode currentSortMode = SORT_NAME;

// Delete confirmation
bool showDeleteConfirm = false;
char deleteScriptName[32] = "";

// For script name editing
char currentEditName[32] = "Untitled";
bool isEditingName = false;
bool shiftMode = false;
bool capsMode = false;
bool alphaMode = false;

// T9-style keypad variables
char lastKey = '\0';
unsigned long lastKeyTime = 0;
int currentLetterIndex = 0;
const unsigned long T9_TIMEOUT = 300; // 0.3 seconds

// ==================== INA226 Setup ====================
INA226 ina_gse1(0x40);
INA226 ina_gse2(0x41);
INA226 ina_ter(0x42);
INA226 ina_te1(0x43);
INA226 ina_te2(0x44);
INA226 ina_te3(0x45);
INA226 ina_all(0x46);
INA226* inaDevices[] = { &ina_gse1, &ina_gse2, &ina_ter, &ina_te1, &ina_te2, &ina_te3, &ina_all };
const char* inaNames[] = { "GSE-1", "GSE-2", "TE-R", "TE-1", "TE-2", "TE-3", "Total" };
const int numIna = 7;
float deviceVoltage[numIna];
float deviceCurrent[numIna];
float devicePower[numIna];

bool serialAvailable = false;

// ==================== Relay/Output Setup ====================
struct SwitchOutput {
  const char* name;
  int outputPin;
  int switchPin;
  Bounce debouncer;
  bool state;  // HIGH=ON, LOW=OFF
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

// ==================== Scripts & Timed Events ====================
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
bool scriptEndedEarly = false; // FIXED: Track if script ended early
bool lockStateBeforeScript = false;

// Track which devices have been turned on/off to prevent loops
bool deviceOnTriggered[6] = {false, false, false, false, false, false};
bool deviceOffTriggered[6] = {false, false, false, false, false, false};

// For blinking the lock LED every 0.75s while script is running
unsigned long lastLockBlink = 0;
bool lockLedState = false;

long scriptTimeSeconds = 0;

// ==================== Global State ====================
bool lock = false;
bool safetyStop = false;
bool lockBeforeStop = false;

// ==================== Touch Debounce ====================
unsigned long lastTouchTime = 0;
const unsigned long touchDebounceMs = 200;

// ==================== Recording State ====================
bool recording = false;
bool recordingScript = false;
unsigned long recordStartMillis = 0;
char recordFilename[64] = "power_data.json"; // FIXED: Larger buffer for script names
bool firstDataPoint = true;

// ==================== GUI Modes ====================
enum GUIMode {
  MODE_MAIN,
  MODE_SETTINGS,
  MODE_KEYPAD,
  MODE_SCRIPT,
  MODE_SCRIPT_LOAD,
  MODE_EDIT,
  MODE_EDIT_LOAD,
  MODE_EDIT_SAVE,
  MODE_EDIT_FIELD,
  MODE_DATE_TIME,
  MODE_EDIT_NAME,
  MODE_DELETE_CONFIRM,
  MODE_ABOUT
};
GUIMode currentMode = MODE_MAIN;
GUIMode previousMode = MODE_MAIN;

// ==================== FAN PWM ====================
#define FAN_PWM_PIN 33
bool fanOn = false;
int fanSpeed = 255;

// ==================== Update Rate ====================
unsigned long updateRate = 100;
unsigned long lastClockRefresh = 0;

// ==================== Time Format ====================
bool use24HourFormat = true;

// ==================== Dark Mode Setting ====================
bool darkMode = true;

// ==================== EEPROM ADDRESSES ====================
#define EEPROM_FAN_ON_ADDR     0
#define EEPROM_FAN_SPEED_ADDR  4
#define EEPROM_UPDATE_RATE_ADDR 8
#define EEPROM_TIME_FORMAT_ADDR 12
#define EEPROM_DARK_MODE_ADDR  16
#define EEPROM_NETWORK_CONFIG_ADDR 20

// ==================== Date/Time Editing ====================
tmElements_t tmSet;

// ==================== Keypad Setup ====================
enum KeypadMode {
  KEYPAD_NONE,
  KEYPAD_UPDATE_RATE,
  KEYPAD_FAN_SPEED,
  KEYPAD_SCRIPT_TSTART,
  KEYPAD_SCRIPT_TEND,
  KEYPAD_DEVICE_ON_TIME,
  KEYPAD_DEVICE_OFF_TIME,
  KEYPAD_SCRIPT_SEARCH,
  KEYPAD_SCRIPT_NAME
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

// T9 keypad mapping
const char* t9Letters[] = {
  "-_ ", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz", ""
};

// ==================== Function Prototypes ====================
void refreshHeaderClock();
void saveSettingsToEEPROM();
void loadSettingsFromEEPROM();
void applyFanSettings();
void applyUpdateRate();
void applyDarkMode();
void drawButton(ButtonRegion& btn, uint16_t bgColor, uint16_t textColor,
                const char* label, bool pressed=false, bool enabled=true);
void updateLockButton();
bool touchInButton(int16_t x, int16_t y, const ButtonRegion& btn);
void drawMainScreen();
void drawSettingsPanel();
void drawKeypadPanel();
void drawScriptPage();
void drawEditPage();
void drawScriptLoadPage();
void drawEditSavePage();
void drawDateTimePanel();
void drawDeleteConfirmDialog();
void drawAboutPage();

void updateSensorData();
void updateDisplayElements();

// SD Card context management
void ensureExternalSDContext();
void ensureInternalSDContext();

void recordDataDirect();

// FIXED: Smart SD card check functions
void smartCheckSDCard();
void checkInternalSD();

// Scripting & Timed Events
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

// Touch handling
void handleTouchMain(int16_t x, int16_t y);
void handleTouchSettings(int16_t x, int16_t y);
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

// Helpers
int getInaIndexForSwitch(int switchIdx);
void drawDeviceRow(int row);
void drawTotalRow();
void updateLiveValueRow(int row);
void setAllOutputsOff();
void syncOutputsToSwitches();
void serialPrint(String message);
void processSerialCommands();
void handleCommand(String command);
void setOutputState(String deviceName, bool state);
int findSwitchIndex(String deviceName);
void printCurrentStatus();
void printHelp();
void nextAvailableFilename(char* buf, size_t buflen);
void startRecording(bool scriptRequested=false);
void stopRecording();
String getCurrentTimeString();
String formatTimeHHMMSS(time_t t);
String formatDateString(time_t t);
String formatShortDateTime(time_t t);
void initRTC();
time_t getTeensyTime();
void setDateTime(tmElements_t tm);
void handleKeypadInput(char key);

// FIXED: Script-based filename generation
void generateScriptFilename(char* buf, size_t buflen, const char* scriptName);

// FIXED: Switch state synchronization after script
void syncSwitchesToOutputs();

// ==================== Network Function Prototypes ====================
void initNetwork();
void handleNetworkCommunication();
void handleTCPClients();
void handleUDPCommunication();
void sendLiveDataStream();
void processNetworkCommand(String command, Print* responseOutput);
void sendHeartbeat();
void checkNetworkStatus();
void saveNetworkConfig();
void loadNetworkConfig();
bool generateLiveDataJSON(char* buffer, size_t bufferSize);
bool generateStatusJSON(char* buffer, size_t bufferSize);
bool generateScriptListJSON(char* buffer, size_t bufferSize);
void sendResponse(const char* response, Print* output);
void networkPrint(const char* message);

// ==================== MOVED: Helper functions to convert between uint32_t and IPAddress ====================
IPAddress uint32ToIP(uint32_t ip) {
  return IPAddress((ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
}

uint32_t ipToUint32(IPAddress ip) {
  return ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) | ((uint32_t)ip[2] << 8) | (uint32_t)ip[3];
}

// Helper function to convert IPAddress to String - FIXED: NativeEthernet doesn't have toString()
String ipToString(IPAddress ip) {
  return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
}

// ==================== SD Card Context Management ====================

void ensureExternalSDContext() {
  if (currentSDContext != false) {
    SD.begin(SD_CS);
    currentSDContext = false;
//    Serial.println("Switched to external SD context");
  }
}

void ensureInternalSDContext() {
  if (currentSDContext != true) {
    SD.begin(BUILTIN_SDCARD);
    currentSDContext = true;
//    Serial.println("Switched to internal SD context");
  }
}

// ==================== Network Implementation ====================

void initNetwork() {
  if (!networkConfig.enableEthernet) {
    networkInitialized = false;
    ethernetConnected = false;
    return;
  }

  // Initialize Ethernet with MAC address
  byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

  Serial.println("Initializing Ethernet...");

  if (networkConfig.useDHCP) {
    if (Ethernet.begin(mac) == 0) {
      Serial.println("Failed to configure Ethernet using DHCP");
      ethernetConnected = false;
      networkInitialized = false;
      return;
    }
  } else {
    Ethernet.begin(mac, uint32ToIP(networkConfig.staticIP), uint32ToIP(networkConfig.dns),
                   uint32ToIP(networkConfig.gateway), uint32ToIP(networkConfig.subnet));
  }

  // Update server port if changed
  tcpServer = EthernetServer(networkConfig.tcpPort);
  tcpServer.begin();

  // Start UDP
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

void checkNetworkStatus() {
  if (!networkConfig.enableEthernet || !networkInitialized) return;

  unsigned long currentMillis = millis();
  if (currentMillis - lastNetworkCheck < NETWORK_CHECK_INTERVAL) return;

  lastNetworkCheck = currentMillis;

  // Check if Ethernet hardware is connected
  if (Ethernet.linkStatus() == LinkOFF) {
    if (ethernetConnected) {
      Serial.println("Ethernet cable disconnected");
      ethernetConnected = false;
    }
    return;
  }

  if (!ethernetConnected) {
    Serial.println("Ethernet cable connected - reinitializing...");
    initNetwork();
  }
}

void handleNetworkCommunication() {
  if (!networkInitialized || !ethernetConnected) return;

  handleTCPClients();
  handleUDPCommunication();

  // Send heartbeat
  if (heartbeatEnabled && (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL)) {
    sendHeartbeat();
    lastHeartbeat = millis();
  }
}

void handleTCPClients() {
  // Accept new connections
  EthernetClient newClient = tcpServer.available();
  if (newClient) {
    // Find an empty slot
    for (int i = 0; i < 5; i++) {
      if (!tcpClients[i] || !tcpClients[i].connected()) {
        tcpClients[i] = newClient;
        Serial.print("New TCP client connected: ");
        Serial.println(ipToString(newClient.remoteIP()));

        // Send welcome message - FIXED: Use JsonDocument
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

  // Handle existing connections
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
          if (networkCommandBuffer.length() > 512) { // Prevent buffer overflow
            networkCommandBuffer = "";
          }
        }
      }
    }
  }
}

void handleUDPCommunication() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char packetBuffer[512];
    int len = udp.read(packetBuffer, sizeof(packetBuffer) - 1);
    if (len > 0) {
      packetBuffer[len] = '\0';
      String command = String(packetBuffer);
      command.trim();

      // Process UDP command and send response back to sender
      udp.beginPacket(udp.remoteIP(), udp.remotePort());
      processNetworkCommand(command, &udp);
      udp.endPacket();
    }
  }
}

void processNetworkCommand(String command, Print* responseOutput) {
  command.trim();

  // Parse JSON command - FIXED: Use JsonDocument
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, command);

  if (error) {
    // Try processing as simple text command for backward compatibility
    handleCommand(command);
    return;
  }

  String cmd = doc["cmd"].as<String>();

  // Device Control Commands
  if (cmd == "set_output") {
    String device = doc["device"].as<String>();
    bool state = doc["state"].as<bool>();
    setOutputState(device, state);

    // FIXED: Use JsonDocument
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

    // FIXED: Use JsonDocument
    JsonDocument response;
    response["type"] = "command_response";
    response["cmd"] = "all_outputs";
    response["state"] = state;
    response["success"] = true;
    serializeJson(response, *responseOutput);
    responseOutput->println();
  }

  // System Control Commands
  else if (cmd == "lock") {
    bool lockState = doc["state"].as<bool>();
    bool prevLock = lock;
    lock = lockState;
    updateLockButton();
    if (!lock && prevLock) syncOutputsToSwitches();

    // FIXED: Use JsonDocument
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

    // FIXED: Use JsonDocument
    JsonDocument response;
    response["type"] = "command_response";
    response["cmd"] = "safety_stop";
    response["state"] = safetyStop;
    response["success"] = true;
    serializeJson(response, *responseOutput);
    responseOutput->println();
  }
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

  // Script Control Commands
  else if (cmd == "load_script") {
    String scriptName = doc["name"].as<String>();
    loadScriptFromFile((scriptName + ".json").c_str());

    // FIXED: Use JsonDocument
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

  // Settings Commands
  else if (cmd == "set_fan_speed") {
    int speed = doc["value"].as<int>();
    speed = constrain(speed, 0, 255);
    fanSpeed = speed;
    saveSettingsToEEPROM();
    applyFanSettings();

    // FIXED: Use JsonDocument
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
    rate = constrain(rate, 10UL, 5000UL); // FIXED: Use UL for unsigned literals
    updateRate = rate;
    saveSettingsToEEPROM();

    // FIXED: Use JsonDocument
    JsonDocument response;
    response["type"] = "command_response";
    response["cmd"] = "set_update_rate";
    response["value"] = updateRate;
    response["success"] = true;
    serializeJson(response, *responseOutput);
    responseOutput->println();
  }

  // Data Request Commands
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
    streamConfig.usbStreamEnabled = (responseOutput == &Serial);
    streamConfig.tcpStreamEnabled = true;
    streamConfig.udpStreamEnabled = false;
    streamingActive = true;

    // FIXED: Use JsonDocument
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

  // Send to UDP if enabled (would need to store client addresses)
  if (streamConfig.udpStreamEnabled) {
    // UDP streaming would require storing client addresses
    // This could be implemented if needed
  }
}

bool generateLiveDataJSON(char* buffer, size_t bufferSize) {
  // FIXED: Use JsonDocument
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
      device["current"] = deviceCurrent[inaIdx];
      device["power"] = devicePower[inaIdx];
    } else {
      device["voltage"] = 0.0;
      device["current"] = 0.0;
      device["power"] = 0.0;
    }
  }

  // Add total power
  JsonObject total = devices.add<JsonObject>();
  total["name"] = "Total";
  total["state"] = false;
  if (numIna > 6) {
    total["voltage"] = deviceVoltage[6];
    total["current"] = deviceCurrent[6];
    total["power"] = devicePower[6];
  }

  size_t len = serializeJson(doc, buffer, bufferSize);
  return (len > 0 && len < bufferSize);
}

bool generateStatusJSON(char* buffer, size_t bufferSize) {
  // FIXED: Use JsonDocument
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
    doc["ip_address"] = ipToString(Ethernet.localIP()); // FIXED: Use custom ipToString function
    doc["tcp_port"] = networkConfig.tcpPort;
    doc["udp_port"] = networkConfig.udpPort;
  }

  size_t len = serializeJson(doc, buffer, bufferSize);
  return (len > 0 && len < bufferSize);
}

bool generateScriptListJSON(char* buffer, size_t bufferSize) {
  // FIXED: Use JsonDocument
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

void sendHeartbeat() {
  // FIXED: Use JsonDocument
  JsonDocument doc;
  doc["type"] = "heartbeat";
  doc["timestamp"] = getCurrentTimeString();
  doc["uptime"] = millis();

  // Send to all connected TCP clients
  for (int i = 0; i < 5; i++) {
    if (tcpClients[i] && tcpClients[i].connected()) {
      serializeJson(doc, tcpClients[i]);
      tcpClients[i].println();
    }
  }
}

void sendResponse(const char* response, Print* output) {
  output->println(response);
}

void saveNetworkConfig() {
  EEPROM.put(EEPROM_NETWORK_CONFIG_ADDR, networkConfig);
}

void loadNetworkConfig() {
  NetworkConfig defaultConfig;
  EEPROM.get(EEPROM_NETWORK_CONFIG_ADDR, networkConfig);

  // Validate loaded config
  if (networkConfig.tcpPort < 1024 || networkConfig.tcpPort > 65535) {
    networkConfig.tcpPort = defaultConfig.tcpPort;
  }
  if (networkConfig.udpPort < 1024 || networkConfig.udpPort > 65535) {
    networkConfig.udpPort = defaultConfig.udpPort;
  }
}

// ==================== FIXED: Smart SD Card Check Functions ====================

void smartCheckSDCard() {
//  Serial.println("Smart SD card check initiated...");

  ensureExternalSDContext();

  bool nowAvailable = false;
  if (SD.begin(SD_CS)) {
    File root = SD.open("/");
    if (root) {
      nowAvailable = true;
      root.close();
//      Serial.println("External SD card: Available");
    } else {
//      Serial.println("External SD card: Cannot access root directory");
    }
  } else {
//    Serial.println("External SD card: Cannot initialize");
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

//  Serial.print("SD card status updated: ");
//  Serial.println(sdAvailable ? "Available" : "Not Available");
}

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
//    Serial.print("Internal SD card status: ");
//    Serial.println(internalSdAvailable ? "Available" : "Not Available");
  }
}

// ==================== Optimized Main Functions ====================

void updateSensorData() {
  for (int i = 0; i < numIna; i++) {
    deviceVoltage[i] = inaDevices[i]->getBusVoltage();
    deviceCurrent[i] = inaDevices[i]->getCurrent_mA();
    devicePower[i]   = inaDevices[i]->getPower_mW() / 1000.0;
  }
}

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

// ==================== Direct Recording Functions ====================

void recordDataDirect() {
  if (!recording || !logFile) return;

  ensureExternalSDContext();

  if (!logFile.available() && logFile.size() == 0) {
    Serial.println("Log file became invalid - stopping recording");
    stopRecording();
    return;
  }

  unsigned long t = (millis() - recordStartMillis);

  if (csvOutput) {
    logFile.print(t);
    for (int i = 0; i < numSwitches; i++) {
      int inaIdx = getInaIndexForSwitch(i);
      logFile.print(",");
      logFile.print(switchOutputs[i].state ? "1" : "0");
      logFile.print(",");
      logFile.print(inaIdx >= 0 ? deviceVoltage[inaIdx] : 0.0f, 4);
      logFile.print(",");
      logFile.print(inaIdx >= 0 ? deviceCurrent[inaIdx] : 0.0f, 4);
      logFile.print(",");
      logFile.print(inaIdx >= 0 ? devicePower[inaIdx] : 0.0f, 4);
    }
    logFile.println();
  } else {
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
      logFile.print(inaIdx >= 0 ? deviceCurrent[inaIdx] : 0.0f, 4);

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

  static int flushCounter = 0;
  if (++flushCounter >= 3) {
    logFile.flush();
    flushCounter = 0;
  }
}

// ==================== Button Drawing Function ====================
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

// ==================== Setup ====================
void setup() {
  Serial.begin(2000000);
  Wire.begin();

  initRTC();

  // Load network configuration
  loadNetworkConfig();

  for (int i = 0; i < numIna; i++) {
    inaDevices[i]->begin();
    inaDevices[i]->setMaxCurrentShunt(8, 0.01);
  }

  for (int i = 0; i < numSwitches; i++) {
    if (switchOutputs[i].switchPin == -1) {
      continue;
    }
    pinMode(switchOutputs[i].switchPin, INPUT_PULLUP);
    pinMode(switchOutputs[i].outputPin, OUTPUT);
    switchOutputs[i].debouncer.attach(switchOutputs[i].switchPin);
    switchOutputs[i].debouncer.interval(10);
  }

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

  pinMode(FAN_PWM_PIN, OUTPUT);

  pinMode(pwrLed, OUTPUT);
  pinMode(lockLed, OUTPUT);
  pinMode(stopLed, OUTPUT);

  digitalWrite(pwrLed, HIGH);
  digitalWrite(lockLed, LOW);
  digitalWrite(stopLed, LOW);

  loadSettingsFromEEPROM();

  tft.init(320, 480, 0, 0, ST7796S_BGR);
  applyDarkMode();
  tft.setRotation(1);
  ts.begin();
  ts.setRotation(1);
  tft.fillScreen(COLOR_BLACK);

  // FIXED: Initial smart SD card check
  smartCheckSDCard();
  checkInternalSD();

  applyFanSettings();
  applyUpdateRate();

  createNewScript();
  loadAllScriptNames();

  // Initialize network
  initNetwork();

  drawMainScreen();

  if (Serial.available()) {
    serialAvailable = true;
    Serial.println("Teensy 4.1 Power Controller Ready - Network Enabled");
    Serial.println("Type 'help' for available commands");
    if (ethernetConnected) {
      Serial.print("Network: TCP/");
      Serial.print(networkConfig.tcpPort);
      Serial.print(" UDP/");
      Serial.print(networkConfig.udpPort);
      Serial.print(" IP: ");
      Serial.println(ipToString(Ethernet.localIP()));
    }
  }
}

// ==================== FIXED: Main Loop with Smart SD Checking and Network ====================
void loop() {
  unsigned long currentMillis = millis();

  processSerialCommands();

  // Handle network communication (non-blocking)
  handleNetworkCommunication();

  // Check network status periodically
  checkNetworkStatus();

  // FIXED: Move streaming logic outside network function for USB support
  if (streamingActive && (currentMillis - lastStreamTime >= streamConfig.streamInterval)) {
    sendLiveDataStream();
    lastStreamTime = currentMillis;
  }

  // FIXED: Smart SD checking - only when NOT recording
  if (!recording && (currentMillis - lastSDCheck > SD_CHECK_INTERVAL)) {
    smartCheckSDCard();
    checkInternalSD();
    lastSDCheck = currentMillis;
  }

  char key = keypad.getKey();
  if (key == 'B') {
    if (currentMode != MODE_KEYPAD && currentMode != MODE_EDIT_SAVE && currentMode != MODE_EDIT_NAME) {
      handleUniversalBackButton();
    } else {
      handleKeypadInput(key);
    }
  }
  // Handle script selection keypad with early return
  if (currentMode == MODE_SCRIPT_LOAD && key >= '1' && key <= '9') {
    int scriptNum = key - '0';
    if (scriptNum <= numScripts) {
      selectedScript = scriptNum - 1;
      highlightedScript = scriptNum - 1;
      scriptListOffset = max(0, (scriptNum - 1) - 5);
      drawScriptLoadPage();
      return; // Early return ONLY for script selection
    }
  }

  if (recording && (currentMillis - lastPowerLedBlink >= 500)) {
    lastPowerLedBlink = currentMillis;
    powerLedState = !powerLedState;
    digitalWrite(pwrLed, powerLedState ? HIGH : LOW);
  } else if (!recording) {
    digitalWrite(pwrLed, HIGH);
  }

  if (currentMode == MODE_KEYPAD || currentMode == MODE_EDIT_SAVE || currentMode == MODE_EDIT_NAME) {
    if (key && key != 'B') {
      handleKeypadInput(key);
    }
  }

  if (currentMillis - lastSensorUpdate >= SENSOR_UPDATE_INTERVAL) {
    updateSensorData();
    lastSensorUpdate = currentMillis;
  }

  if (currentMillis - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    updateDisplayElements();
    lastDisplayUpdate = currentMillis;
  }

  if (recording && (currentMillis - lastLogWrite >= LOG_WRITE_INTERVAL)) {
    recordDataDirect();
    lastLogWrite = currentMillis;
  }

  if (isScriptRunning && !isScriptPaused) {
    handleScripts();

    if (currentMillis - lastLockBlink >= 750) {
      lastLockBlink = currentMillis;
      lockLedState = !lockLedState;
      digitalWrite(lockLed, lockLedState ? HIGH : LOW);
    }
  }
  else {
    digitalWrite(lockLed, lock ? HIGH : LOW);
  }

  if (currentMillis - lastClockRefresh >= 1000) {
    lastClockRefresh = currentMillis;
    refreshHeaderClock();
  }

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

  if (ts.touched()) {
    if (currentMillis - lastTouchTime > touchDebounceMs) {
      TS_Point p = ts.getPoint();
      int16_t x = map(p.x, 200, 3800, 0, SCREEN_WIDTH);
      x = SCREEN_WIDTH - x;
      int16_t y = map(p.y, 200, 3800, SCREEN_HEIGHT, 0);

      switch(currentMode) {
        case MODE_MAIN:
          handleTouchMain(x, y);
          break;
        case MODE_SETTINGS:
          handleTouchSettings(x, y);
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

  digitalWrite(stopLed, safetyStop ? HIGH : LOW);
}

// ==================== Enhanced Serial Command Processing ====================
void processSerialCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) {
        // Try to parse as JSON first, then fall back to text commands - FIXED: Use JsonDocument
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, serialBuffer);

        if (!error) {
          // Process as network command through serial
          processNetworkCommand(serialBuffer, &Serial);
        } else {
          // Process as legacy text command
          handleCommand(serialBuffer);
        }
        serialBuffer = "";
      }
    } else {
      serialBuffer += c;
    }
  }
}

// ==================== teensy_remaining_functions.ino ====================

// ==================== Universal Back Button Handler ====================
void handleUniversalBackButton() {
  switch(currentMode) {
    case MODE_SETTINGS:
      currentMode = MODE_MAIN;
      drawMainScreen();
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

// ==================== RTC Functions ====================
void initRTC() {
  setSyncProvider(getTeensyTime);

  if (timeStatus() != timeSet) {
    Serial.println("Unable to sync with the RTC");
    setTime(0, 0, 0, 1, 1, 2025);
  } else {
    Serial.println("RTC has set the system time");
  }
}

time_t getTeensyTime() {
  return Teensy3Clock.get();
}

void setDateTime(tmElements_t tm) {
  time_t t = makeTime(tm);
  setTime(t);
  Teensy3Clock.set(t);
}

// =============================
//       SCRIPT LOGIC
// =============================
void handleScripts() {
  unsigned long totalPausedTime = scriptPausedTime;
  if (isScriptPaused) {
    totalPausedTime += (millis() - pauseStartMillis);
  }

  unsigned long msSinceStart = millis() - scriptStartMillis - totalPausedTime;
  long currentSecond = currentScript.tStart + (long)(msSinceStart / 1000);
  scriptTimeSeconds = currentSecond;

  if (currentSecond >= currentScript.tEnd) {
    stopScript(false); // Natural completion
    return;
  }

  for (int i = 0; i < 6; i++) {
    if (!currentScript.devices[i].enabled) continue;

    if (currentSecond >= currentScript.devices[i].onTime &&
        !deviceOnTriggered[i] && switchOutputs[i].state == LOW) {
      digitalWrite(switchOutputs[i].outputPin, HIGH);
      switchOutputs[i].state = HIGH;
      deviceOnTriggered[i] = true;
      if (currentMode == MODE_SCRIPT) {
        updateLiveValueRow(i);
      }
    }

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

void startScript() {
  if (isScriptRunning || safetyStop) return;

  lockStateBeforeScript = lock;

  setAllOutputsOff();

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
  scriptEndedEarly  = false; // Reset early end flag

  currentScript.lastUsed = now();

  if (currentScript.useRecord) {
    startRecording(true);
  }

  if (currentMode == MODE_SCRIPT) {
    drawScriptPage();
  }
}

void pauseScript() {
  if (!isScriptRunning || isScriptPaused) return;

  isScriptPaused = true;
  pauseStartMillis = millis();

  if (currentMode == MODE_SCRIPT) {
    drawScriptPage();
  }
}

void resumeScript() {
  if (!isScriptRunning || !isScriptPaused) return;

  scriptPausedTime += (millis() - pauseStartMillis);
  isScriptPaused = false;

  if (currentMode == MODE_SCRIPT) {
    drawScriptPage();
  }
}

void stopScript(bool userEnded) {
  if (!isScriptRunning) return;
  isScriptRunning = false;
  isScriptPaused = false;

  for (int i = 0; i < 6; i++) {
    deviceOnTriggered[i] = false;
    deviceOffTriggered[i] = false;
  }

  lock = lockStateBeforeScript;
  updateLockButton();

  scriptEndedEarly = userEnded; // FIXED: Track if ended early

  if (recordingScript) {
    stopRecording();
  }

  // FIXED: Sync switch states to output states after script ends
  if (!safetyStop) { // Only sync if not in safety stop
    syncSwitchesToOutputs();
  }

  if (currentMode == MODE_SCRIPT) {
    drawScriptPage();
  }
}

// FIXED: New function to sync switches to current output states
void syncSwitchesToOutputs() {
  for (int i = 0; i < numSwitches; i++) {
    if (switchOutputs[i].switchPin == -1) continue;

    int switchState = digitalRead(switchOutputs[i].switchPin);
    bool switchOn = (switchState == LOW); // Assuming switch is active low

    if (switchOn && switchOutputs[i].state == LOW) {
      // Switch is ON but output is OFF - turn output ON
      digitalWrite(switchOutputs[i].outputPin, HIGH);
      switchOutputs[i].state = HIGH;
      if (currentMode == MODE_MAIN) drawDeviceRow(i);
      if (currentMode == MODE_SCRIPT) updateLiveValueRow(i);
    }
    else if (!switchOn && switchOutputs[i].state == HIGH) {
      // Switch is OFF but output is ON - turn output OFF
      digitalWrite(switchOutputs[i].outputPin, LOW);
      switchOutputs[i].state = LOW;
      if (currentMode == MODE_MAIN) drawDeviceRow(i);
      if (currentMode == MODE_SCRIPT) updateLiveValueRow(i);
    }
  }
}

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

// FIXED: Generate script-based filename
void generateScriptFilename(char* buf, size_t buflen, const char* scriptName) {
  ensureExternalSDContext();

  const char* ext = csvOutput ? ".csv" : ".json";

  // Clean script name (remove invalid characters)
  char cleanName[32];
  int cleanPos = 0;
  size_t nameLen = strlen(scriptName); // FIXED: Store length to avoid repeated calls
  for (size_t i = 0; i < nameLen && cleanPos < 31; i++) { // FIXED: Use size_t for loop variable
    char c = scriptName[i];
    if (isalnum(c) || c == '-' || c == '_') {
      cleanName[cleanPos++] = c;
    } else if (c == ' ') {
      cleanName[cleanPos++] = '_';
    }
  }
  cleanName[cleanPos] = '\0';

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
      scriptList[numScripts].name[31] = '\0'; // FIXED: Ensure null termination

      char filePath[64];
      snprintf(filePath, sizeof(filePath), "%s/%s", SCRIPTS_DIR, entry.name());
      File scriptFile = SD.open(filePath, FILE_READ);
      if (scriptFile) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, scriptFile);
        if (!error) {
          scriptList[numScripts].dateCreated = doc["dateCreated"] | now();
          scriptList[numScripts].lastUsed = doc["lastUsed"] | now();
        } else {
          scriptList[numScripts].dateCreated = now();
          scriptList[numScripts].lastUsed = now();
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

void deleteScript(const char* scriptName) {
  ensureInternalSDContext();

  char filePath[64];
  snprintf(filePath, sizeof(filePath), "%s/%s", SCRIPTS_DIR, scriptName);

  if (SD.exists(filePath)) {
    SD.remove(filePath);
    loadAllScriptNames();
  }
}

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
  currentScript.scriptName[sizeof(currentScript.scriptName) - 1] = '\0'; // FIXED: Ensure null termination
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

  Serial.print("Loaded script: ");
  Serial.println(currentScript.scriptName);
}

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

// =============================
//   TOUCH HANDLERS
// =============================
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
  if (pressedIdx >= 0) {
    ButtonRegion* btn = buttons[pressedIdx];
    drawButton(*btn, btn->color, COLOR_WHITE, btn->label, true, btn->enabled);
    delay(80);
    drawButton(*btn, btn->color, COLOR_BLACK, btn->label, false, btn->enabled);
  }

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

void handleTouchSettings(int16_t x, int16_t y) {
  if (touchInButton(x, y, btnSettingsBack)) {
    currentMode = MODE_MAIN;
    drawMainScreen();
    return;
  }
  if (touchInButton(x, y, btnSettingsStop)) {
    if (!safetyStop) {
      lockBeforeStop = lock;
      safetyStop = true;
      setAllOutputsOff();
      drawButton(btnSettingsStop, COLOR_PURPLE, COLOR_BLACK, "RELEASE",
                 false, btnSettingsStop.enabled);
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
      drawButton(btnSettingsStop, COLOR_YELLOW, COLOR_BLACK, "STOP",
                 false, btnSettingsStop.enabled);
      if (!lock && prevLock) syncOutputsToSwitches();
    }
    return;
  }
  if (touchInButton(x, y, btnFanSpeedInput)) {
    currentMode  = MODE_KEYPAD;
    keypadMode   = KEYPAD_FAN_SPEED;
    keypadPos    = 0;
    keypadBuffer[0] = 0;
    drawKeypadPanel();
    return;
  }
  if (touchInButton(x, y, btnUpdateRateInput)) {
    currentMode  = MODE_KEYPAD;
    keypadMode   = KEYPAD_UPDATE_RATE;
    keypadPos    = 0;
    keypadBuffer[0] = 0;
    drawKeypadPanel();
    return;
  }
  if (touchInButton(x, y, btnSetTimeDate)) {
    time_t t = now();
    breakTime(t, tmSet);
    currentMode = MODE_DATE_TIME;
    drawDateTimePanel();
    return;
  }
  if (touchInButton(x, y, btnTimeFormatToggle)) {
    use24HourFormat = !use24HourFormat;
    saveSettingsToEEPROM();
    drawSettingsPanel();
    return;
  }
  if (touchInButton(x, y, btnDarkModeToggle)) {
    darkMode = !darkMode;
    applyDarkMode();
    saveSettingsToEEPROM();
    drawSettingsPanel();
    return;
  }
  if (touchInButton(x, y, btnAbout)) {
    currentMode = MODE_ABOUT;
    drawAboutPage();
    return;
  }
}

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

void handleTouchEdit(int16_t x, int16_t y) {
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

  // FIXED: Remove search box touch handling - now handled by direct keypad input
}

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

void handleTouchEditField(int16_t x, int16_t y) {
  if (touchInButton(x, y, btnEditFieldBack)) {  // Change from btnModalBack
    currentMode = MODE_EDIT;
    drawEditPage();
    return;
  }
  currentMode = MODE_EDIT;
  drawEditPage();
}

// FIXED: Proper back button handling for edit save
void handleTouchEditSave(int16_t x, int16_t y) {
  if (touchInButton(x, y, btnEditSaveBack)) {
    currentMode = MODE_EDIT;
    drawEditPage();
    return;
  }
}

// FIXED: Proper back button handling for edit name
void handleTouchEditName(int16_t x, int16_t y) {
  if (touchInButton(x, y, btnEditNameBack)) {
    currentMode = MODE_EDIT;
    drawEditPage();
    return;
  }
}

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

  // Year field
  if (x >= fieldX && x <= (fieldX + fieldWidth) && y >= 70 && y <= (70 + fieldHeight)) {
    tmSet.Year = constrain(tmSet.Year + 1, 25, 99);
    drawDateTimePanel();
    return;
  }
  if (x >= (fieldX - 30) && x <= fieldX && y >= 70 && y <= (70 + fieldHeight)) {
    tmSet.Year = constrain(tmSet.Year - 1, 25, 99);
    drawDateTimePanel();
    return;
  }

  // Month field
  if (x >= fieldX && x <= (fieldX + fieldWidth) && y >= 110 && y <= (110 + fieldHeight)) {
    tmSet.Month = tmSet.Month % 12 + 1;
    drawDateTimePanel();
    return;
  }
  if (x >= (fieldX - 30) && x <= fieldX && y >= 110 && y <= (110 + fieldHeight)) {
    tmSet.Month = (tmSet.Month + 10) % 12 + 1;
    drawDateTimePanel();
    return;
  }

  // Day field
  if (x >= fieldX && x <= (fieldX + fieldWidth) && y >= 150 && y <= (150 + fieldHeight)) {
    tmSet.Day = tmSet.Day % 31 + 1;
    drawDateTimePanel();
    return;
  }
  if (x >= (fieldX - 30) && x <= fieldX && y >= 150 && y <= (150 + fieldHeight)) {
    tmSet.Day = (tmSet.Day + 29) % 31 + 1;
    drawDateTimePanel();
    return;
  }

  // Hour field
  if (x >= fieldX && x <= (fieldX + fieldWidth) && y >= 190 && y <= (190 + fieldHeight)) {
    tmSet.Hour = (tmSet.Hour + 1) % 24;
    drawDateTimePanel();
    return;
  }
  if (x >= (fieldX - 30) && x <= fieldX && y >= 190 && y <= (190 + fieldHeight)) {
    tmSet.Hour = (tmSet.Hour + 23) % 24;
    drawDateTimePanel();
    return;
  }

  // Minute field
  if (x >= fieldX && x <= (fieldX + fieldWidth) && y >= 230 && y <= (230 + fieldHeight)) {
    tmSet.Minute = (tmSet.Minute + 1) % 60;
    drawDateTimePanel();
    return;
  }
  if (x >= (fieldX - 30) && x <= fieldX && y >= 230 && y <= (230 + fieldHeight)) {
    tmSet.Minute = (tmSet.Minute + 59) % 60;
    drawDateTimePanel();
    return;
  }

  // Second field
  if (x >= fieldX && x <= (fieldX + fieldWidth) && y >= 270 && y <= (270 + fieldHeight)) {
    tmSet.Second = (tmSet.Second + 1) % 60;
    drawDateTimePanel();
    return;
  }
  if (x >= (fieldX - 30) && x <= fieldX && y >= 270 && y <= (270 + fieldHeight)) {
    tmSet.Second = (tmSet.Second + 59) % 60;
    drawDateTimePanel();
    return;
  }
}

void handleKeypadInput(char key) {
  if (currentMode == MODE_KEYPAD) {
    if (keypadMode == KEYPAD_DEVICE_ON_TIME || keypadMode == KEYPAD_DEVICE_OFF_TIME ||
        keypadMode == KEYPAD_SCRIPT_TSTART || keypadMode == KEYPAD_SCRIPT_TEND ||
        keypadMode == KEYPAD_UPDATE_RATE || keypadMode == KEYPAD_FAN_SPEED) {

      if (key >= '0' && key <= '9') {
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
        if (keypadBuffer[0] == '-') {
          memmove(keypadBuffer, keypadBuffer + 1, keypadPos);
          keypadPos--;
        } else {
          memmove(keypadBuffer + 1, keypadBuffer, keypadPos + 1);
          keypadBuffer[0] = '-';
          keypadPos++;
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
          default:
            break;
        }
        return;
      }
      else if (key == 'B') {
        currentMode = (keypadMode == KEYPAD_UPDATE_RATE || keypadMode == KEYPAD_FAN_SPEED) ?
                      MODE_SETTINGS : MODE_EDIT;
        keypadMode = KEYPAD_NONE;
        if (currentMode == MODE_SETTINGS) drawSettingsPanel();
        else drawEditPage();
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

// FIXED: Proper back button handling for keypad
void handleTouchKeypad(int16_t x, int16_t y) {
  if (touchInButton(x, y, btnKeypadBack)) {  // Change from btnModalBack
    currentMode = (keypadMode == KEYPAD_UPDATE_RATE || keypadMode == KEYPAD_FAN_SPEED) ?
                  MODE_SETTINGS : MODE_EDIT;
    keypadMode = KEYPAD_NONE;
    if (currentMode == MODE_SETTINGS) drawSettingsPanel();
    else drawEditPage();
    return;
  }
}

// ==================== Drawing Functions ====================
void drawMainScreen() {
  tft.fillScreen(COLOR_BLACK);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  int16_t x1, y1;
  uint16_t w, h;

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

  for (int i = 0; i < numSwitches; i++) {
    drawDeviceRow(i);
  }

  int totalRowY = 85 + numSwitches * 25 + 10;
  tft.drawLine(5, totalRowY - 5, SCREEN_WIDTH - 5, totalRowY - 5, COLOR_GRAY);
  drawTotalRow();
}

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

  tft.fillRect(20, 70, 460, 30, COLOR_DARK_ROW1);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 90);
  tft.print("Fan Speed (0-255):");
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", fanSpeed);
  drawButton(btnFanSpeedInput, COLOR_YELLOW, COLOR_BLACK, buf, false, true);

  tft.fillRect(20, 110, 460, 30, COLOR_DARK_ROW2);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 130);
  tft.print("Update Rate (ms):");
  char buf2[12];
  snprintf(buf2, sizeof(buf2), "%lu", updateRate);
  drawButton(btnUpdateRateInput, COLOR_YELLOW, COLOR_BLACK, buf2, false, true);

  tft.fillRect(20, 150, 460, 30, COLOR_DARK_ROW1);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 170);
  tft.print("RTC Clock:");
  drawButton(btnSetTimeDate, COLOR_YELLOW, COLOR_BLACK, "Set", false, true);

  tft.fillRect(20, 190, 460, 30, COLOR_DARK_ROW2);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 210);
  tft.print("Time Format:");
  drawButton(btnTimeFormatToggle, COLOR_YELLOW, COLOR_BLACK, use24HourFormat ? "24H" : "12H", false, true);

  tft.fillRect(20, 230, 460, 30, COLOR_DARK_ROW1);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(30, 250);
  tft.print("Dark Mode:");
  drawButton(btnDarkModeToggle, COLOR_YELLOW, COLOR_BLACK, darkMode ? "ON" : "OFF", false, true);

  drawButton(btnAbout, COLOR_YELLOW, COLOR_BLACK, "About", false, true);

  tft.setTextColor(COLOR_GRAY);
  tft.setCursor(30, 280);
  tft.print(formatDateString(now()));
  tft.print(" ");
  tft.print(getCurrentTimeString());
}

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
  tft.print("Permission is hereby granted, free of charge, to any person");
  tft.setCursor(30, 160);
  tft.print("obtaining a copy of this software and associated documentation");
  tft.setCursor(30, 175);
  tft.print("files (the \"Software\"), to deal in the Software without");
  tft.setCursor(30, 190);
  tft.print("restriction, including without limitation the rights to use,");
  tft.setCursor(30, 205);
  tft.print("copy, modify, merge, publish, distribute, sublicense, and/or");
  tft.setCursor(30, 220);
  tft.print("sell copies of the Software, and to permit persons to whom");
  tft.setCursor(30, 235);
  tft.print("the Software is furnished to do so, subject to the above");
  tft.setCursor(30, 250);
  tft.print("copyright notice being included in all copies.");
}

void drawKeypadPanel() {
  tft.fillScreen(COLOR_BLACK);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(40, 50);

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
    default:
      tft.print("Enter Value:");
      break;
  }

  tft.setFont(&FreeMonoBold9pt7b);
  tft.setCursor(40, 90);
  tft.print(keypadBuffer);

  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(40, 160);

  if (keypadMode == KEYPAD_DEVICE_ON_TIME || keypadMode == KEYPAD_DEVICE_OFF_TIME ||
      keypadMode == KEYPAD_SCRIPT_TSTART || keypadMode == KEYPAD_SCRIPT_TEND ||
      keypadMode == KEYPAD_UPDATE_RATE || keypadMode == KEYPAD_FAN_SPEED) {
    tft.print("*=Backspace, #=+/-, A=Enter, B=Back, C=Clear");
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

void drawScriptPage() {
  tft.fillScreen(COLOR_BLACK);

  drawButton(btnScriptBack, COLOR_YELLOW, COLOR_BLACK, "Back");
  drawButton(btnScriptStop, COLOR_YELLOW, COLOR_BLACK, "STOP");

  tft.setTextColor(COLOR_WHITE);
  char buff[64];

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

  int configY = baseY + 6 * rowHeight + 25;
  tft.setCursor(10, configY);
  tft.print("Start: ");
  tft.print(currentScript.tStart);
  tft.print("  Stop: ");
  tft.print(currentScript.tEnd);
  tft.print("  Record: ");
  tft.print(currentScript.useRecord ? "Yes" : "No");

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
      tft.printf("%.1fV %.0fmA", deviceVoltage[inaIdx], deviceCurrent[inaIdx]);
    }
  }

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

void drawEditPage() {
  tft.fillScreen(COLOR_BLACK);

  drawButton(btnEditBack, COLOR_YELLOW, COLOR_BLACK, "Back");
  drawButton(btnEditStop, COLOR_YELLOW, COLOR_BLACK, "STOP");

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

// FIXED: Script load page with alternating row backgrounds
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

    // FIXED: Alternating row backgrounds
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

  if (numScripts > 10) {
    if (scriptListOffset > 0) {
      tft.fillTriangle(450, 70, 440, 80, 460, 80, COLOR_YELLOW);
    }

    if (scriptListOffset < (numScripts - 10)) {
      tft.fillTriangle(450, 240, 440, 230, 460, 230, COLOR_YELLOW);
    }
  }

  // FIXED: Remove search box - now using direct keypad input
  tft.setTextColor(COLOR_GRAY);
  tft.setCursor(120, 300);
  tft.print("Press 1-9 to select script");

  if (selectedScript >= 0) {
    drawButton(btnScriptSelect, COLOR_GREEN, COLOR_BLACK, "Select", false, true);
    drawButton(btnScriptDelete, COLOR_RED, COLOR_BLACK, "Delete", false, true);
  }
}

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

  // Year
  tft.setCursor(50, 80);
  tft.print("Year:");
  tft.fillRect(150, 70, 30, 30, COLOR_GRAY);
  tft.drawRect(150, 70, 30, 30, COLOR_BLACK);
  tft.setTextColor(COLOR_BLACK);
  tft.setCursor(158, 90);
  tft.print("-");

  tft.fillRect(180, 70, 60, 30, COLOR_YELLOW);
  tft.drawRect(180, 70, 60, 30, COLOR_BLACK);
  tft.setCursor(190, 90);
  tft.print("20");
  tft.print(tmSet.Year);

  tft.fillRect(240, 70, 30, 30, COLOR_GRAY);
  tft.drawRect(240, 70, 30, 30, COLOR_BLACK);
  tft.setCursor(248, 90);
  tft.print("+");

  // Month
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(50, 120);
  tft.print("Month:");
  tft.fillRect(150, 110, 30, 30, COLOR_GRAY);
  tft.drawRect(150, 110, 30, 30, COLOR_BLACK);
  tft.setTextColor(COLOR_BLACK);
  tft.setCursor(158, 130);
  tft.print("-");

  tft.fillRect(180, 110, 60, 30, COLOR_YELLOW);
  tft.drawRect(180, 110, 60, 30, COLOR_BLACK);
  tft.setCursor(195, 130);
  tft.print(tmSet.Month);

  tft.fillRect(240, 110, 30, 30, COLOR_GRAY);
  tft.drawRect(240, 110, 30, 30, COLOR_BLACK);
  tft.setCursor(248, 130);
  tft.print("+");

  // Day
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(50, 160);
  tft.print("Day:");
  tft.fillRect(150, 150, 30, 30, COLOR_GRAY);
  tft.drawRect(150, 150, 30, 30, COLOR_BLACK);
  tft.setTextColor(COLOR_BLACK);
  tft.setCursor(158, 170);
  tft.print("-");

  tft.fillRect(180, 150, 60, 30, COLOR_YELLOW);
  tft.drawRect(180, 150, 60, 30, COLOR_BLACK);
  tft.setCursor(195, 170);
  tft.print(tmSet.Day);

  tft.fillRect(240, 150, 30, 30, COLOR_GRAY);
  tft.drawRect(240, 150, 30, 30, COLOR_BLACK);
  tft.setCursor(248, 170);
  tft.print("+");

  // Hour
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(50, 200);
  tft.print("Hour:");
  tft.fillRect(150, 190, 30, 30, COLOR_GRAY);
  tft.drawRect(150, 190, 30, 30, COLOR_BLACK);
  tft.setTextColor(COLOR_BLACK);
  tft.setCursor(158, 210);
  tft.print("-");

  tft.fillRect(180, 190, 60, 30, COLOR_YELLOW);
  tft.drawRect(180, 190, 60, 30, COLOR_BLACK);
  tft.setCursor(195, 210);
  tft.print(tmSet.Hour);

  tft.fillRect(240, 190, 30, 30, COLOR_GRAY);
  tft.drawRect(240, 190, 30, 30, COLOR_BLACK);
  tft.setCursor(248, 210);
  tft.print("+");

  // Minute
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(50, 240);
  tft.print("Minute:");
  tft.fillRect(150, 230, 30, 30, COLOR_GRAY);
  tft.drawRect(150, 230, 30, 30, COLOR_BLACK);
  tft.setTextColor(COLOR_BLACK);
  tft.setCursor(158, 250);
  tft.print("-");

  tft.fillRect(180, 230, 60, 30, COLOR_YELLOW);
  tft.drawRect(180, 230, 60, 30, COLOR_BLACK);
  tft.setCursor(195, 250);
  tft.print(tmSet.Minute);

  tft.fillRect(240, 230, 30, 30, COLOR_GRAY);
  tft.drawRect(240, 230, 30, 30, COLOR_BLACK);
  tft.setCursor(248, 250);
  tft.print("+");

  // Second
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(50, 280);
  tft.print("Second:");
  tft.fillRect(150, 270, 30, 30, COLOR_GRAY);
  tft.drawRect(150, 270, 30, 30, COLOR_BLACK);
  tft.setTextColor(COLOR_BLACK);
  tft.setCursor(158, 290);
  tft.print("-");

  tft.fillRect(180, 270, 60, 30, COLOR_YELLOW);
  tft.drawRect(180, 270, 60, 30, COLOR_BLACK);
  tft.setCursor(195, 290);
  tft.print(tmSet.Second);

  tft.fillRect(240, 270, 30, 30, COLOR_GRAY);
  tft.drawRect(240, 270, 30, 30, COLOR_BLACK);
  tft.setCursor(248, 290);
  tft.print("+");
}

void drawDeviceRow(int row) {
  int yPos = 85 + row * 25;
  bool isOn = (switchOutputs[row].state == HIGH);

  tft.fillRect(0, yPos - 17, SCREEN_WIDTH, 25, COLOR_BLACK);

  if (isOn) {
    tft.fillRect(0, yPos - 17, SCREEN_WIDTH, 25, COLOR_PURPLE);
  }

  uint16_t textColor = isOn ? COLOR_WHITE : COLOR_WHITE;

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(textColor);

  tft.setCursor(10, yPos);
  tft.print(switchOutputs[row].name);

  int inaIdx = getInaIndexForSwitch(row);
  if (inaIdx >= 0) {
    tft.setTextColor(isOn ? COLOR_WHITE : COLOR_WHITE);

    tft.setCursor(100, yPos);
    tft.printf("%.2fV", deviceVoltage[inaIdx]);

    tft.setCursor(180, yPos);
    tft.printf("%.4fA", deviceCurrent[inaIdx] / 1000.0);

    tft.setCursor(260, yPos);
    tft.printf("%.3fW", devicePower[inaIdx]);
  }
}

void drawTotalRow() {
  int totalRowY = 85 + numSwitches * 25 + 15;

  float totalVoltage = 0;
  float totalCurrent = 0;
  float totalPower = 0;
  int activeDevices = 0;

  for (int i = 0; i < numSwitches; i++) {
    int inaIdx = getInaIndexForSwitch(i);
    if (inaIdx >= 0 && switchOutputs[i].state == HIGH) {
      totalVoltage += deviceVoltage[inaIdx];
      totalCurrent += deviceCurrent[inaIdx];
      totalPower += devicePower[inaIdx];
      activeDevices++;
    }
  }

  if (activeDevices > 0) {
    totalVoltage /= activeDevices;
  }

  tft.fillRect(0, totalRowY - 17, SCREEN_WIDTH, 25, COLOR_BLACK);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_YELLOW);

  tft.setCursor(10, totalRowY);
  tft.print("Total");

  tft.setCursor(100, totalRowY);
  tft.printf("%.2fV", totalVoltage);

  tft.setCursor(180, totalRowY);
  tft.printf("%.4fA", totalCurrent / 1000.0);

  tft.setCursor(260, totalRowY);
  tft.printf("%.3fW", totalPower);
}

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
      tft.printf("%.1fV %.0fmA", deviceVoltage[inaIdx], deviceCurrent[inaIdx]);
    }
  }
}

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

  int16_t x1,y1; uint16_t w,h;
  tft.getTextBounds(buff, 0, 0, &x1, &y1, &w, &h);
  int tx = (SCREEN_WIDTH - w) / 2;
  int clearX = max(0, tx - 10);
  int clearW = min(SCREEN_WIDTH, w + 20);
  tft.fillRect(clearX, 10, clearW, 25, COLOR_BLACK);
  tft.setCursor(tx, currentMode == MODE_SCRIPT && isScriptRunning ? 25 : 30);
  tft.print(buff);
}

void updateLockButton() {
  drawButton(btnLock,
             lock ? COLOR_PURPLE : COLOR_YELLOW,
             lock ? COLOR_WHITE : COLOR_BLACK,
             "LOCK",
             btnLock.pressed,
             btnLock.enabled);
}

// ==================== Helpers ====================
bool touchInButton(int16_t x, int16_t y, const ButtonRegion& btn) {
  return btn.enabled && (x >= btn.x && x <= btn.x+btn.w && y >= btn.y && y <= btn.y+btn.h);
}

int getInaIndexForSwitch(int switchIdx) {
  const char* name = switchOutputs[switchIdx].name;
  for (int i=0; i<numIna; i++) {
    if (strcasecmp(name, inaNames[i]) == 0) return i;
  }
  return -1;
}

void setAllOutputsOff() {
  for (int i=0; i<numSwitches; i++) {
    digitalWrite(switchOutputs[i].outputPin, LOW);
    switchOutputs[i].state = LOW;
    if (currentMode == MODE_MAIN) drawDeviceRow(i);
    if (currentMode == MODE_SCRIPT) updateLiveValueRow(i);
  }
}

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

void applyFanSettings() {
  analogWrite(FAN_PWM_PIN, (fanOn ? fanSpeed : 0));
}

void applyUpdateRate() {
  // Just uses updateRate in loop
}

void applyDarkMode() {
  tft.invertDisplay(darkMode);
  Serial.print("Dark mode: ");
  Serial.println(darkMode ? "ON" : "OFF");
}

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

  // FIXED: Use script-based filename when recording from script
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
    // FIXED: Enhanced JSON header with detailed script information
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

      // FIXED: Include detailed device configuration
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

      // FIXED: Add script ending information
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

// ==================== EEPROM & Settings ====================
void saveSettingsToEEPROM() {
  EEPROM.put(EEPROM_FAN_ON_ADDR, fanOn);
  EEPROM.put(EEPROM_FAN_SPEED_ADDR, fanSpeed);
  EEPROM.put(EEPROM_UPDATE_RATE_ADDR, updateRate);
  EEPROM.put(EEPROM_TIME_FORMAT_ADDR, use24HourFormat);
  EEPROM.put(EEPROM_DARK_MODE_ADDR, darkMode);
}

void loadSettingsFromEEPROM() {
  EEPROM.get(EEPROM_FAN_ON_ADDR, fanOn);
  EEPROM.get(EEPROM_FAN_SPEED_ADDR, fanSpeed);
  EEPROM.get(EEPROM_UPDATE_RATE_ADDR, updateRate);
  EEPROM.get(EEPROM_TIME_FORMAT_ADDR, use24HourFormat);
  EEPROM.get(EEPROM_DARK_MODE_ADDR, darkMode);

  if (fanSpeed < 50 || fanSpeed > 255) fanSpeed = 255;
  if (updateRate < 10 || updateRate > 5000) updateRate = 100;
}

// ==================== Serial Command Processing ====================
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
      Serial.print(inaIdx>=0?deviceCurrent[inaIdx]:0,4);
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
        Serial.print(deviceCurrent[inaIdx], 1);
        Serial.print("mA | P=");
        Serial.print(devicePower[inaIdx], 3);
        Serial.print("W");
      }
      Serial.println();
    }
    Serial.println("===================");
  }
}

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

void serialPrint(String message) {
  if (serialAvailable && message.trim().length() > 0) {
    String temp = message;
    temp.trim();
    Serial.println(temp);
  }
}

// ==================== Time Helpers ====================
String getCurrentTimeString() {
  time_t t = now();
  return formatTimeHHMMSS(t);
}

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

String formatDateString(time_t t) {
  char buf[12];
  sprintf(buf, "20%02d-%02d-%02d", year(t) % 100, month(t), day(t));
  return String(buf);
}

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
