/**
 * @file graphs.cpp
 * @brief Graph system implementation with smooth updates, diagonal lines in real-time,
 *        data persistence, and full regeneration on time-range/Y-axis changes.
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

#include "graphs.h"
#include "config.h"
#include "display.h"
#include "sensors.h"
#include <algorithm>  // For std::swap
#include <cmath>      // For round, floor, fabs
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
extern bool isScriptPaused;
extern long scriptTimeSeconds;
extern Script currentScript;
extern unsigned long scriptStartMillis;
extern unsigned long scriptPausedTime;
extern unsigned long pauseStartMillis;

static float scriptEndTime = 0.0f;
static bool useScriptTimeline = false;

// Additional UI buttons
ButtonRegion btnGraphDisplay = {380, SCREEN_HEIGHT - 70, 80, 30, "Display", false, COLOR_YELLOW, true};
ButtonRegion btnGraphDisplayBack = {5, 5, 80, 35, "Back", false, COLOR_YELLOW, true};
ButtonRegion btnGraphDataTypeFooter = {265, SCREEN_HEIGHT - 40, 67, 35, "Current", false, COLOR_RED, true};

// buttons for interpolation column

ButtonRegion btnGraphInterpolateToggle = {250, 60, 25, 25, "Interpolate", false, COLOR_WHITE, true}; // Toggle
ButtonRegion btnGraphTensionInput = {250, 100, 80, 25, "0.00", false, COLOR_YELLOW, true}; // Tension input
ButtonRegion btnGraphCurveScaleInput = {250, 140, 80, 25, "2.00", false, COLOR_YELLOW, true}; // Curve scale input
ButtonRegion btnGraphSubdivInput = {250, 180, 80, 25, "32", false, COLOR_YELLOW, true}; // Subdiv input

// Globals
GraphSettings graphSettings;
DMAMEM SimpleGraphData deviceGraphData[6][3];  // Move to RAM2 to free up 126.7KB of RAM1
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
static void drawBresenhamLineInBuffer(int x0, int y0, int x1, int y1, uint16_t color, int thickness);
static void drawAntialiasedLineInBuffer(int x0, int y0, int x1, int y1, uint16_t color, int thickness);
void drawCatmullRomSplineMulti(const int (*pts)[2], int count, uint16_t color, int thickness, bool antialiased);
static uint16_t blendColors(uint16_t fg, uint16_t bg, float alpha);



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

  // ADDED: Memory protection - ensure graph buffer is valid
  if (GRAPH_AREA_WIDTH * GRAPH_AREA_HEIGHT > 0) {
    tft.setSPISpeed(30000000);
    graphState.needsFullRedraw = true;
    bufferNeedsRegeneration = true;
    Serial.println("Graphs initialized");

    // Fill the buffer with background color
    for (int i = 0; i < GRAPH_AREA_WIDTH * GRAPH_AREA_HEIGHT; i++) {
      graphBuffer[i] = GRAPH_BG_COLOR;
    }
  } else {
    Serial.println("ERROR: Invalid graph buffer dimensions");
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
    // Use more granular time calculation for scripts to maintain high-frequency data collection
    unsigned long totalPausedTime = scriptPausedTime;
    if (isScriptPaused) {
      totalPausedTime += (currentMillis - pauseStartMillis);
    }
    unsigned long msSinceStart = currentMillis - scriptStartMillis - totalPausedTime;
    currentTime = currentScript.tStart + (msSinceStart / 1000.0f);
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
  // Don't save pause state to EEPROM - pause state should not persist across reboots
}

void resumeGraphData() {
  graphSettings.isPaused = false;
  bufferNeedsRegeneration = true;
  // Don't save pause state to EEPROM - pause state should not persist across reboots
}

// Save and Load Graph Settings from EEPROM
void saveGraphSettings() {
  // Compute simple checksum (e.g., sum of all bytes except checksum)
  uint32_t sum = 0;
  uint8_t* bytes = (uint8_t*)&graphSettings;
  for (size_t i = 0; i < sizeof(GraphSettings) - sizeof(uint32_t); i++) {
    sum += bytes[i];
  }
  graphSettings.checksum = sum;
  EEPROM.put(EEPROM_GRAPH_SETTINGS_ADDR, graphSettings);
}

/**
 * @brief Draws a smooth, clamped Hermite spline through an array of data points,
 *        rounding peaks without overshooting local max/min.
 *
 * @param pts      Array of (x,y) integer pairs.
 * @param count    Number of points in pts[].
 * @param color    16-bit RGB565 color.
 * @param thickness   Used by Bresenham/AA line functions.
 * @param antialiased Whether to use antialiased or standard lines.
 */
void drawCatmullRomSplineMulti(const int (*pts)[2], int count,
                               uint16_t color, int thickness, bool antialiased) {
  if (count < 2) return;  // Not enough points

  // Tension: 0.0 = tight, 1.0 = loose
  const double tension = 1.0;
  // Curve scale: >1.0 for "larger" curves (wider rounding on peaks)
  const double curveScale = 3.0;  // Adjust: 1.5-3.0 for more exaggeration
  // Subdivisions per segment
  const int subdiv = 32;  // High for smoothness

  // Loop over segments
  for (int i = 0; i < count - 1; i++) {
    int p1x = pts[i][0];
    int p1y = pts[i][1];
    int p2x = pts[i+1][0];
    int p2y = pts[i+1][1];

    // Estimate tangents at p1 and p2
    double tangent1x = (i > 0) ? (double)(p2x - pts[i-1][0]) / 2.0 : (double)(p2x - p1x);
    double tangent1y = (i > 0) ? (double)(p2y - pts[i-1][1]) / 2.0 : (double)(p2y - p1y);
    double tangent2x = (i + 2 < count) ? (double)(pts[i+2][0] - p1x) / 2.0 : (double)(p2x - p1x);
    double tangent2y = (i + 2 < count) ? (double)(pts[i+2][1] - p1y) / 2.0 : (double)(p2y - p1y);

    // Apply tension and scale for larger curves
    tangent1x *= tension * curveScale;
    tangent1y *= tension * curveScale;
    tangent2x *= tension * curveScale;
    tangent2y *= tension * curveScale;

    // Clamp tangents to prevent overshoot
    double localMinY = min((double)p1y, (double)p2y) - 1.0;
    double localMaxY = max((double)p1y, (double)p2y) + 1.0;
    tangent1y = constrain(tangent1y, localMinY - p1y, localMaxY - p1y);
    tangent2y = constrain(tangent2y, localMinY - p2y, localMaxY - p2y);

    int lastX = p1x, lastY = p1y;  // Start from p1

    for (int s = 1; s <= subdiv; s++) {
      double t = (double)s / (double)subdiv;
      double h00 = 2 * t * t * t - 3 * t * t + 1;          // Basis for p1
      double h10 = t * t * t - 2 * t * t + t;              // Basis for tangent1
      double h01 = -2 * t * t * t + 3 * t * t;             // Basis for p2
      double h11 = t * t * t - t * t;                      // Basis for tangent2

      double x_ = h00 * (double)p1x + h10 * tangent1x + h01 * (double)p2x + h11 * tangent2x;
      double y_ = h00 * (double)p1y + h10 * tangent1y + h01 * (double)p2y + h11 * tangent2y;

      int curX = constrain((int)lround(x_), 0, GRAPH_AREA_WIDTH - 1);
      int curY = constrain((int)lround(y_), 0, GRAPH_AREA_HEIGHT - 1);

      // Draw segment from last to current
      if (antialiased) {
        drawAntialiasedLineInBuffer(lastX, lastY, curX, curY, color, thickness);
      } else {
        drawBresenhamLineInBuffer(lastX, lastY, curX, curY, color, thickness);
      }

      lastX = curX;
      lastY = curY;
    }
  }

  // Optional Gaussian filter for extra smoothing on small peaks (if enabled)
  if (graphSettings.enableGaussianFilter) {
    // Simple 3-point Gaussian average on y-values
    for (int x = 0; x < GRAPH_AREA_WIDTH; x++) {
      for (int y = 1; y < GRAPH_AREA_HEIGHT - 1; y++) {
        if (graphBuffer[y * GRAPH_AREA_WIDTH + x] == color) {  // Only blur drawn lines
          uint16_t above = graphBuffer[(y-1) * GRAPH_AREA_WIDTH + x];
          uint16_t below = graphBuffer[(y+1) * GRAPH_AREA_WIDTH + x];
          // Average with weights (Gaussian approx: 0.25, 0.5, 0.25)
          graphBuffer[y * GRAPH_AREA_WIDTH + x] = blendColors(blendColors(above, graphBuffer[y * GRAPH_AREA_WIDTH + x], 0.25f), below, 0.25f);
        }
      }
    }
  }
}

// Draw smoothed (Catmull-Rom spline interpolated) line between points for rounding/smoothing
/**
 * @brief Draw a Catmull-Rom spline between two data points (prevX,prevY) and (px,py).
 * Creates a smooth, curved line for "rounding" graph segments.
 *
 * @param prevX, prevY  Start coordinate
 * @param px, py        End coordinate
 * @param color         16-bit RGB565 color
 * @param thickness     Currently used by drawBresenhamLineInBuffer(...) or drawAntialiasedLineInBuffer(...)
 * @param antialiased   Whether to call the antialiased or standard line drawer
 */
static void drawInterpolatedLineInBuffer(int prevX, int prevY,
                                         int px, int py,
                                         uint16_t color,
                                         int thickness,
                                         bool antialiased)
{
    // Subdivisions = how many short segments per line
    const int subdivisions = 20;
    // "tension" typically 0.0-1.0; 0.5 recommended for Catmull-Rom
    const double tension = 0.3;

    // If there's no horizontal/vertical distance, just draw a single line
    if (prevX == px && prevY == py) {
        // The line is degenerate (no length). Draw a single pixel to represent it.
        if (antialiased) {
            drawAntialiasedLineInBuffer(prevX, prevY, prevX, prevY, color, thickness);
        } else {
            drawBresenhamLineInBuffer(prevX, prevY, prevX, prevY, color, thickness);
        }
        return;
    }

    // We treat [prevX, prevY] and [px, py] as P1 and P2 in Catmull-Rom.
    // P0 & P3 are extrapolated beyond them for smoothness.
    int dx = px - prevX;
    int dy = py - prevY;

    // Extrapolate P0 as "one segment behind" the start
    int p0x = prevX - dx;
    int p0y = prevY - dy;
    // Extrapolate P3 as "one segment beyond" the end
    int p3x = px + dx;
    int p3y = py + dy;

    // Loop over subdivisions to create small segments
    for (int s = 0; s < subdivisions; s++) {
        double t1 = static_cast<double>(s) / static_cast<double>(subdivisions);
        double t2 = static_cast<double>(s + 1) / static_cast<double>(subdivisions);

        // Evaluate Catmull-Rom for t1, t2 between P1 & P2
        // Using standard formula:
        //   P(t) = 0.5 * ( (2 * P1) + (-P0 + P2) * t + (2P0 - 5P1 + 4P2 - P3)*t^2 + (-P0 + 3P1 - 3P2 + P3)*t^3 )
        // We swap out tension if we want a "general" cardinal spline,
        // but standard Catmull-Rom sets tension=0.5 inside the formula.
        // We're constructing the formula manually:

        // Convert tension into the standard "s" factor used in cardinal splines
        // For pure Catmull-Rom (s=0.5), we can code the cardinal coefficients directly:
        auto catmullRomX = [&](double t) -> double {
            double t2 = t * t;
            double t3 = t2 * t;
            // Catmull-Rom matrix parts:
            double c1 = 2.0 * static_cast<double>(prevX);
            double c2 = static_cast<double>(px) - static_cast<double>(p0x);
            double c3 = 2.0 * static_cast<double>(p0x) - 5.0 * static_cast<double>(prevX) + 4.0 * static_cast<double>(px) - static_cast<double>(p3x);
            double c4 = -1.0 * static_cast<double>(p0x) + 3.0 * static_cast<double>(prevX) - 3.0 * static_cast<double>(px) + static_cast<double>(p3x);
            return 0.5 * (c1 + c2 * t + c3 * t2 + c4 * t3);
        };

        auto catmullRomY = [&](double t) -> double {
            double t2 = t * t;
            double t3 = t2 * t;
            double c1 = 2.0 * static_cast<double>(prevY);
            double c2 = static_cast<double>(py) - static_cast<double>(p0y);
            double c3 = 2.0 * static_cast<double>(p0y) - 5.0 * static_cast<double>(prevY) + 4.0 * static_cast<double>(py) - static_cast<double>(p3y);
            double c4 = -1.0 * static_cast<double>(p0y) + 3.0 * static_cast<double>(prevY) - 3.0 * static_cast<double>(py) + static_cast<double>(p3y);
            return 0.5 * (c1 + c2 * t + c3 * t2 + c4 * t3);
        };

        double xStart = catmullRomX(t1);
        double yStart = catmullRomY(t1);
        double xEnd   = catmullRomX(t2);
        double yEnd   = catmullRomY(t2);

        // Round to int and clamp in final stage
        int ix0 = constrain((int)lround(xStart), 0, GRAPH_AREA_WIDTH - 1);
        int iy0 = constrain((int)lround(yStart), 0, GRAPH_AREA_HEIGHT - 1);
        int ix1 = constrain((int)lround(xEnd),   0, GRAPH_AREA_WIDTH - 1);
        int iy1 = constrain((int)lround(yEnd),   0, GRAPH_AREA_HEIGHT - 1);

        // Draw the segment
        if (antialiased) {
            drawAntialiasedLineInBuffer(ix0, iy0, ix1, iy1, color, thickness);
        } else {
            drawBresenhamLineInBuffer(ix0, iy0, ix1, iy1, color, thickness);
        }
    }

    // Optionally ensure final pixel is marked
    int finalX = constrain(px, 0, GRAPH_AREA_WIDTH - 1);
    int finalY = constrain(py, 0, GRAPH_AREA_HEIGHT - 1);
    graphBuffer[finalY * GRAPH_AREA_WIDTH + finalX] = color;
}

/**
 * @brief Loads graph settings from EEPROM, applying defaults if necessary.
 * Validates loaded settings and applies defaults if corrupted.
 */
void loadGraphSettings() {
  GraphSettings def;
  // Setup defaults
  for (int i = 0; i < 6; i++) {
    def.devices[i].enabled = true;
    def.devices[i].dataType = GRAPH_CURRENT;
    def.devices[i].lineColor = DEFAULT_GRAPH_COLORS[i];
    def.devices[i].autoScale = true;
    
    // Set default axis ranges based on device type
    if (i == 0 || i == 1) {
      // GSE-1 and GSE-2: Current max = 2A
      def.devices[i].axisRanges[GRAPH_CURRENT][0] = -0.01f;
      def.devices[i].axisRanges[GRAPH_CURRENT][1] = 2.0f;
    } else {
      // TE-R, TE-1, TE-2, TE-3: Current max = 3.6A
      def.devices[i].axisRanges[GRAPH_CURRENT][0] = -0.01f;
      def.devices[i].axisRanges[GRAPH_CURRENT][1] = 3.6f;
    }
    
    // Voltage and Power ranges for all devices
    def.devices[i].axisRanges[GRAPH_VOLTAGE][0] = -3.0f;
    def.devices[i].axisRanges[GRAPH_VOLTAGE][1] = 34.0f;
    def.devices[i].axisRanges[GRAPH_POWER][0] = -1.0f;
    def.devices[i].axisRanges[GRAPH_POWER][1] = 50.0f;
  }
  def.all.dataType = GRAPH_CURRENT;
  for (int i = 0; i < 6; i++) def.all.deviceEnabled[i] = true;
  def.all.autoScale = true;
  def.all.lineThickness = 1;
  def.all.axisRanges[GRAPH_CURRENT][0] = -0.01f;
  def.all.axisRanges[GRAPH_CURRENT][1] = 3.6f;
  def.all.axisRanges[GRAPH_VOLTAGE][0] = -3.0f;
  def.all.axisRanges[GRAPH_VOLTAGE][1] = 34.0f;
  def.all.axisRanges[GRAPH_POWER][0] = -1.0f;
  def.all.axisRanges[GRAPH_POWER][1] = 50.0f;
  def.isPaused = false;
  def.autoScroll = true;
  def.showAxesLabels = true;
  def.timeRange = 30.0f;
  def.panOffsetX = 0.0f;
  def.panOffsetY = 0.0f;
  def.enablePanning = false;
  def.autoFitEnabled = true;
  def.effectiveMaxPoints = GRAPH_MAX_POINTS;
  def.graphRefreshRate = GRAPH_UPDATE_INTERVAL;
  def.enableAntialiasing = true;
  def.enableInterpolation = false;
  def.interpolationSmoothness = 1.0f;
  def.enableGaussianFilter = false;
  def.interpolationTension = 0.0f;
  def.interpolationCurveScale = 2.0f;
  def.interpolationSubdiv = 32;

  def.showGrids = true;
  def.pausedMinTime = 0.0f;
  def.pausedMaxTime = 0.0f;

  GraphSettings readSettings;
  EEPROM.get(EEPROM_GRAPH_SETTINGS_ADDR, readSettings);
  bool valid = (readSettings.timeRange >= 0.01f && readSettings.timeRange <= 300.0f);

  uint32_t sum = 0;
  uint8_t* bytes = (uint8_t*)&readSettings;
  for (size_t i = 0; i < sizeof(GraphSettings) - sizeof(uint32_t); i++) {
    sum += bytes[i];
  }
  if (readSettings.checksum != sum) {
    valid = false;  // Corrupted
  }

  if (readSettings.effectiveMaxPoints < 10 || readSettings.effectiveMaxPoints > GRAPH_MAX_POINTS) valid = false;
  if (readSettings.graphRefreshRate < 20 || readSettings.graphRefreshRate > 500) valid = false;
  if (readSettings.interpolationTension < 0.0f || readSettings.interpolationTension > 1.0f) valid = false;
  if (readSettings.interpolationCurveScale < 1.0f || readSettings.interpolationCurveScale > 9.0f) valid = false;
  if (readSettings.interpolationSubdiv < 8 || readSettings.interpolationSubdiv > 64) valid = false;


  for (int i = 0; i < 6 && valid; i++) {
    // Existing axis validation...
    for (int dt = 0; dt < 3; dt++) {
      float minY = readSettings.devices[i].axisRanges[dt][0];
      float maxY = readSettings.devices[i].axisRanges[dt][1];
      if (isnan(minY) || isnan(maxY) || isinf(minY) || isinf(maxY) || maxY <= minY) {
        valid = false;
        break;
      }
    }
    // NEW: Validate lineColor (clamp to 0x0000-0xFFFF)
    if (readSettings.devices[i].lineColor > 0xFFFF) {
      valid = false;  // Or set to default without invalidating all
    }
  }

  // Existing all.axisRanges validation...

  if (!valid) {
    graphSettings = def;
    saveGraphSettings();
    Serial.println("Graph settings invalid or corrupted, loaded defaults.");
  } else {
    for (int i = 0; i < 6; i++) {
      graphSettings.devices[i].lineColor &= 0xFFFF;  // Mask to valid 16-bit
    }
    graphSettings.interpolationSmoothness = constrain(graphSettings.interpolationSmoothness, 0.0f, 1.0f);
    readSettings.interpolationTension = constrain(readSettings.interpolationTension, 0.0f, 1.0f);
    readSettings.interpolationCurveScale = constrain(readSettings.interpolationCurveScale, 1.0f, 9.0f);
    readSettings.interpolationSubdiv = constrain(readSettings.interpolationSubdiv, 8, 64);
    Serial.println("Graph settings loaded from EEPROM.");
    graphSettings = readSettings;
  }
  
  // Always start with graphs unpaused, regardless of what was saved
  graphSettings.isPaused = false;
}

void resetGraphSettings() {
  // Create default settings
  GraphSettings def;
  
  // Setup defaults
  for (int i = 0; i < 6; i++) {
    def.devices[i].enabled = true;
    def.devices[i].dataType = GRAPH_CURRENT;
    def.devices[i].lineColor = DEFAULT_GRAPH_COLORS[i];
    def.devices[i].autoScale = true;
    
    // Set default axis ranges based on device type
    if (i == 0 || i == 1) {
      // GSE-1 and GSE-2: Current max = 2A
      def.devices[i].axisRanges[GRAPH_CURRENT][0] = -0.01f;
      def.devices[i].axisRanges[GRAPH_CURRENT][1] = 2.0f;
    } else {
      // TE-R, TE-1, TE-2, TE-3: Current max = 3.6A
      def.devices[i].axisRanges[GRAPH_CURRENT][0] = -0.01f;
      def.devices[i].axisRanges[GRAPH_CURRENT][1] = 3.6f;
    }
    
    // Voltage and Power ranges for all devices
    def.devices[i].axisRanges[GRAPH_VOLTAGE][0] = -3.0f;
    def.devices[i].axisRanges[GRAPH_VOLTAGE][1] = 34.0f;
    def.devices[i].axisRanges[GRAPH_POWER][0] = -1.0f;
    def.devices[i].axisRanges[GRAPH_POWER][1] = 50.0f;
  }
  
  def.all.dataType = GRAPH_CURRENT;
  for (int i = 0; i < 6; i++) def.all.deviceEnabled[i] = true;
  def.all.autoScale = true;
  def.all.lineThickness = 1;
  def.all.axisRanges[GRAPH_CURRENT][0] = -0.01f;
  def.all.axisRanges[GRAPH_CURRENT][1] = 3.6f;
  def.all.axisRanges[GRAPH_VOLTAGE][0] = -3.0f;
  def.all.axisRanges[GRAPH_VOLTAGE][1] = 34.0f;
  def.all.axisRanges[GRAPH_POWER][0] = -1.0f;
  def.all.axisRanges[GRAPH_POWER][1] = 50.0f;
  
  def.isPaused = false;
  def.autoScroll = true;
  def.showAxesLabels = true;
  def.timeRange = 30.0f;
  def.panOffsetX = 0.0f;
  def.panOffsetY = 0.0f;
  def.enablePanning = false;
  def.autoFitEnabled = true;
  def.effectiveMaxPoints = GRAPH_MAX_POINTS;
  def.graphRefreshRate = GRAPH_UPDATE_INTERVAL;
  def.enableAntialiasing = true;
  def.enableInterpolation = false;
  def.interpolationSmoothness = 1.0f;
  def.enableGaussianFilter = false;
  def.interpolationTension = 0.0f;
  def.interpolationCurveScale = 2.0f;
  def.interpolationSubdiv = 32;
  def.showGrids = true;
  def.pausedMinTime = 0.0f;
  def.pausedMaxTime = 0.0f;
  
  // Apply the default settings
  graphSettings = def;
  
  // Save to EEPROM
  saveGraphSettings();
  
  // Force regeneration of graph data
  bufferNeedsRegeneration = true;
  graphState.axesNeedUpdate = true;
  
  Serial.println("Graph settings reset to defaults.");
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
  
  // Calculate more precise timing for graph display
  unsigned long totalPausedTime = scriptPausedTime;
  if (isScriptPaused) {
    totalPausedTime += (millis() - pauseStartMillis);
  }
  unsigned long msSinceStart = millis() - scriptStartMillis - totalPausedTime;
  long preciseTime = currentScript.tStart + (long)((msSinceStart + 500) / 1000); // Round to nearest second
  
  if (preciseTime < 0) {
    sprintf(timeBuf, "T%ld", preciseTime);
  } else {
    sprintf(timeBuf, "T+%ld", preciseTime);
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
  int yPos = 60;  // Starting y for top item
  int spacing = 30;  // Compact vertical spacing (reduced from 40)

  // Existing items (left column, compact)
  // Antialiasing toggle
  tft.setCursor(20, yPos);
  tft.print("Antialiasing:");
  tft.drawRect(180, yPos - 15, 25, 25, COLOR_WHITE);
  if (graphSettings.enableAntialiasing) {
    tft.fillRect(183, yPos - 12, 19, 19, COLOR_GREEN);
  }
  yPos += spacing;

  // Show Grids toggle
  tft.setCursor(20, yPos);
  tft.print("Show Grids:");
  tft.drawRect(180, yPos - 15, 25, 25, COLOR_WHITE);
  if (graphSettings.showGrids) {
    tft.fillRect(183, yPos - 12, 19, 19, COLOR_GREEN);
  }
  yPos += spacing;

  // Max Points setting
  tft.setCursor(20, yPos);
  tft.print("Max Points:");
  char pointsBuf[16];
  sprintf(pointsBuf, "%d", graphSettings.effectiveMaxPoints);
  tft.drawRect(180, yPos - 15, 80, 25, COLOR_YELLOW);
  tft.setCursor(185, yPos);
  tft.print(pointsBuf);
  yPos += spacing;

  // Refresh Rate setting
  tft.setCursor(20, yPos);
  tft.print("Refresh Rate (ms):");
  char refreshBuf[16];
  sprintf(refreshBuf, "%lu", graphSettings.graphRefreshRate);
  tft.drawRect(180, yPos - 15, 80, 25, COLOR_YELLOW);
  tft.setCursor(185, yPos);
  tft.print(refreshBuf);
  yPos += spacing;

  // NEW: Interpolation items (continued in same column, compact)
  // Interpolate Data toggle
  tft.setCursor(20, yPos);
  tft.print("Interpolate Data:");
  tft.drawRect(180, yPos - 15, 25, 25, COLOR_WHITE);
  if (graphSettings.enableInterpolation) {
    tft.fillRect(183, yPos - 12, 19, 19, COLOR_GREEN);
  }
  yPos += spacing;

  // Tension input
  tft.setCursor(20, yPos);
  tft.print("Tension:");
  char tensionBuf[16];
  sprintf(tensionBuf, "%.2f", graphSettings.interpolationTension);
  tft.drawRect(180, yPos - 15, 80, 25, COLOR_YELLOW);
  tft.setCursor(185, yPos);
  tft.print(tensionBuf);
  yPos += spacing;

  // Curve Scale input
  tft.setCursor(20, yPos);
  tft.print("Curve Scale:");
  char curveScaleBuf[16];
  sprintf(curveScaleBuf, "%.2f", graphSettings.interpolationCurveScale);
  tft.drawRect(180, yPos - 15, 80, 25, COLOR_YELLOW);
  tft.setCursor(185, yPos);
  tft.print(curveScaleBuf);
  yPos += spacing;

  // Subdiv input
  tft.setCursor(20, yPos);
  tft.print("Subdiv:");
  char subdivBuf[16];
  sprintf(subdivBuf, "%d", graphSettings.interpolationSubdiv);
  tft.drawRect(180, yPos - 15, 80, 25, COLOR_YELLOW);
  tft.setCursor(185, yPos);
  tft.print(subdivBuf);
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
      if (range < 0.001f) range = 1.0f;  // Existing
      minY = dataMin - range * 0.1f;
      maxY = dataMax + range * 0.1f;
    } else {
      minY = 0.0f;
      maxY = 1.0f;
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

            // Get py1 from buffer's second-last column (average color position or approx)
            int py1Approx = (GRAPH_AREA_HEIGHT - 1) / 2;  // Fallback midpoint; improve if needed
            int idx2 = (gd->writeIndex - 1 + graphSettings.effectiveMaxPoints) % graphSettings.effectiveMaxPoints;
            int idx1 = (gd->writeIndex - 2 + graphSettings.effectiveMaxPoints) % graphSettings.effectiveMaxPoints;
            float val1 = gd->dataPoints[idx1];
            float val2 = gd->dataPoints[idx2];

            float yNorm1 = (val1 - minY) / (maxY - minY);
            float yNorm2 = (val2 - minY) / (maxY - minY);
            yNorm1 = constrain(yNorm1, 0.0f, 1.0f);
            yNorm2 = constrain(yNorm2, 0.0f, 1.0f);

            int py2 = (GRAPH_AREA_HEIGHT - 1) - static_cast<int>(yNorm2 * (GRAPH_AREA_HEIGHT - 1));
            int px1 = GRAPH_AREA_WIDTH - 2;
            int px2 = GRAPH_AREA_WIDTH - 1;

            // For partial update, collect last 4 points if possible for multi-point spline
            if (gd->count >= 4 && graphSettings.enableInterpolation) {
              int idx4 = (gd->writeIndex - 1 + graphSettings.effectiveMaxPoints) % graphSettings.effectiveMaxPoints;
              int idx3 = (idx4 - 1 + graphSettings.effectiveMaxPoints) % graphSettings.effectiveMaxPoints;
              int idx2 = (idx3 - 1 + graphSettings.effectiveMaxPoints) % graphSettings.effectiveMaxPoints;
              int idx1 = (idx2 - 1 + graphSettings.effectiveMaxPoints) % graphSettings.effectiveMaxPoints;

              float val4 = gd->dataPoints[idx4];
              float val3 = gd->dataPoints[idx3];
              float val2 = gd->dataPoints[idx2];
              float val1 = gd->dataPoints[idx1];

              float yNorm4 = (val4 - minY) / (maxY - minY);
              float yNorm3 = (val3 - minY) / (maxY - minY);
              float yNorm2 = (val2 - minY) / (maxY - minY);
              float yNorm1 = (val1 - minY) / (maxY - minY);
              yNorm4 = constrain(yNorm4, 0.0f, 1.0f);
              yNorm3 = constrain(yNorm3, 0.0f, 1.0f);
              yNorm2 = constrain(yNorm2, 0.0f, 1.0f);
              yNorm1 = constrain(yNorm1, 0.0f, 1.0f);

              int py4 = (GRAPH_AREA_HEIGHT - 1) - static_cast<int>(yNorm4 * (GRAPH_AREA_HEIGHT - 1));
              int py3 = (GRAPH_AREA_HEIGHT - 1) - static_cast<int>(yNorm3 * (GRAPH_AREA_HEIGHT - 1));
              int py2 = (GRAPH_AREA_HEIGHT - 1) - static_cast<int>(yNorm2 * (GRAPH_AREA_HEIGHT - 1));
              int py1 = (GRAPH_AREA_HEIGHT - 1) - static_cast<int>(yNorm1 * (GRAPH_AREA_HEIGHT - 1));

              int pts[4][2] = {
                {GRAPH_AREA_WIDTH - 3, py1},
                {GRAPH_AREA_WIDTH - 2, py2},
                {GRAPH_AREA_WIDTH - 1, py3},
                {GRAPH_AREA_WIDTH, py4}  // Slight extrapolation for end smoothing
              };

              drawCatmullRomSplineMulti(pts, 4, graphSettings.devices[dev].lineColor, graphSettings.all.lineThickness, graphSettings.enableAntialiasing);
            } else {
              drawInterpolatedLineInBuffer(px1, py1Approx, px2, py2, graphSettings.devices[dev].lineColor, graphSettings.all.lineThickness, graphSettings.enableAntialiasing);
            }
          }
        } else {
          // Single-device partial update - adapt similarly
          int dIdx = (int)guiState.currentGraphTab - 1;
          SimpleGraphData* gd = &deviceGraphData[dIdx][dt];
          if (gd->count >= 2) {
            // Get py1 from buffer's second-last column (average color position or approx)
            int py1Approx = (GRAPH_AREA_HEIGHT - 1) / 2;  // Fallback midpoint; improve if needed

            int idx2 = (gd->writeIndex - 1 + graphSettings.effectiveMaxPoints) % graphSettings.effectiveMaxPoints;
            int idx1 = (gd->writeIndex - 2 + graphSettings.effectiveMaxPoints) % graphSettings.effectiveMaxPoints;
            float val1 = gd->dataPoints[idx1];
            float val2 = gd->dataPoints[idx2];

            float yNorm1 = (val1 - minY) / (maxY - minY);
            float yNorm2 = (val2 - minY) / (maxY - minY);
            yNorm1 = constrain(yNorm1, 0.0f, 1.0f);
            yNorm2 = constrain(yNorm2, 0.0f, 1.0f);

            int py2 = (GRAPH_AREA_HEIGHT - 1) - static_cast<int>(yNorm2 * (GRAPH_AREA_HEIGHT - 1));
            int px1 = GRAPH_AREA_WIDTH - 2;
            int px2 = GRAPH_AREA_WIDTH - 1;

            // For partial update, collect last 4 points if possible for multi-point spline
            if (gd->count >= 4 && graphSettings.enableInterpolation) {
              int idx4 = (gd->writeIndex - 1 + graphSettings.effectiveMaxPoints) % graphSettings.effectiveMaxPoints;
              int idx3 = (idx4 - 1 + graphSettings.effectiveMaxPoints) % graphSettings.effectiveMaxPoints;
              int idx2 = (idx3 - 1 + graphSettings.effectiveMaxPoints) % graphSettings.effectiveMaxPoints;
              int idx1 = (idx2 - 1 + graphSettings.effectiveMaxPoints) % graphSettings.effectiveMaxPoints;

              float val4 = gd->dataPoints[idx4];
              float val3 = gd->dataPoints[idx3];
              float val2 = gd->dataPoints[idx2];
              float val1 = gd->dataPoints[idx1];

              float yNorm4 = (val4 - minY) / (maxY - minY);
              float yNorm3 = (val3 - minY) / (maxY - minY);
              float yNorm2 = (val2 - minY) / (maxY - minY);
              float yNorm1 = (val1 - minY) / (maxY - minY);
              yNorm4 = constrain(yNorm4, 0.0f, 1.0f);
              yNorm3 = constrain(yNorm3, 0.0f, 1.0f);
              yNorm2 = constrain(yNorm2, 0.0f, 1.0f);
              yNorm1 = constrain(yNorm1, 0.0f, 1.0f);

              int py4 = (GRAPH_AREA_HEIGHT - 1) - static_cast<int>(yNorm4 * (GRAPH_AREA_HEIGHT - 1));
              int py3 = (GRAPH_AREA_HEIGHT - 1) - static_cast<int>(yNorm3 * (GRAPH_AREA_HEIGHT - 1));
              int py2 = (GRAPH_AREA_HEIGHT - 1) - static_cast<int>(yNorm2 * (GRAPH_AREA_HEIGHT - 1));
              int py1 = (GRAPH_AREA_HEIGHT - 1) - static_cast<int>(yNorm1 * (GRAPH_AREA_HEIGHT - 1));

              int pts[4][2] = {
                {GRAPH_AREA_WIDTH - 3, py1},
                {GRAPH_AREA_WIDTH - 2, py2},
                {GRAPH_AREA_WIDTH - 1, py3},
                {GRAPH_AREA_WIDTH, py4}  // Slight extrapolation for end smoothing
              };

              drawCatmullRomSplineMulti(pts, 4, graphSettings.devices[dIdx].lineColor, graphSettings.all.lineThickness, graphSettings.enableAntialiasing);
            } else {
              drawInterpolatedLineInBuffer(px1, py1Approx, px2, py2, graphSettings.devices[dIdx].lineColor, graphSettings.all.lineThickness, graphSettings.enableAntialiasing);
            }
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
  for (int i = 0; i < GRAPH_AREA_WIDTH * GRAPH_AREA_HEIGHT; i++) {
    graphBuffer[i] = GRAPH_BG_COLOR;
  }

  // Add horizontal grid lines only
  if (graphSettings.showGrids) {
    for (int i = 1; i < 5; i++) {
      int gy = (GRAPH_AREA_HEIGHT * i) / 5;
      for (int x = 0; x < GRAPH_AREA_WIDTH; x++) {
        graphBuffer[gy * GRAPH_AREA_WIDTH + x] = GRAPH_GRID_COLOR;
      }
    }
  }

  // Draw all data from arrays
  if (guiState.currentGraphTab == GRAPH_TAB_ALL) {
    for (int dev = 0; dev < 6; dev++) {
      if (!graphSettings.all.deviceEnabled[dev]) continue;

      SimpleGraphData *gd = &deviceGraphData[dev][dt];
      if (gd->count < 2) continue;

      uint16_t color = graphSettings.devices[dev].lineColor;
      int thickness = graphSettings.all.lineThickness;

      const int maxPts = gd->count;
      int (*pts)[2] = new int[maxPts][2]; // Dynamic array (Teensy has plenty RAM)
      int ptCount = 0;

      for (int i = 0; i < gd->count; i++) {
        int idx = (gd->writeIndex - gd->count + i) % graphSettings.effectiveMaxPoints;
        if (idx < 0) idx += graphSettings.effectiveMaxPoints;

        float tVal = gd->timePoints[idx];
        if (tVal < minTime || tVal > maxTime) continue;

        float val = gd->dataPoints[idx];
        if (isnan(val) || isinf(val)) continue;

        float tNorm = (tVal - minTime) / (maxTime - minTime);
        tNorm = constrain(tNorm, 0.0f, 1.0f);
        float vNorm = (val - minY) / (maxY - minY);
        vNorm = constrain(vNorm, 0.0f, 1.0f);

        int pxPt = static_cast<int>(tNorm * (GRAPH_AREA_WIDTH - 1));
        int pyPt = (GRAPH_AREA_HEIGHT - 1) - static_cast<int>(vNorm * (GRAPH_AREA_HEIGHT - 1));

        pts[ptCount][0] = pxPt;
        pts[ptCount][1] = pyPt;
        ptCount++;
      }

      // Draw the full spline if enabled and enough points
      if (graphSettings.enableInterpolation && ptCount >= 4) {
        drawCatmullRomSplineMulti(pts, ptCount, color, thickness, graphSettings.enableAntialiasing);
      } else {
        // Fallback to per-segment lines if too few points or disabled
        int prevX = -1, prevY = -1;
        for (int j = 0; j < ptCount; j++) {
          int px = pts[j][0];
          int py = pts[j][1];
          if (prevX >= 0 && prevY >= 0) {
            if (graphSettings.enableAntialiasing) {
              drawAntialiasedLineInBuffer(prevX, prevY, px, py, color, thickness);
            } else {
              drawBresenhamLineInBuffer(prevX, prevY, px, py, color, thickness);
            }
          }
          prevX = px;
          prevY = py;
        }
      }

      delete[] pts; // Free the array
    }
  } else {
    // Single-device mode - adapt similarly (collect points into pts[][] and call drawCatmullRomSplineMulti)
    int devIdx = static_cast<int>(guiState.currentGraphTab) - 1;
    SimpleGraphData *gd = &deviceGraphData[devIdx][dt];

    if (gd->count >= 2) {
      uint16_t color = graphSettings.devices[devIdx].lineColor;
      int thickness = graphSettings.all.lineThickness; // Use global thickness (or add per-device if needed)

      // Collect all visible points into array for multi-point spline
      const int maxPts = gd->count;
      int (*pts)[2] = new int[maxPts][2];
      int ptCount = 0;

      for (int i = 0; i < gd->count; i++) {
        int idx = (gd->writeIndex - gd->count + i) % graphSettings.effectiveMaxPoints;
        if (idx < 0) idx += graphSettings.effectiveMaxPoints;

        float tVal = gd->timePoints[idx];
        if (tVal < minTime || tVal > maxTime) continue;

        float val = gd->dataPoints[idx];
        if (isnan(val) || isinf(val)) continue;

        float tNorm = (tVal - minTime) / (maxTime - minTime);
        tNorm = constrain(tNorm, 0.0f, 1.0f);
        float vNorm = (val - minY) / (maxY - minY);
        vNorm = constrain(vNorm, 0.0f, 1.0f);

        int pxPt = static_cast<int>(tNorm * (GRAPH_AREA_WIDTH - 1));
        int pyPt = (GRAPH_AREA_HEIGHT - 1) - static_cast<int>(vNorm * (GRAPH_AREA_HEIGHT - 1));

        pts[ptCount][0] = pxPt;
        pts[ptCount][1] = pyPt;
        ptCount++;
      }

      // Draw the full spline if enabled and enough points
      if (graphSettings.enableInterpolation && ptCount >= 4) {
        drawCatmullRomSplineMulti(pts, ptCount, color, thickness, graphSettings.enableAntialiasing);
      } else {
        // Fallback to per-segment lines if too few points or disabled
        int prevX = -1, prevY = -1;
        for (int j = 0; j < ptCount; j++) {
          int px = pts[j][0];
          int py = pts[j][1];
          if (prevX >= 0 && prevY >= 0) {
            if (graphSettings.enableAntialiasing) {
              drawAntialiasedLineInBuffer(prevX, prevY, px, py, color, thickness);
            } else {
              drawBresenhamLineInBuffer(prevX, prevY, px, py, color, thickness);
            }
          }
          prevX = px;
          prevY = py;
        }
      }

      delete[] pts; // Free the array
    }
  }
}

// Helper to blend two colors (for antialiasing) - Fixed RGB extraction and scaling
static uint16_t blendColors(uint16_t fg, uint16_t bg, float alpha) {
  // Extract RGB from 565 (5-6-5 bits)
  uint8_t fg_r = ((fg >> 11) & 0x1F) * (255 / 31);  // Scale to 8-bit
  uint8_t fg_g = ((fg >> 5) & 0x3F) * (255 / 63);
  uint8_t fg_b = (fg & 0x1F) * (255 / 31);

  uint8_t bg_r = ((bg >> 11) & 0x1F) * (255 / 31);
  uint8_t bg_g = ((bg >> 5) & 0x3F) * (255 / 63);
  uint8_t bg_b = (bg & 0x1F) * (255 / 31);

  // Blend with alpha (0.0=bg, 1.0=fg) - Use integer math for speed
  uint8_t r = (uint8_t)((fg_r * alpha) + (bg_r * (1.0f - alpha)));
  uint8_t g = (uint8_t)((fg_g * alpha) + (bg_g * (1.0f - alpha)));
  uint8_t b = (uint8_t)((fg_b * alpha) + (bg_b * (1.0f - alpha)));

  // Pack back to 565 (scale down)
  return ((r * 31 / 255) << 11) | ((g * 63 / 255) << 5) | (b * 31 / 255);
}

// Antialiased line using Wu's algorithm (smooth edges)
static void drawAntialiasedLineInBuffer(int x0, int y0, int x1, int y1, uint16_t color, int thickness) {
  // Revert: Ignore thickness (force to 1) to avoid artifacts
  thickness = 1;

  bool steep = abs(y1 - y0) > abs(x1 - x0);
  if (steep) {
    std::swap(x0, y0);
    std::swap(x1, y1);
  }
  if (x0 > x1) {
    std::swap(x0, x1);
    std::swap(y0, y1);
  }

  int dx = x1 - x0;
  int dy = y1 - y0;
  float gradient = (dx == 0) ? 1.0f : (float)dy / (float)dx;

  // Start point
  float xend = round(x0);
  float yend = y0 + gradient * (xend - x0);
  float xgap = 1.0f - (x0 + 0.5f - floor(x0 + 0.5f));
  int xpxl1 = (int)xend;
  int ypxl1 = (int)floor(yend);
  float fpart = yend - floor(yend);
  float intery = yend + gradient;

  // Draw start
  if (steep) {
    if (ypxl1 >= 0 && ypxl1 < GRAPH_AREA_WIDTH && xpxl1 >= 0 && xpxl1 < GRAPH_AREA_HEIGHT) {
      graphBuffer[xpxl1 * GRAPH_AREA_WIDTH + ypxl1] = blendColors(color, graphBuffer[xpxl1 * GRAPH_AREA_WIDTH + ypxl1], (1.0f - fpart) * xgap);
    }
    if (ypxl1 + 1 >= 0 && ypxl1 + 1 < GRAPH_AREA_WIDTH && xpxl1 >= 0 && xpxl1 < GRAPH_AREA_HEIGHT) {
      graphBuffer[xpxl1 * GRAPH_AREA_WIDTH + ypxl1 + 1] = blendColors(color, graphBuffer[xpxl1 * GRAPH_AREA_WIDTH + ypxl1 + 1], fpart * xgap);
    }
  } else {
    if (xpxl1 >= 0 && xpxl1 < GRAPH_AREA_WIDTH && ypxl1 >= 0 && ypxl1 < GRAPH_AREA_HEIGHT) {
      graphBuffer[ypxl1 * GRAPH_AREA_WIDTH + xpxl1] = blendColors(color, graphBuffer[ypxl1 * GRAPH_AREA_WIDTH + xpxl1], (1.0f - fpart) * xgap);
    }
    if (xpxl1 >= 0 && xpxl1 < GRAPH_AREA_WIDTH && ypxl1 + 1 >= 0 && ypxl1 + 1 < GRAPH_AREA_HEIGHT) {
      graphBuffer[(ypxl1 + 1) * GRAPH_AREA_WIDTH + xpxl1] = blendColors(color, graphBuffer[(ypxl1 + 1) * GRAPH_AREA_WIDTH + xpxl1], fpart * xgap);
    }
  }

  // Main loop
  for (int x = xpxl1 + 1; x < x1; x++) {
    int ipart = (int)floor(intery);
    fpart = intery - (float)ipart;

    if (steep) {
      if (x >= 0 && x < GRAPH_AREA_HEIGHT && ipart >= 0 && ipart < GRAPH_AREA_WIDTH) {
        graphBuffer[x * GRAPH_AREA_WIDTH + ipart] = blendColors(color, graphBuffer[x * GRAPH_AREA_WIDTH + ipart], 1.0f - fpart);
      }
      if (x >= 0 && x < GRAPH_AREA_HEIGHT && ipart + 1 >= 0 && ipart + 1 < GRAPH_AREA_WIDTH) {
        graphBuffer[x * GRAPH_AREA_WIDTH + ipart + 1] = blendColors(color, graphBuffer[x * GRAPH_AREA_WIDTH + ipart + 1], fpart);
      }
    } else {
      if (x >= 0 && x < GRAPH_AREA_WIDTH && ipart >= 0 && ipart < GRAPH_AREA_HEIGHT) {
        graphBuffer[ipart * GRAPH_AREA_WIDTH + x] = blendColors(color, graphBuffer[ipart * GRAPH_AREA_WIDTH + x], 1.0f - fpart);
      }
      if (x >= 0 && x < GRAPH_AREA_WIDTH && ipart + 1 >= 0 && ipart + 1 < GRAPH_AREA_HEIGHT) {
        graphBuffer[(ipart + 1) * GRAPH_AREA_WIDTH + x] = blendColors(color, graphBuffer[(ipart + 1) * GRAPH_AREA_WIDTH + x], fpart);
      }
    }
    intery += gradient;
  }

  // End point
  xend = round(x1);
  yend = y1 + gradient * (xend - x1);
  xgap = 1.0f - (x1 + 0.5f - floor(x1 + 0.5f));
  int xpxl2 = (int)xend;
  int ypxl2 = (int)floor(yend);
  fpart = yend - floor(yend);

  if (steep) {
    if (ypxl2 >= 0 && ypxl2 < GRAPH_AREA_WIDTH && xpxl2 >= 0 && xpxl2 < GRAPH_AREA_HEIGHT) {
      graphBuffer[xpxl2 * GRAPH_AREA_WIDTH + ypxl2] = blendColors(color, graphBuffer[xpxl2 * GRAPH_AREA_WIDTH + ypxl2], (1.0f - fpart) * xgap);
    }
    if (ypxl2 + 1 >= 0 && ypxl2 + 1 < GRAPH_AREA_WIDTH && xpxl2 >= 0 && xpxl2 < GRAPH_AREA_HEIGHT) {
      graphBuffer[xpxl2 * GRAPH_AREA_WIDTH + ypxl2 + 1] = blendColors(color, graphBuffer[xpxl2 * GRAPH_AREA_WIDTH + ypxl2 + 1], fpart * xgap);
    }
  } else {
    if (xpxl2 >= 0 && xpxl2 < GRAPH_AREA_WIDTH && ypxl2 >= 0 && ypxl2 < GRAPH_AREA_HEIGHT) {
      graphBuffer[ypxl2 * GRAPH_AREA_WIDTH + xpxl2] = blendColors(color, graphBuffer[ypxl2 * GRAPH_AREA_WIDTH + xpxl2], (1.0f - fpart) * xgap);
    }
    if (xpxl2 >= 0 && xpxl2 < GRAPH_AREA_WIDTH && ypxl2 + 1 >= 0 && ypxl2 + 1 < GRAPH_AREA_HEIGHT) {
      graphBuffer[(ypxl2 + 1) * GRAPH_AREA_WIDTH + xpxl2] = blendColors(color, graphBuffer[(ypxl2 + 1) * GRAPH_AREA_WIDTH + xpxl2], fpart * xgap);
    }
  }
}

/**
 * @brief A helper function to draw a line in the rolling buffer using Bresenham's algorithm.
 */
static void drawBresenhamLineInBuffer(int x0, int y0, int x1, int y1, uint16_t color, int thickness) {
  color &= 0xFFFF;  // Ensure it's 16-bit

  // For thickness >1, draw multiple parallel lines
  for (int offset = -thickness / 2; offset <= thickness / 2; offset++) {
    int dy0 = y0 + offset;
    int dy1 = y1 + offset;

    int dx = abs(x1 - x0);
    int dy = abs(dy1 - dy0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (dy0 < dy1) ? 1 : -1;
    int err = dx - dy;
    int x = x0, y = dy0;

    while (true) {
      if (x >= 0 && x < GRAPH_AREA_WIDTH && y >= 0 && y < GRAPH_AREA_HEIGHT) {
        graphBuffer[y * GRAPH_AREA_WIDTH + x] = color;
      }
      if (x == x1 && y == dy1) break;
      int e2 = 2 * err;
      if (e2 > -dy) { err -= dy; x += sx; }
      if (e2 < dx) { err += dx; y += sy; }
    }
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

  // Clear X-axis label area - increased height to ensure full clearing
  tft.fillRect(GRAPH_AREA_X, GRAPH_AREA_Y + GRAPH_AREA_HEIGHT+2, GRAPH_AREA_WIDTH, 20, COLOR_BLACK);

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
    
    // Calculate text width more accurately (6 pixels per character for default font)
    int textWidth = strlen(buf) * 6;
    int textX = x - (textWidth / 2); // Center the text on the tick mark
    
    // Ensure text stays within graph bounds
    if (textX < GRAPH_AREA_X) {
      textX = GRAPH_AREA_X + 2; // Small margin from left edge
    } else if (textX + textWidth > GRAPH_AREA_X + GRAPH_AREA_WIDTH) {
      textX = GRAPH_AREA_X + GRAPH_AREA_WIDTH - textWidth - 2; // Small margin from right edge
    }
    
    tft.setCursor(textX, GRAPH_AREA_Y + GRAPH_AREA_HEIGHT +10);
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
  int tabWidth=55;
  int startX=90;

  for(int i=0; i<7; i++){
    uint16_t col=(i== (int)guiState.currentGraphTab)? COLOR_BLUE: COLOR_GRAY;
    tft.fillRect(startX + i* tabWidth, 10, tabWidth-2, GRAPH_TAB_HEIGHT, col);
    tft.drawRect(startX + i* tabWidth, 10, tabWidth-2, GRAPH_TAB_HEIGHT, COLOR_WHITE);

    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(startX + i* tabWidth+3, 11 + (GRAPH_TAB_HEIGHT/2)+5);
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
  tft.setCursor(20, yPos + 15);
  tft.print("Data Type:");
  const char* dtLabels[3]={"Current","Voltage","Power"};
  btnGraphDataType.x=150;
  btnGraphDataType.y=yPos+5;
  drawButton(btnGraphDataType, COLOR_YELLOW, COLOR_BLACK, dtLabels[graphSettings.all.dataType], false,true);
  yPos+=35;

  // Devices
  tft.setCursor(20, yPos);
  tft.print("Devices to Show:");
  yPos+=25;
  for(int i=0;i<6;i++){
    int buttonX= 30 + (i%3)*140;
    int buttonY= yPos - 7 + (i/3)*40;
    uint16_t btnColor= graphSettings.all.deviceEnabled[i]? COLOR_RED: COLOR_GRAY_DARK;
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
  tft.setCursor(20, yPos+5);
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
  graphSettings.graphRefreshRate= constrain((int)rate,20,500);
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