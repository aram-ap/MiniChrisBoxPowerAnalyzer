/**
 * @file graphs.cpp
 * @brief Graph system implementation with smooth updates, diagonal lines in real-time,
 *        data persistence, and full regeneration on time-range/Y-axis changes.
 */

#include "graphs.h"
#include "config.h"
#include "display.h"
#include "sensors.h"
#include "switches.h"
#include "script.h"
#include "settings.h"
#include "network.h"
#include <EEPROM.h>
#include <cfloat>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

// External references
extern SystemState systemState;
extern GUIState guiState;
extern Adafruit_ST7796S tft;
extern ButtonRegion btnGraphBack;
extern ButtonRegion btnGraphStop;
extern ButtonRegion btnGraphClear;
extern ButtonRegion btnGraphPause;
extern ButtonRegion btnGraphSettings;
extern ButtonRegion btnGraphSettingsBack;
extern ButtonRegion btnGraphDataType;
extern ButtonRegion btnGraphMinY;
extern ButtonRegion btnGraphMaxY;
extern ButtonRegion btnGraphThickness;
extern ButtonRegion btnGraphTimeRange;
extern bool isScriptRunning;
extern long scriptTimeSeconds;

static float scriptEndTime = 0.0f;
static bool useScriptTimeline = false;

// Additional UI buttons
ButtonRegion btnGraphDisplay = {380, SCREEN_HEIGHT - 70, 80, 30, "Display", false, COLOR_YELLOW, true};
ButtonRegion btnGraphDisplayBack = {5, 5, 80, 35, "Back", false, COLOR_YELLOW, true};
ButtonRegion btnGraphDataTypeFooter = {265, SCREEN_HEIGHT - 40, 67, 35, "Current", false, COLOR_RED, true};

// Globals
GraphSettings graphSettings;
SimpleGraphData deviceGraphData[6][3];
unsigned long graphStartTime = 0;

// Rolling framebuffer (for partial shifting)
DMAMEM uint16_t graphBuffer[GRAPH_AREA_WIDTH * GRAPH_AREA_HEIGHT];

// Keep track of last rendered tab/data type, so we know when to regenerate
static GraphTab lastRenderedTab = GRAPH_TAB_ALL;
static GraphDataType lastRenderedDataType = GRAPH_CURRENT;
static bool bufferNeedsRegeneration = true;

// Minimum time range to avoid extremely high-speed shifts
static const float MIN_TIME_RANGE = 1.0f;

// Store the last plotted pixel for each device to make real-time diagonal lines
static int lastPx[6];
static int lastPy[6];
static bool hasLastPixel[6];  // Whether lastPx/Py are valid for each device

// GraphState with caching fields (you can extend or remove if not needed)
GraphState graphState = {
  0.0f, 10.0f, 0.0f, 60.0f,
  true,   // needsFullRedraw
  true,   // axesNeedUpdate
  0UL,    // lastAxesUpdate
  0UL,    // lastDataUpdate
  FLT_MAX,   // cachedOldestTime
  -FLT_MAX,  // cachedNewestTime
  true       // dataBoundsDirty
};

const uint16_t DEFAULT_GRAPH_COLORS[8] = {
  0xF800, // Red
  0x07E0, // Green
  0x001F, // Blue
  0xFFE0, // Yellow
  0x07FF, // Cyan
  0xF81F, // Magenta
  0xFD20, // Orange
  0xFFFF  // White
};

// Forward Declarations
static void regenerateBufferFromData(float minTime, float maxTime, float minY, float maxY, GraphDataType dt);
static void drawAxisLinesStatic();
static void drawBresenhamLineInBuffer(int x0, int y0, int x1, int y1, uint16_t color);
static void formatYAxisValue(float value, char* buffer, size_t bufSize);

// Formats Y-axis labels with 1–2 decimal places if needed
static void formatYAxisValue(float value, char* buffer, size_t bufSize) {
  float absVal = fabs(value);
  if (absVal >= 100.0f) {
    snprintf(buffer, bufSize, "%.0f", value);
  } else if (absVal >= 10.0f) {
    snprintf(buffer, bufSize, "%.1f", value);
  } else {
    snprintf(buffer, bufSize, "%.2f", value);
  }
}

/**
 * @brief Initializes the graph system: clears data buffers, sets up the TFT, etc.
 */
void initGraphs() {
  for (int dev = 0; dev < 6; dev++) {
    for (int dt = 0; dt < 3; dt++) {
      deviceGraphData[dev][dt].count = 0;
      deviceGraphData[dev][dt].writeIndex = 0;
    }
    hasLastPixel[dev] = false;
  }
  graphStartTime = millis();
  loadGraphSettings();

  // Enforce a minimal time range
  if (graphSettings.timeRange < MIN_TIME_RANGE) {
    graphSettings.timeRange = MIN_TIME_RANGE;
  }

  tft.setSPISpeed(30000000);
  graphState.needsFullRedraw = true;
  bufferNeedsRegeneration = true;
  Serial.println("Graphs initialized");

  // Fill the buffer with background color
  for (int i = 0; i < GRAPH_AREA_WIDTH * GRAPH_AREA_HEIGHT; i++) {
    graphBuffer[i] = GRAPH_BG_COLOR;
  }
}

/**
 * @brief Periodically called to add data from sensors to deviceGraphData.
 */
void updateGraphData(unsigned long currentMillis) {
  if (graphSettings.isPaused) return;

  static unsigned long lastUpdate = 0;
  if (currentMillis - lastUpdate < graphSettings.graphRefreshRate) return;
  lastUpdate = currentMillis;

  float currentTime;
  if (isScriptRunning) {
    currentTime = (float)scriptTimeSeconds;
  } else if (useScriptTimeline) {
    float elapsedSinceScriptEnd = (currentMillis - graphStartTime) / 1000.0f;
    currentTime = scriptEndTime + elapsedSinceScriptEnd;
  } else {
    currentTime = (currentMillis - graphStartTime) / 1000.0f;
  }

  // For each device, add new data points
  for (int dev = 0; dev < 6; dev++) {
    int inaIdx = getInaIndexForSwitch(dev);
    if (inaIdx < 0) continue;

    float I = deviceCurrent[inaIdx] / 1000.0f;  // Convert mA -> A
    float V = deviceVoltage[inaIdx];
    float P = devicePower[inaIdx];

    addGraphPoint(dev, GRAPH_CURRENT, currentTime, I);
    addGraphPoint(dev, GRAPH_VOLTAGE, currentTime, V);
    addGraphPoint(dev, GRAPH_POWER, currentTime, P);
  }
}

/**
 * @brief Called when a script ends to update timeline references if needed.
 */
void onScriptEnd() {
  if (isScriptRunning && useScriptTimeline) {
    scriptEndTime = (float)scriptTimeSeconds;
    graphStartTime = millis();
  }
}

void addGraphPoint(int deviceIndex, GraphDataType dataType, float timeValue, float dataValue) {
  if (deviceIndex < 0 || deviceIndex >= 6) return;
  if (dataType > GRAPH_POWER) return;

  SimpleGraphData* gd = &deviceGraphData[deviceIndex][dataType];
  gd->timePoints[gd->writeIndex] = timeValue;
  gd->dataPoints[gd->writeIndex] = dataValue;

  gd->writeIndex = (gd->writeIndex + 1) % graphSettings.effectiveMaxPoints;
  if (gd->count < graphSettings.effectiveMaxPoints) {
    gd->count++;
  }
}

/**
 * @return The current device data (e.g. last sensor reading) for the chosen data type
 */
float getDeviceGraphValue(int deviceIndex, GraphDataType dataType) {
  int inaIdx = getInaIndexForSwitch(deviceIndex);
  if (inaIdx < 0) return 0.0f;

  if (dataType == GRAPH_CURRENT) return deviceCurrent[inaIdx] / 1000.0f;
  if (dataType == GRAPH_VOLTAGE) return deviceVoltage[inaIdx];
  if (dataType == GRAPH_POWER)   return devicePower[inaIdx];
  return 0.0f;
}

/**
 * @brief Clears all graph data arrays and the rolling buffer.
 */
void clearGraphData() {
  for (int dev = 0; dev < 6; dev++) {
    for (int dt = 0; dt < 3; dt++) {
      deviceGraphData[dev][dt].count = 0;
      deviceGraphData[dev][dt].writeIndex = 0;
    }
    hasLastPixel[dev] = false;
  }
  graphStartTime = millis();
  useScriptTimeline = false;
  scriptEndTime = 0.0f;
  graphState.needsFullRedraw = true;
  bufferNeedsRegeneration = true;
  Serial.println("Graph data cleared.");

  // Fill buffer with background
  for (int i = 0; i < GRAPH_AREA_WIDTH * GRAPH_AREA_HEIGHT; i++) {
    graphBuffer[i] = GRAPH_BG_COLOR;
  }
}

void pauseGraphData() {
  graphSettings.isPaused = true;
  float currTime = (isScriptRunning)
                   ? (float)scriptTimeSeconds
                   : (millis() - graphStartTime) / 1000.0f;
  graphSettings.pausedMinTime = currTime - graphSettings.timeRange;
  graphSettings.pausedMaxTime = currTime;
  bufferNeedsRegeneration = true;
  saveGraphSettings();
}

void resumeGraphData() {
  graphSettings.isPaused = false;
  bufferNeedsRegeneration = true;
  saveGraphSettings();
}

// Save and Load Graph Settings from EEPROM
void saveGraphSettings() {
  EEPROM.put(EEPROM_GRAPH_SETTINGS_ADDR, graphSettings);
}

void loadGraphSettings() {
  GraphSettings def;
  // Setup defaults
  for (int i = 0; i < 6; i++) {
    def.devices[i].enabled = true;
    def.devices[i].dataType = GRAPH_CURRENT;
    def.devices[i].lineColor = DEFAULT_GRAPH_COLORS[i];
    def.devices[i].autoScale = false;
    def.devices[i].axisRanges[GRAPH_CURRENT][0] = 0.0f;
    def.devices[i].axisRanges[GRAPH_CURRENT][1] = 10.0f;
    def.devices[i].axisRanges[GRAPH_VOLTAGE][0] = 0.0f;
    def.devices[i].axisRanges[GRAPH_VOLTAGE][1] = 30.0f;
    def.devices[i].axisRanges[GRAPH_POWER][0] = 0.0f;
    def.devices[i].axisRanges[GRAPH_POWER][1] = 100.0f;
  }
  def.all.dataType = GRAPH_CURRENT;
  for (int i = 0; i < 6; i++) def.all.deviceEnabled[i] = true;
  def.all.autoScale = false;
  def.all.lineThickness = 1;
  def.all.axisRanges[GRAPH_CURRENT][0] = 0.0f;
  def.all.axisRanges[GRAPH_CURRENT][1] = 10.0f;
  def.all.axisRanges[GRAPH_VOLTAGE][0] = 0.0f;
  def.all.axisRanges[GRAPH_VOLTAGE][1] = 30.0f;
  def.all.axisRanges[GRAPH_POWER][0] = 0.0f;
  def.all.axisRanges[GRAPH_POWER][1] = 100.0f;
  def.isPaused = false;
  def.autoScroll = true;
  def.showAxesLabels = true;
  def.timeRange = 60.0f;
  def.panOffsetX = 0.0f;
  def.panOffsetY = 0.0f;
  def.enablePanning = false;
  def.autoFitEnabled = true;
  def.effectiveMaxPoints = GRAPH_MAX_POINTS;
  def.graphRefreshRate = GRAPH_UPDATE_INTERVAL;
  def.enableAntialiasing = false;
  def.showGrids = true;
  def.pausedMinTime = 0.0f;
  def.pausedMaxTime = 0.0f;

  GraphSettings readSettings;
  EEPROM.get(EEPROM_GRAPH_SETTINGS_ADDR, readSettings);

  bool valid = true;
  if ((readSettings.timeRange < 0.01f) || (readSettings.timeRange > 300.0f)) valid = false;
  if (readSettings.effectiveMaxPoints < 10 || readSettings.effectiveMaxPoints > GRAPH_MAX_POINTS) valid = false;
  if (readSettings.graphRefreshRate < 20 || readSettings.graphRefreshRate > 200) valid = false;

  if (!valid) {
    graphSettings = def;
    saveGraphSettings();
    Serial.println("Graph settings invalid/corrupted, loaded defaults.");
  } else {
    graphSettings = readSettings;
    Serial.println("Graph settings loaded from EEPROM.");
  }

  if (graphSettings.timeRange < MIN_TIME_RANGE) {
    graphSettings.timeRange = MIN_TIME_RANGE;
  }
}

void resetGraphSettings() {
  loadGraphSettings();
  Serial.println("Graph settings reset.");
}

// Graph Page UI
void drawGraphPage() {
  tft.fillScreen(COLOR_BLACK);

  drawButton(btnGraphBack, COLOR_YELLOW, COLOR_BLACK, "Back", false, true);
  drawGraphTabs();

  GraphDataType dt = (guiState.currentGraphTab == GRAPH_TAB_ALL)
                     ? graphSettings.all.dataType
                     : graphSettings.devices[static_cast<int>(guiState.currentGraphTab)-1].dataType;

  const char* dataTypeNames[3] = {"Current","Voltage","Power"};
  uint16_t dtColor[3] = {COLOR_RED, COLOR_BLUE, 0xF81F};

  drawButton(btnGraphDataTypeFooter, dtColor[dt], COLOR_WHITE, dataTypeNames[dt], false, true);

  // If tab or data type changed, we want a regeneration
  if (lastRenderedTab != guiState.currentGraphTab || lastRenderedDataType != dt) {
    bufferNeedsRegeneration = true;
    lastRenderedTab = guiState.currentGraphTab;
    lastRenderedDataType = dt;
    for (int dev=0; dev<6; dev++) {
      hasLastPixel[dev] = false;
    }
  }

  graphState.needsFullRedraw = true;
  drawGraphArea();
  drawGraphInfo();

  // Buttons at bottom
  drawButton(btnGraphClear, COLOR_YELLOW, COLOR_BLACK, "Clear", false, true);
  drawButton(btnGraphPause,
             graphSettings.isPaused? COLOR_GREEN:COLOR_YELLOW,
             COLOR_BLACK,
             graphSettings.isPaused?"Resume":"Pause",
             false,true);
  drawButton(btnGraphSettings, COLOR_YELLOW, COLOR_BLACK, "Settings", false, true);

  drawButton(btnGraphStop,
             systemState.safetyStop?COLOR_PURPLE:COLOR_YELLOW,
             systemState.safetyStop?COLOR_WHITE:COLOR_BLACK,
             systemState.safetyStop?"RELEASE":"STOP",
             false,true);
}

/**
 * @brief Draws the script timer on the top right corner of the graph.
 */
void drawScriptTimer() {
  if (!isScriptRunning || guiState.currentMode != MODE_GRAPH) return;

  tft.fillRect(GRAPH_AREA_X + GRAPH_AREA_WIDTH - 60, GRAPH_AREA_Y + 2, 58, 15, GRAPH_BG_COLOR);

  tft.setFont();
  tft.setTextColor(COLOR_YELLOW);
  char timeBuf[16];
  if (scriptTimeSeconds < 0) {
    sprintf(timeBuf, "T%ld", scriptTimeSeconds);
  } else {
    sprintf(timeBuf, "T+%ld", scriptTimeSeconds);
  }
  tft.setCursor(GRAPH_AREA_X + GRAPH_AREA_WIDTH - 55, GRAPH_AREA_Y + 10);
  tft.print(timeBuf);
}

// Display settings page (unchanged from before or minimal changes)
void drawGraphDisplaySettingsPage() {
  tft.fillScreen(COLOR_BLACK);

  drawButton(btnGraphDisplayBack, COLOR_YELLOW, COLOR_BLACK, "Back", false, true);

  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(150, 30);
  tft.print("Display Settings");

  tft.setFont(&FreeSans9pt7b);
  int yPos = 60;

  // Antialiasing
  tft.setCursor(20, yPos);
  tft.print("Antialiasing:");
  tft.drawRect(180, yPos - 15, 25, 25, COLOR_WHITE);
  if (graphSettings.enableAntialiasing) {
    tft.fillRect(183, yPos - 12, 19, 19, COLOR_GREEN);
  }
  yPos += 40;

  // Show Grids
  tft.setCursor(20, yPos);
  tft.print("Show Grids:");
  tft.drawRect(180, yPos - 15, 25, 25, COLOR_WHITE);
  if (graphSettings.showGrids) {
    tft.fillRect(183, yPos - 12, 19, 19, COLOR_GREEN);
  }
  yPos += 40;

  // Max Points
  tft.setCursor(20, yPos);
  tft.print("Max Points:");
  char pointsBuf[16];
  sprintf(pointsBuf, "%d", graphSettings.effectiveMaxPoints);
  tft.drawRect(180, yPos - 15, 80, 25, COLOR_YELLOW);
  tft.setCursor(185, yPos);
  tft.print(pointsBuf);
  yPos += 40;

  // Refresh Rate
  tft.setCursor(20, yPos);
  tft.print("Refresh Rate (ms):");
  char refreshBuf[16];
  sprintf(refreshBuf, "%lu", graphSettings.graphRefreshRate);
  tft.drawRect(180, yPos - 15, 80, 25, COLOR_YELLOW);
  tft.setCursor(185, yPos);
  tft.print(refreshBuf);
}

/**
 * @brief The Graph Settings Page
 */
void drawGraphSettingsPage() {
  tft.fillScreen(COLOR_BLACK);

  drawButton(btnGraphSettingsBack, COLOR_YELLOW, COLOR_BLACK, "Back", false, true);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_WHITE);

  String title = (guiState.currentGraphTab == GRAPH_TAB_ALL)
                ? "All Devices Settings"
                : String(switchOutputs[static_cast<int>(guiState.currentGraphTab)-1].name) + " Settings";
  tft.setCursor((SCREEN_WIDTH - (title.length() * 6)) / 2, 20);
  tft.print(title);

  if (guiState.currentGraphTab == GRAPH_TAB_ALL) {
    drawAllGraphSettings();
  } else {
    drawDeviceGraphSettings(static_cast<int>(guiState.currentGraphTab)-1);
  }
}

/**
 * @brief Updates the graph area in real-time, partially shifting & drawing diagonal lines.
 */
void updateGraphAreaSmooth() {
  // Ensure we only update the graph if we're actually on the graph page
  if (guiState.currentMode != MODE_GRAPH) return;

  // Determine current timeline reference
  float currTime;
  if (isScriptRunning) {
    currTime = (float)scriptTimeSeconds;
  } else if (useScriptTimeline) {
    float elapsedSinceScriptEnd = (millis() - graphStartTime) / 1000.0f;
    currTime = scriptEndTime + elapsedSinceScriptEnd;
  } else {
    currTime = (millis() - graphStartTime) / 1000.0f;
  }

  // Compute minTime, maxTime based on time range, pause state
  float minTime, maxTime;
  if (graphSettings.isPaused) {
    minTime = graphSettings.pausedMinTime;
    maxTime = graphSettings.pausedMaxTime;
  } else {
    // Enforce a minimal time range
    if (graphSettings.timeRange < MIN_TIME_RANGE) {
      graphSettings.timeRange = MIN_TIME_RANGE;
    }
    minTime = currTime - graphSettings.timeRange;
    maxTime = currTime;
  }

  // Identify which GraphDataType to plot
  GraphDataType dt = (guiState.currentGraphTab == GRAPH_TAB_ALL)
    ? graphSettings.all.dataType
    : graphSettings.devices[(int)guiState.currentGraphTab - 1].dataType;

  // Determine current Y-axis min/max
  float minY, maxY;
  if (guiState.currentGraphTab == GRAPH_TAB_ALL) {
    minY = graphSettings.all.axisRanges[dt][0];
    maxY = graphSettings.all.axisRanges[dt][1];
  } else {
    int devIdx = (int)guiState.currentGraphTab - 1;
    minY = graphSettings.devices[devIdx].axisRanges[dt][0];
    maxY = graphSettings.devices[devIdx].axisRanges[dt][1];
  }

  if (graphSettings.autoFitEnabled) {
    float dataMin = FLT_MAX;
    float dataMax = -FLT_MAX;
    bool hasData = false;

    if (guiState.currentGraphTab == GRAPH_TAB_ALL) {
      // Combine all enabled devices
      for (int dev = 0; dev < 6; dev++) {
        if (!graphSettings.all.deviceEnabled[dev]) continue;
        SimpleGraphData* gd = &deviceGraphData[dev][dt];

        for (int i = 0; i < gd->count; i++) {
          int idx = (gd->writeIndex - gd->count + i) % graphSettings.effectiveMaxPoints;
          if (idx < 0) idx += graphSettings.effectiveMaxPoints;

          float tVal = gd->timePoints[idx];
          if (tVal < minTime || tVal > maxTime) continue;

          float val = gd->dataPoints[idx];
          if (!isnan(val) && !isinf(val)) {
            dataMin = min(dataMin, val);
            dataMax = max(dataMax, val);
            hasData = true;
          }
        }
      }
    } else {
      // Single device
      int devIdx = (int)guiState.currentGraphTab - 1;
      SimpleGraphData* gd = &deviceGraphData[devIdx][dt];

      for (int i = 0; i < gd->count; i++) {
        int idx = (gd->writeIndex - gd->count + i) % graphSettings.effectiveMaxPoints;
        if (idx < 0) idx += graphSettings.effectiveMaxPoints;

        float tVal = gd->timePoints[idx];
        if (tVal < minTime || tVal > maxTime) continue;

        float val = gd->dataPoints[idx];
        if (!isnan(val) && !isinf(val)) {
          dataMin = min(dataMin, val);
          dataMax = max(dataMax, val);
          hasData = true;
        }
      }
    }

    if (hasData && dataMin < dataMax) {
      float range = dataMax - dataMin;
      if (range < 0.001f) range = 1.0f;
      minY = dataMin - range * 0.1f;
      maxY = dataMax + range * 0.1f;
    }
  }

  // Ensure minY < maxY
  if (maxY <= minY) {
    maxY = minY + 1.0f;
  }

  // Detect changes in time or Y-axis
  static float lastMinTime = 0, lastMaxTime = 0;
  static float lastMinY = 0, lastMaxY = 0;
  bool changedRangeOrAxis = false;
  if (fabs(lastMinTime - minTime) > 0.001f || fabs(lastMaxTime - maxTime) > 0.001f ||
      fabs(lastMinY - minY)   > 0.001f || fabs(lastMaxY - maxY)   > 0.001f) {
    changedRangeOrAxis = true;
    lastMinTime = minTime;
    lastMaxTime = maxTime;
    lastMinY = minY;
    lastMaxY = maxY;
    // We'll force an axis label redraw
    graphState.axesNeedUpdate = true;
  }

  // If we need a full buffer regeneration
  if (bufferNeedsRegeneration || changedRangeOrAxis) {
    // Do not do partial shifts
    regenerateBufferFromData(minTime, maxTime, minY, maxY, dt);
    bufferNeedsRegeneration = false;
  } else {
    // Otherwise partial shift is done only if graph is not paused
    if (!graphSettings.isPaused) {
      static unsigned long lastShift = 0;
      unsigned long now = millis();
      if (now - lastShift >= graphSettings.graphRefreshRate) {
        // SHIFT entire buffer left by 1
        for (int y = 0; y < GRAPH_AREA_HEIGHT; y++) {
          memmove(&graphBuffer[y * GRAPH_AREA_WIDTH],
                  &graphBuffer[y * GRAPH_AREA_WIDTH + 1],
                  (GRAPH_AREA_WIDTH - 1) * sizeof(uint16_t));
          // Clear new rightmost column
          graphBuffer[y * GRAPH_AREA_WIDTH + (GRAPH_AREA_WIDTH - 1)] = GRAPH_BG_COLOR;
        }

        // If grids are on, add horizontal lines at the rightmost column
        if (graphSettings.showGrids) {
          for (int i = 1; i < 5; i++) {
            int gy = (GRAPH_AREA_HEIGHT * i) / 5;
            graphBuffer[gy * GRAPH_AREA_WIDTH + (GRAPH_AREA_WIDTH - 1)] = GRAPH_GRID_COLOR;
          }
        }

        // Now draw a new diagonal line from last two data points
        if (guiState.currentGraphTab == GRAPH_TAB_ALL) {
          for (int dev = 0; dev < 6; dev++) {
            if (!graphSettings.all.deviceEnabled[dev]) continue;
            SimpleGraphData* gd = &deviceGraphData[dev][dt];
            if (gd->count < 2) continue;

            int idx2 = (gd->writeIndex - 1 + graphSettings.effectiveMaxPoints) % graphSettings.effectiveMaxPoints;
            int idx1 = (gd->writeIndex - 2 + graphSettings.effectiveMaxPoints) % graphSettings.effectiveMaxPoints;
            float val1 = gd->dataPoints[idx1];
            float val2 = gd->dataPoints[idx2];

            float yNorm1 = (val1 - minY) / (maxY - minY);
            float yNorm2 = (val2 - minY) / (maxY - minY);
            yNorm1 = constrain(yNorm1, 0.0f, 1.0f);
            yNorm2 = constrain(yNorm2, 0.0f, 1.0f);

            int py1 = (GRAPH_AREA_HEIGHT - 1) - (int)(yNorm1 * (GRAPH_AREA_HEIGHT - 1));
            int py2 = (GRAPH_AREA_HEIGHT - 1) - (int)(yNorm2 * (GRAPH_AREA_HEIGHT - 1));
            int px1 = GRAPH_AREA_WIDTH - 2; // second last col
            int px2 = GRAPH_AREA_WIDTH - 1; // last col

            drawBresenhamLineInBuffer(px1, py1, px2, py2, graphSettings.devices[dev].lineColor);
          }
        } else {
          int dIdx = (int)guiState.currentGraphTab - 1;
          SimpleGraphData* gd = &deviceGraphData[dIdx][dt];
          if (gd->count >= 2) {
            int idx2 = (gd->writeIndex - 1 + graphSettings.effectiveMaxPoints) % graphSettings.effectiveMaxPoints;
            int idx1 = (gd->writeIndex - 2 + graphSettings.effectiveMaxPoints) % graphSettings.effectiveMaxPoints;
            float val1 = gd->dataPoints[idx1];
            float val2 = gd->dataPoints[idx2];

            float yNorm1 = (val1 - minY) / (maxY - minY);
            float yNorm2 = (val2 - minY) / (maxY - minY);
            yNorm1 = constrain(yNorm1, 0.0f, 1.0f);
            yNorm2 = constrain(yNorm2, 0.0f, 1.0f);

            int py1 = (GRAPH_AREA_HEIGHT - 1) - (int)(yNorm1 * (GRAPH_AREA_HEIGHT - 1));
            int py2 = (GRAPH_AREA_HEIGHT - 1) - (int)(yNorm2 * (GRAPH_AREA_HEIGHT - 1));
            int px1 = GRAPH_AREA_WIDTH - 2;
            int px2 = GRAPH_AREA_WIDTH - 1;
            drawBresenhamLineInBuffer(px1, py1, px2, py2, graphSettings.devices[dIdx].lineColor);
          }
        }

        lastShift = now;
      }
    }
  }

  // Draw the updated buffer to screen
  tft.drawRGBBitmap(GRAPH_AREA_X, GRAPH_AREA_Y, graphBuffer, GRAPH_AREA_WIDTH, GRAPH_AREA_HEIGHT);

  // If we need new axis labels, draw them over the buffer
  if (graphState.axesNeedUpdate) {
    drawAxesLabelsSmooth(minTime, maxTime, minY, maxY, dt);
    graphState.axesNeedUpdate = false;
  }
}

/**
 * @brief Rebuilds the entire rolling buffer from the stored data (for new time range or tab).
 */
static void regenerateBufferFromData(float minTime, float maxTime, float minY, float maxY, GraphDataType dt) {
  // Clear buffer
  for (int i=0; i< GRAPH_AREA_WIDTH* GRAPH_AREA_HEIGHT; i++) {
    graphBuffer[i] = GRAPH_BG_COLOR;
  }

  // Possibly add horizontal grid lines
  if (graphSettings.showGrids) {
    for (int i=1; i<5; i++) {
      int gy= (GRAPH_AREA_HEIGHT*i)/5;
      for (int x=0; x<GRAPH_AREA_WIDTH; x++) {
        graphBuffer[gy*GRAPH_AREA_WIDTH + x]= GRAPH_GRID_COLOR;
      }
    }
  }

  // For each relevant device, plot lines
  if (guiState.currentGraphTab == GRAPH_TAB_ALL) {
    for (int dev=0; dev<6; dev++) {
      if (!graphSettings.all.deviceEnabled[dev]) continue;
      SimpleGraphData* gd= &deviceGraphData[dev][dt];
      if (gd->count<2) continue;

      int prevPx= -1, prevPy= -1;
      uint16_t color= graphSettings.devices[dev].lineColor;

      for (int i=0; i< gd->count; i++){
        int idx=(gd->writeIndex- gd->count + i) % graphSettings.effectiveMaxPoints;
        if (idx<0) idx+= graphSettings.effectiveMaxPoints;

        float tVal= gd->timePoints[idx];
        if (tVal< minTime || tVal>maxTime) continue;

        float val= gd->dataPoints[idx];
        if (isnan(val)|| isinf(val)) continue;

        float tNorm= (tVal- minTime)/(maxTime- minTime);
        tNorm= constrain(tNorm,0.0f,1.0f);
        float yNorm= (val- minY)/(maxY- minY);
        yNorm= constrain(yNorm,0.0f,1.0f);

        int px= (int)( tNorm* (GRAPH_AREA_WIDTH -1));
        int py= (GRAPH_AREA_HEIGHT -1) - (int)( yNorm* (GRAPH_AREA_HEIGHT -1));
        if (prevPx>=0 && prevPy>=0) {
          drawBresenhamLineInBuffer(prevPx, prevPy, px, py, color);
        }
        prevPx= px;
        prevPy= py;
      }
    }
  } else {
    int devIdx= (int)guiState.currentGraphTab -1;
    SimpleGraphData* gd= &deviceGraphData[devIdx][dt];
    if (gd->count>=2){
      int prevPx= -1, prevPy= -1;
      uint16_t color= graphSettings.devices[devIdx].lineColor;
      for (int i=0;i< gd->count;i++){
        int idx=(gd->writeIndex- gd->count + i) % graphSettings.effectiveMaxPoints;
        if (idx<0) idx+= graphSettings.effectiveMaxPoints;

        float tVal= gd->timePoints[idx];
        if (tVal< minTime || tVal>maxTime) continue;

        float val= gd->dataPoints[idx];
        if (isnan(val)|| isinf(val)) continue;

        float tNorm= (tVal- minTime)/(maxTime- minTime);
        tNorm= constrain(tNorm,0.0f,1.0f);
        float yNorm= (val- minY)/(maxY- minY);
        yNorm= constrain(yNorm,0.0f,1.0f);

        int px=(int)( tNorm* (GRAPH_AREA_WIDTH -1));
        int py=(GRAPH_AREA_HEIGHT -1)- (int)( yNorm*(GRAPH_AREA_HEIGHT -1));
        if (prevPx>=0 && prevPy>=0){
          drawBresenhamLineInBuffer(prevPx, prevPy, px, py, color);
        }
        prevPx= px;
        prevPy= py;
      }
    }
  }
}

/**
 * @brief A helper function to draw a line in the rolling buffer using Bresenham's algorithm.
 */
static void drawBresenhamLineInBuffer(int x0, int y0, int x1, int y1, uint16_t color){
  int dx= abs(x1- x0), dy= abs(y1- y0);
  int sx= (x0< x1)?1:-1;
  int sy= (y0< y1)?1:-1;
  int err= dx- dy;
  int x= x0, y= y0;

  while(true){
    if(x>=0 && x< GRAPH_AREA_WIDTH && y>=0 && y< GRAPH_AREA_HEIGHT){
      graphBuffer[y* GRAPH_AREA_WIDTH + x]= color;
    }
    if(x== x1 && y== y1){
      break;
    }
    int e2= 2* err;
    if (e2> -dy){ err-= dy; x+= sx; }
    if (e2< dx){ err+= dx; y+= sy; }
  }
}

/**
 * @brief Renders axis labels and calls drawAxisLinesStatic.
 */
void drawAxesLabelsSmooth(float minTime, float maxTime, float minY, float maxY, GraphDataType dt) {
  tft.setFont();
  tft.setTextColor(COLOR_WHITE);

  // Clear Y-axis label area
  tft.fillRect(0, GRAPH_AREA_Y-5, GRAPH_AREA_X-2, GRAPH_AREA_HEIGHT+10, COLOR_BLACK);

  // Y-axis unit label
  const char* unit= getGraphDataTypeUnit(dt);
  int unitWidth= strlen(unit)*6;
  tft.setCursor(GRAPH_AREA_X-25- unitWidth, GRAPH_AREA_Y + GRAPH_AREA_HEIGHT/2);
  tft.print(unit);

  // Y-axis labels
  for (int i=0; i<=5; i++){
    float val= minY + (maxY- minY)* i/ 5.0f;
    int y= GRAPH_AREA_Y + GRAPH_AREA_HEIGHT - (i* GRAPH_AREA_HEIGHT/5);
    char buf[12];
    formatYAxisValue(val, buf, sizeof(buf));
    tft.setCursor(GRAPH_AREA_X - (strlen(buf)*6) -8, y-2);
    tft.print(buf);
  }

  // Clear X-axis label area
  tft.fillRect(GRAPH_AREA_X, GRAPH_AREA_Y + GRAPH_AREA_HEIGHT+2, GRAPH_AREA_WIDTH, 15, COLOR_BLACK);

  // X-axis labels
  for(int i=0;i<=4;i++){
    float xVal= minTime + ((maxTime- minTime)* i/ 4.0f);
    int x= GRAPH_AREA_X + (i* GRAPH_AREA_WIDTH/4);
    char buf[12];
    if(graphSettings.timeRange<1.0f){
      sprintf(buf,"%.2f", xVal);
    } else if(graphSettings.timeRange<10.0f){
      sprintf(buf,"%.1f", xVal);
    } else {
      sprintf(buf,"%.0f", xVal);
    }
    tft.setCursor(x - (strlen(buf)*3), GRAPH_AREA_Y + GRAPH_AREA_HEIGHT +10);
    tft.print(buf);
  }

  // Draw axis lines outside the buffer area
  drawAxisLinesStatic();
}

/**
 * @brief Actually draws the static X and Y axis lines around the scrolling area.
 */
static void drawAxisLinesStatic(){
  // X-axis line
  tft.drawFastHLine(GRAPH_AREA_X, GRAPH_AREA_Y + GRAPH_AREA_HEIGHT, GRAPH_AREA_WIDTH, COLOR_WHITE);
  // Y-axis line
  tft.drawFastVLine(GRAPH_AREA_X-1, GRAPH_AREA_Y, GRAPH_AREA_HEIGHT, COLOR_WHITE);
}

/**
 * @brief Called once when the page is first drawn or when we need a redraw.
 */
void drawGraphArea() {
  if(graphState.needsFullRedraw){
    // Clear a border around the graph
    tft.fillRect(GRAPH_AREA_X-2, GRAPH_AREA_Y-2, GRAPH_AREA_WIDTH+4, GRAPH_AREA_HEIGHT+4, COLOR_BLACK);
    // Outline
    tft.drawRect(GRAPH_AREA_X-1, GRAPH_AREA_Y-1, GRAPH_AREA_WIDTH+2, GRAPH_AREA_HEIGHT+2, COLOR_WHITE);
    graphState.needsFullRedraw=false;
    graphState.axesNeedUpdate=true;
  }
  updateGraphAreaSmooth();
}

/**
 * @brief Draw the colored tab rectangles with labels
 */
void drawGraphTabs() {
  const char* labels[] = {"All","GSE1","GSE2","TER","TE1","TE2","TE3"};
  int tabWidth=50;
  int startX=90;

  for(int i=0; i<7; i++){
    uint16_t col=(i== (int)guiState.currentGraphTab)? COLOR_BLUE: COLOR_GRAY;
    tft.fillRect(startX + i* tabWidth, 5, tabWidth-2, GRAPH_TAB_HEIGHT, col);
    tft.drawRect(startX + i* tabWidth, 5, tabWidth-2, GRAPH_TAB_HEIGHT, COLOR_WHITE);

    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(startX + i* tabWidth+5, 5 + (GRAPH_TAB_HEIGHT/2)+5);
    tft.print(labels[i]);
  }
}

/**
 * @brief Just a horizontal grid if needed (unused if we do partial shifting)
 */
void drawGraphGrid() {
  // We do only horizontal lines
  for(int i=1;i<5;i++){
    int y= GRAPH_AREA_Y + (GRAPH_AREA_HEIGHT* i)/5;
    tft.drawLine(GRAPH_AREA_X+1, y, GRAPH_AREA_X + GRAPH_AREA_WIDTH-1, y, GRAPH_GRID_COLOR);
  }
}

void drawGraphData() {
  updateGraphAreaSmooth();
}

/**
 * @brief Show live data or device info on the side panel
 */
void drawGraphInfo() {
  int infoX= GRAPH_AREA_X + GRAPH_AREA_WIDTH +5;
  int infoY= GRAPH_AREA_Y;

  tft.fillRect(infoX, infoY, GRAPH_INFO_WIDTH, GRAPH_AREA_HEIGHT, COLOR_BLACK);
  tft.drawRect(infoX, infoY, GRAPH_INFO_WIDTH, GRAPH_AREA_HEIGHT, COLOR_WHITE);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_WHITE);

  if(guiState.currentGraphTab== GRAPH_TAB_ALL){
    tft.setCursor(infoX+5, infoY+15);
    tft.print("Live Data:");

    GraphDataType dt= graphSettings.all.dataType;
    const char* unit= getGraphDataTypeUnit(dt);
    int yPos= infoY+30;

    for(int dev=0; dev<6; dev++){
      if(!graphSettings.all.deviceEnabled[dev]) continue;

      float val= getDeviceGraphValue(dev, dt);

      // color each device's label
      tft.setTextColor(graphSettings.devices[dev].lineColor);
      tft.setCursor(infoX+5, yPos);
      tft.print(switchOutputs[dev].name);

      // on/off status
      tft.setTextColor(switchOutputs[dev].state? COLOR_GREEN: COLOR_RED);
      tft.setFont();
      tft.print(switchOutputs[dev].state? " ON":" OFF");
      tft.setFont(&FreeSans9pt7b);

      char buf[16];
      sprintf(buf,"%.3f%s", val, unit);
      tft.setTextColor(COLOR_WHITE);
      tft.setCursor(infoX+5, yPos+15);
      tft.print(buf);

      yPos+=30;
      if(yPos> (infoY+ GRAPH_AREA_HEIGHT-20)) break;
    }
  } else {
    int devIdx= (int)guiState.currentGraphTab -1;
    tft.setCursor(infoX+5, infoY+15);
    tft.print(switchOutputs[devIdx].name);

    int yPos= infoY+35;
    float I= getDeviceGraphValue(devIdx, GRAPH_CURRENT);
    tft.setTextColor(COLOR_CYAN);
    tft.setCursor(infoX+5, yPos);
    tft.printf("I: %.3fA", I);
    yPos+=25;

    float V= getDeviceGraphValue(devIdx, GRAPH_VOLTAGE);
    tft.setTextColor(COLOR_GREEN);
    tft.setCursor(infoX+5, yPos);
    tft.printf("V: %.2fV", V);
    yPos+=25;

    float P= getDeviceGraphValue(devIdx, GRAPH_POWER);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(infoX+5, yPos);
    tft.printf("P: %.3fW", P);
    yPos+=25;

    bool st= switchOutputs[devIdx].state;
    tft.setTextColor(st?COLOR_GREEN:COLOR_RED);
    tft.setCursor(infoX+5, yPos);
    tft.print("State: ");
    tft.print(st? "ON":"OFF");
  }

  if(isScriptRunning){
    drawScriptTimer();
  }
}

void drawAxesLabels() {
  float currTime= (isScriptRunning)
                  ? (float)scriptTimeSeconds
                  : (millis()- graphStartTime)/1000.0f;

  float minTime, maxTime;
  if (graphSettings.isPaused) {
    minTime= graphSettings.pausedMinTime;
    maxTime= graphSettings.pausedMaxTime;
  } else {
    if (graphSettings.timeRange < MIN_TIME_RANGE) {
      graphSettings.timeRange= MIN_TIME_RANGE;
    }
    minTime= currTime - graphSettings.timeRange;
    maxTime= currTime;
  }

  GraphDataType dt= (guiState.currentGraphTab== GRAPH_TAB_ALL)
                    ? graphSettings.all.dataType
                    : graphSettings.devices[(int)guiState.currentGraphTab-1].dataType;
  float minY, maxY;
  if(guiState.currentGraphTab== GRAPH_TAB_ALL){
    minY= graphSettings.all.axisRanges[dt][0];
    maxY= graphSettings.all.axisRanges[dt][1];
  } else {
    int dd= (int)guiState.currentGraphTab-1;
    minY= graphSettings.devices[dd].axisRanges[dt][0];
    maxY= graphSettings.devices[dd].axisRanges[dt][1];
  }
  drawAxesLabelsSmooth(minTime, maxTime, minY, maxY, dt);
}

void drawAllGraphSettings() {
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_WHITE);

  int yPos=50;

  // Data Type button
  tft.setCursor(20, yPos);
  tft.print("Data Type:");
  const char* dtLabels[3]={"Current","Voltage","Power"};
  btnGraphDataType.x=150;
  btnGraphDataType.y=yPos-15;
  drawButton(btnGraphDataType, COLOR_YELLOW, COLOR_BLACK, dtLabels[graphSettings.all.dataType], false,true);
  yPos+=35;

  // Devices
  tft.setCursor(20, yPos);
  tft.print("Devices to Show:");
  yPos+=25;
  for(int i=0;i<6;i++){
    int buttonX= 30 + (i%3)*140;
    int buttonY= yPos + (i/3)*40;
    uint16_t btnColor= graphSettings.all.deviceEnabled[i]? COLOR_RED: COLOR_GRAY;
    tft.fillRect(buttonX, buttonY, 120,30, btnColor);
    tft.drawRect(buttonX, buttonY, 120,30, COLOR_WHITE);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(buttonX+5, buttonY+20);
    tft.print(switchOutputs[i].name);
  }
  yPos+=85;

  // Y-Axis Range
  GraphDataType dt= graphSettings.all.dataType;
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(20, yPos);
  tft.print("Y-Axis Range:");
  yPos+=20;

  tft.setCursor(20, yPos);
  tft.print("Min:");
  float minVal= graphSettings.all.axisRanges[dt][0];
  char minBuf[16];
  sprintf(minBuf,"%.2f", minVal);
  btnGraphMinY.x=60;
  btnGraphMinY.y=yPos-15;
  drawButton(btnGraphMinY, COLOR_YELLOW, COLOR_BLACK, minBuf, false,true);

  tft.setCursor(160,yPos);
  tft.print("Max:");
  float maxVal= graphSettings.all.axisRanges[dt][1];
  char maxBuf[16];
  sprintf(maxBuf,"%.2f", maxVal);
  btnGraphMaxY.x=200;
  btnGraphMaxY.y=yPos-15;
  drawButton(btnGraphMaxY, COLOR_YELLOW, COLOR_BLACK, maxBuf, false,true);
  yPos+=35;

  // Auto Scale
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(20,yPos);
  tft.print("Auto Scale:");
  tft.drawRect(120,yPos-15,25,25, COLOR_WHITE);
  if(graphSettings.autoFitEnabled){
    tft.fillRect(123,yPos-12,19,19, COLOR_GREEN);
  }

  // Line Width
  tft.setCursor(200,yPos);
  tft.print("Line Width:");
  char thickBuf[8];
  sprintf(thickBuf,"%d", graphSettings.all.lineThickness);
  btnGraphThickness.x=290;
  btnGraphThickness.y=yPos-15;
  drawButton(btnGraphThickness, COLOR_YELLOW, COLOR_BLACK, thickBuf, false,true);
  yPos+=35;

  // Time Range
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(20,yPos);
  tft.print("Time Range (s):");
  char timeBuf[16];
  sprintf(timeBuf,"%.1f", graphSettings.timeRange);
  btnGraphTimeRange.x=150;
  btnGraphTimeRange.y=yPos-15;
  drawButton(btnGraphTimeRange, COLOR_YELLOW, COLOR_BLACK, timeBuf, false,true);

  // Display button in bottom-right
  btnGraphDisplay.x= SCREEN_WIDTH -100;
  btnGraphDisplay.y= SCREEN_HEIGHT -45;
  drawButton(btnGraphDisplay, COLOR_YELLOW, COLOR_BLACK, "Display", false,true);
}

/**
 * @brief Device-specific Graph Settings
 */
void drawDeviceGraphSettings(int deviceIndex) {
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(COLOR_WHITE);

  int yPos=50;

  // Data Type
  tft.setCursor(20, yPos);
  tft.print("Data Type:");
  yPos+=25;

  const char* dtLabels[3]={"Current","Voltage","Power"};
  for(int i=0;i<3;i++){
    uint16_t col= (graphSettings.devices[deviceIndex].dataType == i)? COLOR_GREEN: COLOR_GRAY;
    tft.fillRect(30 + i*120,yPos,110,30, col);
    tft.drawRect(30 + i*120,yPos,110,30, COLOR_WHITE);
    tft.setCursor(35 + i*120, yPos+20);
    tft.print(dtLabels[i]);
  }
  yPos+=45;

  // Line color
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(20, yPos);
  tft.print("Line Color:");
  yPos+=25;
  for(int i=0;i<8;i++){
    int colorX=30 + i*50;
    int colorY=yPos;
    uint16_t col= DEFAULT_GRAPH_COLORS[i];
    tft.fillRect(colorX, colorY,35,25, col);
    tft.drawRect(colorX, colorY,35,25, COLOR_WHITE);
    if(col == graphSettings.devices[deviceIndex].lineColor){
      tft.drawRect(colorX-2, colorY-2,39,29, COLOR_WHITE);
    }
  }
  yPos+=40;

  // Y-Axis range
  GraphDataType dt= graphSettings.devices[deviceIndex].dataType;
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(20, yPos);
  tft.print("Y-Axis Range:");
  yPos+=20;

  tft.setCursor(20,yPos);
  tft.print("Min:");
  float minVal= graphSettings.devices[deviceIndex].axisRanges[dt][0];
  char minBuf[16];
  sprintf(minBuf,"%.2f", minVal);
  btnGraphMinY.x=60;
  btnGraphMinY.y=yPos-15;
  drawButton(btnGraphMinY, COLOR_YELLOW, COLOR_BLACK, minBuf, false,true);

  tft.setCursor(160,yPos);
  tft.print("Max:");
  float maxVal= graphSettings.devices[deviceIndex].axisRanges[dt][1];
  char maxBuf[16];
  sprintf(maxBuf,"%.2f", maxVal);
  btnGraphMaxY.x=200;
  btnGraphMaxY.y=yPos-15;
  drawButton(btnGraphMaxY, COLOR_YELLOW, COLOR_BLACK, maxBuf, false,true);
  yPos+=35;

  // Auto scale
  tft.setTextColor(COLOR_WHITE);
  tft.setCursor(20,yPos);
  tft.print("Auto Scale:");
  tft.drawRect(120,yPos-15,25,25, COLOR_WHITE);
  if(graphSettings.autoFitEnabled){
    tft.fillRect(123,yPos-12,19,19, COLOR_GREEN);
  }

  // Display button
  btnGraphDisplay.x= SCREEN_WIDTH -100;
  btnGraphDisplay.y= SCREEN_HEIGHT -45;
  drawButton(btnGraphDisplay, COLOR_YELLOW, COLOR_BLACK, "Display", false,true);
}

// Switch tabs
void switchGraphTab(GraphTab newTab) {
  guiState.currentGraphTab= newTab;
  bufferNeedsRegeneration=true;
  if(guiState.currentMode== MODE_GRAPH){
    drawGraphPage();
  }
}

// Cycle data type in all devices
void cycleAllGraphDataType() {
  GraphDataType newType= (GraphDataType)((graphSettings.all.dataType+1)%3);
  graphSettings.all.dataType= newType;
  for(int i=0;i<6;i++){
    graphSettings.devices[i].dataType= newType;
  }
  bufferNeedsRegeneration=true;
  graphState.axesNeedUpdate=true;
  saveGraphSettings();
  if(guiState.currentMode== MODE_GRAPH) {
    drawGraphPage();
  }
}

// Toggling device on/off in the "All" tab
void toggleDeviceInAll(int deviceIndex) {
  if(deviceIndex>=0 && deviceIndex<6){
    graphSettings.all.deviceEnabled[deviceIndex]= !graphSettings.all.deviceEnabled[deviceIndex];
    bufferNeedsRegeneration=true;
    saveGraphSettings();
  }
}

// Set axis bounds for entire tab or device
void setGraphAxisBounds(GraphTab tab, GraphDataType dataType, float minY, float maxY) {
  if(maxY<= minY) maxY= minY+1.0f;

  if(tab== GRAPH_TAB_ALL){
    graphSettings.all.axisRanges[dataType][0]= minY;
    graphSettings.all.axisRanges[dataType][1]= maxY;
  } else {
    int d= (int)tab -1;
    graphSettings.devices[d].axisRanges[dataType][0]= minY;
    graphSettings.devices[d].axisRanges[dataType][1]= maxY;
  }
  bufferNeedsRegeneration=true;
  graphState.axesNeedUpdate=true;
  saveGraphSettings();
}

void setDeviceGraphColor(int deviceIndex, uint16_t color) {
  if(deviceIndex>=0 && deviceIndex<6){
    graphSettings.devices[deviceIndex].lineColor= color;
    bufferNeedsRegeneration=true;
    saveGraphSettings();
  }
}

// Toggling data type for a single device
void toggleDeviceGraphDataType(int deviceIndex, GraphDataType dataType) {
  if(deviceIndex>=0 && deviceIndex<6){
    graphSettings.devices[deviceIndex].dataType= dataType;
    graphSettings.all.dataType= dataType;
    for(int i=0;i<6;i++){
      if(i!= deviceIndex) graphSettings.devices[i].dataType= dataType;
    }
    bufferNeedsRegeneration=true;
    graphState.axesNeedUpdate= true;
    saveGraphSettings();
  }
}

const char* getGraphDataTypeName(GraphDataType dataType) {
  switch(dataType){
    case GRAPH_CURRENT: return "Current";
    case GRAPH_VOLTAGE: return "Voltage";
    case GRAPH_POWER:   return "Power";
    default: return "Unknown";
  }
}
const char* getGraphDataTypeUnit(GraphDataType dataType) {
  switch(dataType){
    case GRAPH_CURRENT: return "A";
    case GRAPH_VOLTAGE: return "V";
    case GRAPH_POWER:   return "W";
    default: return "";
  }
}

void applyGraphRefreshRate(unsigned long rate){
  graphSettings.graphRefreshRate= constrain((int)rate,20,200);
  saveGraphSettings();
}

void toggleAntialiasing(bool enable) {
  graphSettings.enableAntialiasing= enable;
  saveGraphSettings();
}

void toggleGrids(bool show) {
  graphSettings.showGrids= show;
  bufferNeedsRegeneration=true;
  saveGraphSettings();
}

void setEffectiveMaxPoints(int points) {
  // Constrain user input within allowed range
  graphSettings.effectiveMaxPoints = constrain(points, 10, GRAPH_MAX_POINTS);

  // Clear all existing data so the circular buffer sizes are effectively reset
  clearGraphData();

  // Save the updated max-points setting to EEPROM
  saveGraphSettings();
}