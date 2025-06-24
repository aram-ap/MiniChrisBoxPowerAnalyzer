#include <Arduino.h>
#include <Wire.h>
#include <EEPROM.h>
#include <Bounce2.h>
#include <INA226.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7796S.h>
#include <XPT2046_Touchscreen.h>
#include <SD.h>
#include <Keypad.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>

// ==================== Pin & Color Definitions ====================
const int pwrLed  = 22;
const int lockLed = 21;
const int stopLed = 20;

#define COLOR_BLACK    0x0000
#define COLOR_WHITE    0xFFFF
#define COLOR_YELLOW   0xFFE0
#define COLOR_PURPLE   0x780F
#define COLOR_BTN_BG   0x2104
#define COLOR_BTN_DARK 0x18A3
#define COLOR_BTN_PRESS 0x3186
#define COLOR_RECORD   0x001F
#define COLOR_RECORDING 0xF800
#define COLOR_GRAY     0x8410

#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST   7
#define TOUCH_CS   8
#define TOUCH_IRQ 14

Adafruit_ST7796S tft = Adafruit_ST7796S(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

const int SCREEN_WIDTH  = 480;
const int SCREEN_HEIGHT = 320;

// SD Card
#define SD_CS 36
File logFile;
bool sdAvailable = false;
unsigned long lastSDCheck = 0;

// ==================== Button Geometry ====================
struct ButtonRegion {
  int x, y, w, h;
  const char* label;
  bool pressed;
  uint16_t color;
  bool enabled;
};

// All UI buttons (global for simplicity)
ButtonRegion btnRecord = { 5, 5, 120, 35, "RECORD", false, COLOR_RECORD, false };
ButtonRegion btnStop   = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, COLOR_YELLOW, true };
ButtonRegion btnLock   = { SCREEN_WIDTH - 70, SCREEN_HEIGHT - 40, 65, 35, "LOCK", false, COLOR_YELLOW, true };
ButtonRegion btnAllOn  = { 5, SCREEN_HEIGHT - 40, 80, 35, "ALL ON", false, COLOR_YELLOW, true };
ButtonRegion btnAllOff = { 90, SCREEN_HEIGHT - 40, 80, 35, "ALL OFF", false, COLOR_YELLOW, true };
ButtonRegion btnScript = { 175, SCREEN_HEIGHT - 40, 60, 35, "Script", false, COLOR_YELLOW, true };
ButtonRegion btnEdit   = { 240, SCREEN_HEIGHT - 40, 60, 35, "Edit", false, COLOR_YELLOW, true };
ButtonRegion btnSettings = { 310, SCREEN_HEIGHT - 40, 60, 35, "Sett", false, COLOR_YELLOW, true };

// Script/Edit page buttons (add more if needed)
ButtonRegion btnScriptBack   = { 5, 5, 80, 35, "Back", false, COLOR_YELLOW, true };
ButtonRegion btnScriptStop   = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, COLOR_YELLOW, true };
ButtonRegion btnScriptClear  = { 15, SCREEN_HEIGHT - 40, 70, 35, "Clear", false, COLOR_YELLOW, true };
ButtonRegion btnScriptLoad   = { 90, SCREEN_HEIGHT - 40, 70, 35, "Load", false, COLOR_YELLOW, true };
ButtonRegion btnScriptStart  = { 165, SCREEN_HEIGHT - 40, 80, 35, "Start", false, COLOR_YELLOW, true };
ButtonRegion btnScriptRestart= { 250, SCREEN_HEIGHT - 40, 80, 35, "Restart", false, COLOR_YELLOW, true };
ButtonRegion btnScriptSet    = { 340, SCREEN_HEIGHT - 40, 80, 35, "Set", false, COLOR_YELLOW, true };

ButtonRegion btnEditBack     = { 5, 5, 80, 35, "Back", false, COLOR_YELLOW, true };
ButtonRegion btnEditStop     = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, COLOR_YELLOW, true };
ButtonRegion btnEditLoad     = { 40, SCREEN_HEIGHT - 40, 70, 35, "Load", false, COLOR_YELLOW, true };
ButtonRegion btnEditSave     = { 120, SCREEN_HEIGHT - 40, 70, 35, "Save", false, COLOR_YELLOW, true };
ButtonRegion btnEditDefault  = { 200, SCREEN_HEIGHT - 40, 110, 35, "Default", false, COLOR_YELLOW, true };

// Settings panel buttons
ButtonRegion btnSettingsBack = { 5, 5, 80, 35, "Back", false, COLOR_YELLOW, true };
ButtonRegion btnSettingsStop = { SCREEN_WIDTH - 110, 5, 105, 35, "STOP", false, COLOR_YELLOW, true };
ButtonRegion btnFanToggle = { 320, 80, 90, 35, "ON", false, COLOR_YELLOW, true };
ButtonRegion btnFanSpeedInput = { SCREEN_WIDTH-160, 155, 60, 35, "", false, COLOR_YELLOW, true };
ButtonRegion btnUpdateRateInput = { SCREEN_WIDTH-160, 205, 60, 35, "", false, COLOR_YELLOW, true };

// ==================== INA226 Setup ====================
INA226 ina_gse1(0x40);
INA226 ina_gse2(0x42);
INA226 ina_ter(0x41);
INA226 ina_te1(0x44);
INA226 ina_te2(0x45);
INA226 ina_te3(0x4C);
INA226* inaDevices[] = { &ina_gse1, &ina_gse2, &ina_ter, &ina_te1, &ina_te2, &ina_te3 };
const char* inaNames[] = { "GSE-1", "gse2", "TE-R", "TE-1", "TE-2", "TE-3" };
const int numIna = 6;
float deviceVoltage[numIna];
float deviceCurrent[numIna];
float devicePower[numIna]; // in watts

// ==================== Relay/Output Setup ====================
struct SwitchOutput {
  const char* name;
  int outputPin;
  int switchPin;
  Bounce debouncer;
  bool state; // HIGH=ON, LOW=OFF
};
SwitchOutput switchOutputs[] = {
  { "GSE-1", 0, 41, Bounce(), LOW },
  { "TE-R",  1, 40, Bounce(), LOW },
  { "TE-1",  2, 39, Bounce(), LOW },
  { "TE-2",  3, 38, Bounce(), LOW },
  { "TE-3",  4, 24, Bounce(), LOW },
  { "GSE-2", 5, 15, Bounce(), LOW }
};
const int numSwitches = sizeof(switchOutputs)/sizeof(SwitchOutput);

// ========== Script Data Structures ==========
// Each script controls all outputs and has its own timings/settings
#define SCRIPT_SLOT_COUNT 4
#define DEV_PER_SCRIPT 6

struct ScriptDev {
  long tOn;   // seconds, may be negative
  long tOff;  // seconds
};
struct ScriptData {
  ScriptDev dev[DEV_PER_SCRIPT];
  long tStart;
  long tStop;
  bool record;
};

ScriptData scriptSlots[SCRIPT_SLOT_COUNT];   // 4 slots in EEPROM
ScriptData scriptWork;  // Working buffer (editing/running)
int scriptSlotSelected = 0;
bool scriptLoaded = false;
bool scriptDirty = false;

// Script run/exec state
bool scriptRunning = false;
bool scriptPaused = false;
unsigned long scriptRunStart = 0;     // ms
unsigned long scriptPausedAt = 0;     // ms
long scriptRunTimer = 0;              // ms (from tStart)
bool safetyStop = false;

// ========== Global State and UI ==========
bool lock = false;
bool lockBeforeStop = false;
unsigned long lastLive = 0;
unsigned long lastRecordMillis = 0;
unsigned long updateRate = 100;
unsigned long lastTouchTime = 0;
const unsigned long touchDebounceMs = 200;
bool recording = false;
char recordFilename[32] = "power_data.json";
#define RECORD_INTERVAL_MS 100
bool firstDataPoint = true;

// ==================== Recording State (Globals) ====================
unsigned long recordStartMillis = 0;

// ======= FAN ==========
#define FAN_PWM_PIN 33
bool fanOn = false;
int fanSpeed = 255;

// ======= Keypad ==========
const char keys[4][4] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[4] = {28, 27, 26, 25};
byte colPins[4] = {32, 31, 30, 29};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, 4, 4);
char keypadBuffer[8] = "";
int keypadPos = 0;

enum GUIMode {
  MODE_MAIN, MODE_SETTINGS, MODE_KEYPAD,
  MODE_SCRIPT, MODE_SCRIPT_SLOTSEL, MODE_SCRIPT_SETTINGS,
  MODE_EDIT, MODE_EDIT_SLOTSEL, MODE_EDIT_SAVE_CONFIRM, MODE_EDIT_DEFAULTS,
  MODE_EDIT_KEYPAD, MODE_SCRIPT_SET_KEYPAD
};
GUIMode currentMode = MODE_MAIN;

// Keypad mode context
enum KeypadMode {
  KEYPAD_NONE,
  KEYPAD_UPDATE_RATE, KEYPAD_FAN_SPEED,
  KEYPAD_SCRIPT_SLOT, KEYPAD_EDIT_SLOT,
  KEYPAD_EDIT_ON, KEYPAD_EDIT_OFF, KEYPAD_EDIT_TSTART, KEYPAD_EDIT_TSTOP,
  KEYPAD_SCRIPT_TSTART, KEYPAD_SCRIPT_TSTOP
};
KeypadMode keypadMode = KEYPAD_NONE;
int keypadEditRow = 0; // row index of device for edit (if applicable)

// Forward declarations for all functions, see next chunk

// ==================== FUNCTION PROTOTYPES ====================
// Drawing/UI
void drawButton(ButtonRegion &btn, uint16_t bgColor, uint16_t textColor, const char* label, bool pressed = false, bool enabled = true);
void drawDeviceRow(int row);
void updateLiveValueRow(int row);
void drawMainScreen();
void drawSettingsPanel();
void drawKeypadPanel();
void drawScriptPage();
void drawScriptEditPage();
void drawScriptSettingsPage();
void drawScriptSlotSel(bool forLoad);
void drawEditSlotSel(bool forLoad);
void drawEditSaveConfirm();
void drawScriptSetKeypad();
void drawScriptDefaults();

void handleTouch();
void handleTouchMain(int16_t x, int16_t y);
void handleTouchSettings(int16_t x, int16_t y);
void handleTouchScript(int16_t x, int16_t y);
void handleTouchScriptSlotSel(int16_t x, int16_t y);
void handleTouchEdit(int16_t x, int16_t y);
void handleTouchEditSlotSel(int16_t x, int16_t y);

void handleKeypadEntry();

// SD & Sensor
void checkSDCard();
void recordData();
void startRecording();
void stopRecording();
void nextAvailableFilename(char* buf, size_t buflen);
int getInaIndexForSwitch(int switchIdx);
void applyFanSettings();
void applyUpdateRate();
void loadSettingsFromEEPROM();
void saveSettingsToEEPROM();

void setAllOutputsOff();
void syncOutputsToSwitches();
void updateLockButton();

bool touchInButton(int16_t x, int16_t y, const ButtonRegion &btn);

// ==================== EEPROM MANAGEMENT ====================
// EEPROM layout: 0-1023 used for 4 script slots, settings use existing addresses
#define EEPROM_FAN_ON_ADDR      0
#define EEPROM_FAN_SPEED_ADDR   4
#define EEPROM_UPDATE_RATE_ADDR 8

void saveSettingsToEEPROM() {
  EEPROM.put(EEPROM_FAN_ON_ADDR, fanOn);
  EEPROM.put(EEPROM_FAN_SPEED_ADDR, fanSpeed);
  EEPROM.put(EEPROM_UPDATE_RATE_ADDR, updateRate);
}

void loadSettingsFromEEPROM() {
  EEPROM.get(EEPROM_FAN_ON_ADDR, fanOn);
  EEPROM.get(EEPROM_FAN_SPEED_ADDR, fanSpeed);
  EEPROM.get(EEPROM_UPDATE_RATE_ADDR, updateRate);
  if (fanSpeed < 50 || fanSpeed > 255) fanSpeed = 255;
  if (updateRate < 10 || updateRate > 5000) updateRate = 100;
}
#define SCRIPT_EEPROM_SLOT_SIZE  (sizeof(ScriptData))
#define SCRIPT_EEPROM_ADDR(slot) (100 + (slot) * SCRIPT_EEPROM_SLOT_SIZE)

void saveScriptSlotToEEPROM(int slot, ScriptData &data) {
  EEPROM.put(SCRIPT_EEPROM_ADDR(slot), data);
}

void loadScriptSlotFromEEPROM(int slot, ScriptData &data) {
  EEPROM.get(SCRIPT_EEPROM_ADDR(slot), data);
}

void loadAllScriptSlots() {
  for (int i = 0; i < SCRIPT_SLOT_COUNT; i++) {
    loadScriptSlotFromEEPROM(i, scriptSlots[i]);
  }
}

// Set defaults for a script (all zeros/off, TStart=0, TStop=10, record=false)
void setScriptDefault(ScriptData &data) {
  for (int i = 0; i < DEV_PER_SCRIPT; i++) {
    data.dev[i].tOn = 0;
    data.dev[i].tOff = 0;
  }
  data.tStart = 0;
  data.tStop = 10;
  data.record = false;
}

// Mark scriptDirty if changed, mark scriptLoaded if loaded from slot
void markScriptDirty(bool dirty) {
  scriptDirty = dirty;
}
void markScriptLoaded(bool loaded) {
  scriptLoaded = loaded;
  scriptDirty = false;
}

// ==================== MISC UTILS ====================
// Return label for a slot
const char* slotName(int slot) {
  static const char* names[4] = { "Script 1", "Script 2", "Script 3", "Script 4" };
  if (slot >= 0 && slot < 4) return names[slot];
  return "No Script";
}

// Return output device name
const char* deviceName(int i) {
  if (i < 0 || i >= numSwitches) return "???";
  return switchOutputs[i].name;
}

// Format T value (e.g., T+10, T-4)
String fmtT(long val) {
  if (val >= 0) return "T+" + String(val);
  else return "T" + String(val);
}

// For script timer UI
String fmtTimer(long ms, long tStart) {
  long tSec = ms / 1000 + tStart;
  if (tSec >= 0) return "T+" + String(tSec);
  else return "T" + String(tSec);
}

// Return true if device output is ON given the current script time (seconds)
bool deviceIsOnAt(const ScriptDev &dev, long t) {
  return (t >= dev.tOn && t < dev.tOff);
}

// For "Back" or "Stop" safety
void doSafetyStop() {
  safetyStop = true;
  setAllOutputsOff();
  scriptRunning = false;
  scriptPaused = false;
  recording = false;
}

// =============== SCRIPT RUN/PAUSE/STOP ======================
void startScriptRun();
void stopScriptRun();
void restartScriptRun();
void updateScriptExecution();

void startScriptRun() {
  scriptRunning = true;
  scriptPaused = false;
  scriptRunStart = millis();
  if (scriptWork.record) startRecording();
}

void stopScriptRun() {
  scriptRunning = false;
  scriptPaused = false;
  doSafetyStop();
  stopRecording();
}

void restartScriptRun() {
  scriptRunning = true;
  scriptPaused = false;
  scriptRunStart = millis();
  if (scriptWork.record && !recording) startRecording();
}

// Handle time and output logic for script running
void updateScriptExecution() {
  if (!scriptRunning) return;
  unsigned long tNow = millis();
  long tScript = scriptPaused ? (scriptPausedAt - scriptRunStart) : (tNow - scriptRunStart);
  long tSec = tScript / 1000 + scriptWork.tStart;
  if (tSec < scriptWork.tStart) tSec = scriptWork.tStart;
  if (tSec > scriptWork.tStop) { stopScriptRun(); return; }

  // Control outputs
  for (int i = 0; i < DEV_PER_SCRIPT; i++) {
    bool shouldOn = deviceIsOnAt(scriptWork.dev[i], tSec);
    if (shouldOn && switchOutputs[i].state == LOW) {
      digitalWrite(switchOutputs[i].outputPin, HIGH); // ON
      switchOutputs[i].state = HIGH;
    }
    if (!shouldOn && switchOutputs[i].state == HIGH) {
      digitalWrite(switchOutputs[i].outputPin, LOW); // OFF
      switchOutputs[i].state = LOW;
    }
  }
}

// ==================== DRAWING & UI ====================
void drawButton(ButtonRegion &btn, uint16_t bgColor, uint16_t textColor, const char* label, bool pressed, bool enabled) {
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

void drawDeviceRow(int row) {
  int yPos = 60 + row * 30;
  bool isOn = (switchOutputs[row].state == HIGH);
  uint16_t bg = isOn ? COLOR_PURPLE : COLOR_BLACK;
  uint16_t tx = isOn ? COLOR_WHITE : COLOR_YELLOW;
  tft.fillRect(0, yPos - 20, SCREEN_WIDTH, 30, bg);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(tx);
  tft.setCursor(5, yPos);
  tft.print(switchOutputs[row].name);

  int inaIdx = getInaIndexForSwitch(row);
  if (inaIdx >= 0) {
    tft.setFont(&FreeMonoBold9pt7b);
    tft.setTextColor(tx);
    tft.fillRect(160, yPos - 18, 310, 24, bg);
    char buf[48];
    snprintf(buf, sizeof(buf), "V=%.2f I=%.1fmA P=%.3fW", deviceVoltage[inaIdx], deviceCurrent[inaIdx], devicePower[inaIdx]);
    tft.setCursor(160, yPos);
    tft.print(buf);
  }
}

void updateLiveValueRow(int row) {
  int yPos = 60 + row * 30;
  int inaIdx = getInaIndexForSwitch(row);
  if (inaIdx >= 0) {
    uint16_t bg = (switchOutputs[row].state == HIGH) ? COLOR_PURPLE : COLOR_BLACK;
    tft.fillRect(160, yPos - 18, 310, 24, bg);
    tft.setFont(&FreeMonoBold9pt7b);
    tft.setTextColor((switchOutputs[row].state == HIGH) ? COLOR_WHITE : COLOR_YELLOW);
    char buf[48];
    snprintf(buf, sizeof(buf), "V=%.2f I=%.1fmA P=%.3fW", deviceVoltage[inaIdx], deviceCurrent[inaIdx], devicePower[inaIdx]);
    tft.setCursor(160, yPos);
    tft.print(buf);
  }
}

void drawMainScreen() {
  tft.fillScreen(COLOR_BLACK);
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(140, 25);
  tft.print("Outputs");

  drawButton(btnRecord, btnRecord.color, COLOR_WHITE, btnRecord.label, false, btnRecord.enabled);
  drawButton(btnStop, COLOR_YELLOW, COLOR_BLACK, "STOP");
  drawButton(btnAllOn,  COLOR_YELLOW, COLOR_BLACK, "ALL ON");
  drawButton(btnAllOff, COLOR_YELLOW, COLOR_BLACK, "ALL OFF");
  drawButton(btnScript, COLOR_YELLOW, COLOR_BLACK, "Script");
  drawButton(btnEdit,   COLOR_YELLOW, COLOR_BLACK, "Edit");
  drawButton(btnLock, lock ? COLOR_PURPLE : COLOR_YELLOW, lock ? COLOR_WHITE : COLOR_BLACK, "LOCK", btnLock.pressed, btnLock.enabled);
  drawButton(btnSettings, COLOR_YELLOW, COLOR_BLACK, "Sett", false, btnSettings.enabled);

  tft.setFont(&FreeMonoBold9pt7b);
  for (int i = 0; i < numSwitches; i++) drawDeviceRow(i);
}

void drawSettingsPanel() {
  tft.fillScreen(COLOR_BLACK);

  drawButton(btnSettingsBack, COLOR_YELLOW, COLOR_BLACK, "Back", false, true);
  drawButton(btnSettingsStop, COLOR_YELLOW, COLOR_BLACK, "STOP", false, true);
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds("Settings", 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_WIDTH-w)/2, 32);
  tft.print("Settings");

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(60, 105);
  tft.print("Fan:");
  if (fanOn) {
    drawButton(btnFanToggle, COLOR_PURPLE, COLOR_WHITE, "ON", false, true);
  } else {
    drawButton(btnFanToggle, COLOR_GRAY, COLOR_BLACK, "OFF", false, true);
  }

  tft.setCursor(60, 155);
  tft.print("Fan Speed:");
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", fanSpeed);
  drawButton(btnFanSpeedInput, COLOR_YELLOW, COLOR_BLACK, buf, false, true);

  tft.setCursor(60, 205);
  tft.print("Update Rate:");
  char buf2[12];
  snprintf(buf2, sizeof(buf2), "%lu", updateRate);
  drawButton(btnUpdateRateInput, COLOR_YELLOW, COLOR_BLACK, buf2, false, true);
  tft.setCursor(485, 205);
  tft.print("ms");
}

void drawKeypadPanel() {
  tft.fillScreen(COLOR_BLACK);
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(120, 30);
  if (keypadMode == KEYPAD_UPDATE_RATE) {
    tft.print("Enter Update Rate (ms):");
  } else if (keypadMode == KEYPAD_FAN_SPEED) {
    tft.print("Enter Fan Speed (0-255):");
  } else if (keypadMode == KEYPAD_EDIT_ON) {
    tft.print("T-On for device (sec, neg ok):");
  } else if (keypadMode == KEYPAD_EDIT_OFF) {
    tft.print("T-Off for device (sec):");
  } else if (keypadMode == KEYPAD_EDIT_TSTART) {
    tft.print("Script T-Start (sec, neg ok):");
  } else if (keypadMode == KEYPAD_EDIT_TSTOP) {
    tft.print("Script T-Stop (sec, pos):");
  } else if (keypadMode == KEYPAD_SCRIPT_SLOT || keypadMode == KEYPAD_EDIT_SLOT) {
    tft.print("Enter slot number (1-4):");
  }
  tft.setFont(&FreeMonoBold9pt7b);
  tft.setCursor(160, 90);
  tft.print(keypadBuffer);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(100, 160);
  tft.print("Press # to confirm, * to clear");
}

bool touchInButton(int16_t x, int16_t y, const ButtonRegion &btn) {
  return btn.enabled && (x >= btn.x && x <= btn.x + btn.w && y >= btn.y && y <= btn.y + btn.h);
}

// ==================== SD LOGGING ====================
void checkSDCard() {
  bool nowAvailable = SD.begin(SD_CS);
  if (nowAvailable != sdAvailable) {
    sdAvailable = nowAvailable;
    btnRecord.enabled = sdAvailable;
    drawButton(btnRecord, sdAvailable ? COLOR_RECORD : COLOR_GRAY, COLOR_WHITE, btnRecord.label, false, btnRecord.enabled);
  }
}

void nextAvailableFilename(char* buf, size_t buflen) {
  int idx = 0;
  while (true) {
    if (idx == 0)
      snprintf(buf, buflen, "power_data.json");
    else
      snprintf(buf, buflen, "power_data%d.json", idx);
    if (!SD.exists(buf)) break;
    idx++;
    if (idx > 99) break;
  }
}

void startRecording() {
  checkSDCard();
  if (!sdAvailable) {
    drawButton(btnRecord, COLOR_GRAY, COLOR_WHITE, "NO SD", false, false);
    delay(800);
    drawButton(btnRecord, COLOR_GRAY, COLOR_WHITE, "RECORD", false, false);
    return;
  }
  nextAvailableFilename(recordFilename, sizeof(recordFilename));
  logFile = SD.open(recordFilename, FILE_WRITE);
  if (!logFile) {
    drawButton(btnRecord, COLOR_GRAY, COLOR_WHITE, "SD ERR", false, false);
    delay(800);
    drawButton(btnRecord, COLOR_GRAY, COLOR_WHITE, "RECORD", false, false);
    return;
  }
  // Write JSON header
  logFile.print("{\n");
  logFile.print("\"details\": {\"test_num\":\"1\"},\n");
  logFile.print("\"parameters\": {");
  for (int i = 0; i < numSwitches; i++) {
    logFile.print("\"");
    logFile.print(switchOutputs[i].name);
    logFile.print("_on\":\"0\",\"");
    logFile.print(switchOutputs[i].name);
    logFile.print("_off\":\"0\"");
    if (i != numSwitches - 1) logFile.print(",");
  }
  logFile.print("},\n");
  logFile.print("\"data\": [\n");
  logFile.flush();

  recording = true;
  recordStartMillis = millis();
  lastRecordMillis = 0;
  firstDataPoint = true;
  btnRecord.label = "RECORDING";
  drawButton(btnRecord, COLOR_RECORDING, COLOR_WHITE, "RECORDING", false, true);
}

void stopRecording() {
  if (!recording) return;
  recording = false;
  logFile.print("\n],\n\"end\":1\n}");
  logFile.close();
  btnRecord.label = "RECORD";
  drawButton(btnRecord, COLOR_RECORD, COLOR_WHITE, "RECORD", false, true);
}

void recordData() {
  if (!recording || !logFile) return;
  unsigned long t = (millis() - recordStartMillis);
  if (!firstDataPoint) logFile.print(",\n");
  firstDataPoint = false;
  logFile.print("{");
  logFile.print("\"time\":"); logFile.print(t);
  for (int i = 0; i < numSwitches; i++) {
    int inaIdx = getInaIndexForSwitch(i);
    logFile.print(",\"");
    logFile.print(switchOutputs[i].name);
    logFile.print("_volt\":");
    logFile.print(inaIdx >= 0 ? deviceVoltage[inaIdx] : 0, 4);
    logFile.print(",\"");
    logFile.print(switchOutputs[i].name);
    logFile.print("_curr\":");
    logFile.print(inaIdx >= 0 ? deviceCurrent[inaIdx] : 0, 4);
    logFile.print(",\"");
    logFile.print(switchOutputs[i].name);
    logFile.print("_pow\":");
    logFile.print(inaIdx >= 0 ? devicePower[inaIdx] : 0, 4);
    logFile.print(",\"");
    logFile.print(switchOutputs[i].name);
    logFile.print("_stat\":");
    logFile.print(switchOutputs[i].state ? "1" : "0");
  }
  logFile.print("}");
  logFile.flush();
}

// ==================== PAGE HANDLERS/STUBS (fill in as needed) ====================

void handleTouch() {
  TS_Point p = ts.getPoint();
  int16_t x = map(p.x, 200, 3800, 0, SCREEN_WIDTH);
  x = SCREEN_WIDTH - x;
  int16_t y = map(p.y, 200, 3800, SCREEN_HEIGHT, 0);

  switch (currentMode) {
    case MODE_MAIN:         handleTouchMain(x, y); break;
    case MODE_SETTINGS:     handleTouchSettings(x, y); break;
    case MODE_SCRIPT:       /* handleTouchScript(x, y); */ break; // Add handler logic as needed
    case MODE_EDIT:         /* handleTouchEdit(x, y);   */ break; // Add handler logic as needed
    default: break;
  }
}

void handleTouchMain(int16_t x, int16_t y) {
  if (touchInButton(x, y, btnRecord)) {
    if (!recording) startRecording();
    else stopRecording();
  } else if (touchInButton(x, y, btnStop)) {
    if (!safetyStop) {
      lockBeforeStop = lock;
      safetyStop = true;
      setAllOutputsOff();
      drawButton(btnStop, COLOR_PURPLE, COLOR_WHITE, "RELEASE", false, btnStop.enabled);
    } else {
      safetyStop = false;
      bool prevLock = lock;
      lock = lockBeforeStop;
      drawButton(btnStop, COLOR_YELLOW, COLOR_BLACK, "STOP", false, btnStop.enabled);
      if (!lock && prevLock) syncOutputsToSwitches();
    }
  } else if (touchInButton(x, y, btnLock)) {
    bool prevLock = lock;
    lock = !lock;
    drawButton(btnLock, lock ? COLOR_PURPLE : COLOR_YELLOW, lock ? COLOR_WHITE : COLOR_BLACK, "LOCK", btnLock.pressed, btnLock.enabled);
    if (!lock && prevLock) syncOutputsToSwitches();
  } else if (touchInButton(x, y, btnAllOn)) {
    if (!safetyStop) {
      lock = true;
      drawButton(btnLock, COLOR_PURPLE, COLOR_WHITE, "LOCK", false, true);
      for (int i = 0; i < numSwitches; i++) {
        digitalWrite(switchOutputs[i].outputPin, HIGH);
        switchOutputs[i].state = HIGH;
        drawDeviceRow(i);
      }
    }
  } else if (touchInButton(x, y, btnAllOff)) {
    if (!safetyStop) {
      lock = true;
      drawButton(btnLock, COLOR_PURPLE, COLOR_WHITE, "LOCK", false, true);
      setAllOutputsOff();
      for (int i = 0; i < numSwitches; i++) drawDeviceRow(i);
    }
  } else if (touchInButton(x, y, btnSettings)) {
    currentMode = MODE_SETTINGS;
    drawSettingsPanel();
    return;
  }
  // Add Script and Edit navigation as needed
  // e.g. if (touchInButton(x, y, btnScript)) { currentMode = MODE_SCRIPT; drawScriptPage(); return; }
  //      if (touchInButton(x, y, btnEdit))   { currentMode = MODE_EDIT;   drawScriptEditPage(); return; }
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
      drawButton(btnSettingsStop, COLOR_PURPLE, COLOR_WHITE, "RELEASE", false, btnSettingsStop.enabled);
    } else {
      safetyStop = false;
      bool prevLock = lock;
      lock = lockBeforeStop;
      drawButton(btnSettingsStop, COLOR_YELLOW, COLOR_BLACK, "STOP", false, btnSettingsStop.enabled);
      if (!lock && prevLock) syncOutputsToSwitches();
    }
    return;
  }
  if (touchInButton(x, y, btnFanToggle)) {
    fanOn = !fanOn;
    saveSettingsToEEPROM();
    applyFanSettings();
    drawSettingsPanel();
    return;
  }
  if (touchInButton(x, y, btnFanSpeedInput)) {
    currentMode = MODE_KEYPAD;
    keypadMode = KEYPAD_FAN_SPEED;
    keypadPos = 0;
    keypadBuffer[0] = 0;
    drawKeypadPanel();
    return;
  }
  if (touchInButton(x, y, btnUpdateRateInput)) {
    currentMode = MODE_KEYPAD;
    keypadMode = KEYPAD_UPDATE_RATE;
    keypadPos = 0;
    keypadBuffer[0] = 0;
    drawKeypadPanel();
    return;
  }
}

int getInaIndexForSwitch(int switchIdx) {
  const char* name = switchOutputs[switchIdx].name;
  for (int i = 0; i < numIna; i++) {
    if (strcasecmp(name, inaNames[i]) == 0) return i;
  }
  return -1;
}

void setAllOutputsOff() {
  for (int i = 0; i < numSwitches; i++) {
    digitalWrite(switchOutputs[i].outputPin, LOW);
    switchOutputs[i].state = LOW;
    if (currentMode == MODE_MAIN) drawDeviceRow(i);
  }
}

void syncOutputsToSwitches() {
  for (int i = 0; i < numSwitches; i++) {
    int sw = digitalRead(switchOutputs[i].switchPin);
    if (sw == LOW) {
      digitalWrite(switchOutputs[i].outputPin, HIGH);
      switchOutputs[i].state = HIGH;
    } else {
      digitalWrite(switchOutputs[i].outputPin, LOW);
      switchOutputs[i].state = LOW;
    }
    if (currentMode == MODE_MAIN) drawDeviceRow(i);
  }
}

void applyFanSettings() {
  analogWrite(FAN_PWM_PIN, (fanOn ? fanSpeed : 0));
}

void applyUpdateRate() {
  // No additional logic needed, just uses updateRate in loop
}

// ==================== SETUP & LOOP ====================
void setup() {
  Serial.begin(9600);
  Wire.begin();

  for (int i = 0; i < numIna; i++) {
    inaDevices[i]->begin();
    inaDevices[i]->setMaxCurrentShunt(1, 0.002);
  }

  for (int i = 0; i < numSwitches; i++) {
    pinMode(switchOutputs[i].switchPin, INPUT_PULLUP);
    pinMode(switchOutputs[i].outputPin, OUTPUT);
    switchOutputs[i].debouncer.attach(switchOutputs[i].switchPin);
    switchOutputs[i].debouncer.interval(10);
  }
  for (int i = 0; i < numSwitches; i++) {
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

  tft.init(320, 480, 0, 0, ST7796S_RGB);
  tft.setRotation(1);
  ts.begin();
  ts.setRotation(1);
  tft.fillScreen(COLOR_BLACK);

  checkSDCard();

  loadSettingsFromEEPROM();
  applyFanSettings();
  applyUpdateRate();

  drawMainScreen();
}

void loop() {
  // SD Card check every 1s
  if (millis() - lastSDCheck > 1000) {
    checkSDCard();
    lastSDCheck = millis();
  }

  // Keypad mode
  if (currentMode == MODE_KEYPAD) {
    char key = keypad.getKey();
    if (key) {
      if ((key >= '0' && key <= '9') || key == '-') {
        if (keypadPos < (int)sizeof(keypadBuffer) - 1) {
          keypadBuffer[keypadPos++] = key;
          keypadBuffer[keypadPos] = 0;
        }
      } else if (key == '*') {
        keypadPos = 0;
        keypadBuffer[0] = 0;
      } else if (key == '#') {
        if (keypadMode == KEYPAD_UPDATE_RATE) {
          unsigned long val = atol(keypadBuffer);
          if (val < 10) val = 10;
          if (val > 5000) val = 5000;
          updateRate = val;
          saveSettingsToEEPROM();
          applyUpdateRate();
          currentMode = MODE_SETTINGS;
          drawSettingsPanel();
        } else if (keypadMode == KEYPAD_FAN_SPEED) {
          int val = atoi(keypadBuffer);
          if (val < 0) val = 0;
          if (val > 255) val = 255;
          fanSpeed = val;
          saveSettingsToEEPROM();
          applyFanSettings();
          currentMode = MODE_SETTINGS;
          drawSettingsPanel();
        }
        // Add other keypad modes as needed for slot/script editing
      }
      drawKeypadPanel();
    }
    return;
  }

  // Only update device data in main mode
  if (currentMode == MODE_MAIN) {
    if (millis() - lastLive > updateRate) {
      for (int i = 0; i < numIna; i++) {
        deviceVoltage[i] = inaDevices[i]->getBusVoltage();
        deviceCurrent[i] = inaDevices[i]->getCurrent_mA();
        devicePower[i]   = inaDevices[i]->getPower_mW() / 1000.0;
      }
      for (int i = 0; i < numSwitches; i++) updateLiveValueRow(i);
      lastLive = millis();
    }
  }

  if (recording && (millis() - lastRecordMillis >= updateRate)) {
    recordData();
    lastRecordMillis = millis();
  }

  // Debounce switches
  for (int i = 0; i < numSwitches; i++) {
    switchOutputs[i].debouncer.update();
    if (!lock && !safetyStop) {
      if (switchOutputs[i].debouncer.fell()) {
        digitalWrite(switchOutputs[i].outputPin, HIGH);
        switchOutputs[i].state = HIGH;
        if (currentMode == MODE_MAIN) drawDeviceRow(i);
      } else if (switchOutputs[i].debouncer.rose()) {
        digitalWrite(switchOutputs[i].outputPin, LOW);
        switchOutputs[i].state = LOW;
        if (currentMode == MODE_MAIN) drawDeviceRow(i);
      }
    }
  }

  // Handle touch input
  if (ts.touched()) {
    if (millis() - lastTouchTime > touchDebounceMs) {
      handleTouch();
      lastTouchTime = millis();
    }
  }

  digitalWrite(lockLed, lock ? HIGH : LOW);
  digitalWrite(stopLed, safetyStop ? HIGH : LOW);

  // Script execution loop (if running)
  updateScriptExecution();
}

// (Add/edit page draw/handler functions, script edit/load/save logic, slot selection, and extended script UI as needed!)
// All the provided structure supports extension for more pages and slot navigation, and script field editing.