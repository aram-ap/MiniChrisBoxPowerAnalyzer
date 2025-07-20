/**
 * @file graphs.h
 * @brief Graph system functionality
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

#ifndef GRAPHS_H
#define GRAPHS_H

#include "config.h"
#include <Arduino.h>
#include "types.h"

// Graph drawing area - FIXED: Made narrower
#define GRAPH_AREA_X      35  // Increased for Y-axis labels
#define GRAPH_AREA_Y      50
#define GRAPH_AREA_WIDTH  340  // Reduced by 10px
#define GRAPH_AREA_HEIGHT 210

// Info panel width
#define GRAPH_INFO_WIDTH 110  // Reduced slightly

// Tab height
#define GRAPH_TAB_HEIGHT 25

// Background/grid colors
#define GRAPH_BG_COLOR   0x0000
#define GRAPH_GRID_COLOR 0x2104

// Default line colors for devices
extern const uint16_t DEFAULT_GRAPH_COLORS[8];

// Simple per-device data buffer
struct SimpleGraphData {
  float timePoints[GRAPH_MAX_POINTS];
  float dataPoints[GRAPH_MAX_POINTS];
  int   count;
  int   writeIndex;
};

// Global data and settings
extern GraphSettings       graphSettings;
extern SimpleGraphData     deviceGraphData[6][3];
extern unsigned long       graphStartTime;

// Add graph state structure
struct GraphState {
    float lastMinY;
    float lastMaxY;
    float lastMinTime;
    float lastMaxTime;
    bool needsFullRedraw;
    bool axesNeedUpdate;
    unsigned long lastAxesUpdate;
    unsigned long lastDataUpdate;
    float cachedOldestTime;  // New: Cached min time across data
    float cachedNewestTime;  // New: Cached max time across data
    bool dataBoundsDirty;    // New: Flag for recompute
};

extern GraphState graphState;

// NEW: RAM framebuffer for smooth scrolling
extern uint16_t graphBuffer[GRAPH_AREA_WIDTH * GRAPH_AREA_HEIGHT];

// Core API
void initGraphs();
void updateGraphData(unsigned long t);
void clearGraphData();
void pauseGraphData();
void resumeGraphData();
void saveGraphSettings();
void loadGraphSettings();
void resetGraphSettings();

// Drawing
void drawGraphPage();
void drawGraphSettingsPage();
void drawGraphDisplaySettingsPage();
void drawGraphArea();
void drawGraphTabs();
void drawGraphGrid();
void drawGraphData();
void drawGraphInfo();
void drawAxesLabels();
void drawAllGraphSettings();
void drawDeviceGraphSettings(int deviceIndex);
void updateGraphAreaSmooth();
void drawAxesLabelsSmooth(float minTime, float maxTime, float minY, float maxY, GraphDataType dt);
void drawGraphDataSmooth(float minTime, float maxTime, float minY, float maxY, GraphDataType dt);

// Helpers
void addGraphPoint(int deviceIndex, GraphDataType dt, float t, float v);
float getDeviceGraphValue(int deviceIndex, GraphDataType dt);
void switchGraphTab(GraphTab newTab);
void cycleAllGraphDataType();
void toggleDeviceInAll(int deviceIndex);
void setGraphAxisBounds(GraphTab tab, GraphDataType dt, float minY, float maxY);
void setDeviceGraphColor(int deviceIndex, uint16_t color);
void toggleDeviceGraphDataType(int deviceIndex, GraphDataType dt);
const char* getGraphDataTypeName(GraphDataType dt);
const char* getGraphDataTypeUnit(GraphDataType dt);

// Display settings
void applyGraphRefreshRate(unsigned long rate);
void toggleAntialiasing(bool on);
void toggleGrids(bool on);
void setEffectiveMaxPoints(int pts);

void onScriptEnd();
void drawScriptTimer();

#endif // GRAPHS_H
