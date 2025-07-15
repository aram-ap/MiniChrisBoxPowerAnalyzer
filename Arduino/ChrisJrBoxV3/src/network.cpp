/**
 * @file network.cpp
 * @brief Network communication implementation
 */

#include "network.h"
#include "config.h"
#include "serial_commands.h"
#include "switches.h"
#include "sensors.h"
#include "script.h"
#include "datalog.h"
#include "time_utils.h"
#include "display.h"
#include "settings.h"
#include <EEPROM.h>

using namespace qindesign::network;

// Network objects
EthernetServer tcpServer(8080);
EthernetUDP udp;
EthernetClient tcpClients[5];

// Network state
NetworkConfig networkConfig;
StreamConfig streamConfig;
bool networkInitialized = false;
bool ethernetConnected = false;
NetworkInitState networkInitState = NET_IDLE;

// Private variables
static unsigned long networkInitStartTime = 0;
static unsigned long lastNetworkCheck = 0;
static unsigned long lastStreamTime = 0;
static unsigned long lastHeartbeat = 0;
static unsigned long lastInitScreenUpdate = 0;
static String lastInitStatusText = "";
static String networkCommandBuffer = "";
static char responseBuffer[1024];
static bool streamingActive = false;
static bool heartbeatEnabled = true;

// External references
extern SystemState systemState;
extern GUIState guiState;

void handleTCPClients();
void handleUDPCommunication();
void sendLiveDataStream();

void initNetwork() {
  loadNetworkConfig();

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

void updateNetwork() {
  if (networkInitState != NET_CHECKING_LINK &&
      networkInitState != NET_INITIALIZING &&
      networkInitState != NET_DHCP_WAIT) return;

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
      Ethernet.begin(uint32ToIP(networkConfig.staticIP),
                     uint32ToIP(networkConfig.subnet),
                     uint32ToIP(networkConfig.gateway));
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
      Ethernet.begin(uint32ToIP(networkConfig.staticIP),
                     uint32ToIP(networkConfig.subnet),
                     uint32ToIP(networkConfig.gateway));
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

void waitForNetworkInit() {
  while (networkInitState == NET_CHECKING_LINK ||
         networkInitState == NET_INITIALIZING ||
         networkInitState == NET_DHCP_WAIT) {
    updateNetwork();
    updateInitializationScreen();
    delay(50);
  }

  updateInitializationScreen();
  if (networkInitState == NET_INITIALIZED) {
    delay(1000);
  } else {
    delay(500);
  }
}

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
    initNetwork();
  }
}

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
      systemState.lock = true;
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
    bool prevLock = systemState.lock;
    systemState.lock = lockState;
    updateLockButton();
    if (!systemState.lock && prevLock) syncOutputsToSwitches();

    JsonDocument response;
    response["type"] = "command_response";
    response["cmd"] = "lock";
    response["state"] = systemState.lock;
    response["success"] = true;
    serializeJson(response, *responseOutput);
    responseOutput->println();
  }
  else if (cmd == "safety_stop") {
    bool stopState = doc["state"].as<bool>();
    if (stopState && !systemState.safetyStop) {
      systemState.lockBeforeStop = systemState.lock;
      systemState.safetyStop = true;
      setAllOutputsOff();
      if (isScriptRunning) stopScript(true);
      if (systemState.recording) stopRecording();
    } else if (!stopState && systemState.safetyStop) {
      systemState.safetyStop = false;
      bool prevLock = systemState.lock;
      systemState.lock = systemState.lockBeforeStop;
      if (!systemState.lock && prevLock) syncOutputsToSwitches();
    }

    JsonDocument response;
    response["type"] = "command_response";
    response["cmd"] = "safety_stop";
    response["state"] = systemState.safetyStop;
    response["success"] = true;
    serializeJson(response, *responseOutput);
    responseOutput->println();
  }
  // Recording control
  else if (cmd == "start_recording") {
    if (!systemState.recording) {
      startRecording(false);
      sendResponse("{\"type\":\"command_response\",\"cmd\":\"start_recording\",\"success\":true}", responseOutput);
    } else {
      sendResponse("{\"type\":\"error\",\"message\":\"Already recording\"}", responseOutput);
    }
  }
  else if (cmd == "stop_recording") {
    if (systemState.recording) {
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
    if (!isScriptRunning && !systemState.safetyStop) {
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
    systemState.fanSpeed = speed;
    systemState.fanOn = (speed > 0);
    saveSettingsToEEPROM();
    applyFanSettings();

    JsonDocument response;
    response["type"] = "command_response";
    response["cmd"] = "set_fan_speed";
    response["value"] = systemState.fanSpeed;
    response["success"] = true;
    serializeJson(response, *responseOutput);
    responseOutput->println();
  }
  else if (cmd == "set_update_rate") {
    unsigned long rate = doc["value"].as<unsigned long>();
    rate = constrain(rate, 10UL, 5000UL);
    systemState.updateRate = rate;
    saveSettingsToEEPROM();

    JsonDocument response;
    response["type"] = "command_response";
    response["cmd"] = "set_update_rate";
    response["value"] = systemState.updateRate;
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

void handleDataStreaming(unsigned long currentMillis) {
  if (streamingActive && (currentMillis - lastStreamTime >= streamConfig.streamInterval)) {
    sendLiveDataStream();
    lastStreamTime = currentMillis;
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

  // Send to UDP if enabled
  if (streamConfig.udpStreamEnabled && networkInitialized) {
    udp.beginPacket(uint32ToIP(networkConfig.udpTargetIP), networkConfig.udpTargetPort);
    udp.print(responseBuffer);
    udp.endPacket();
  }
}

bool generateLiveDataJSON(char* buffer, size_t bufferSize) {
  JsonDocument doc;

  doc["type"] = "live_data";
  doc["timestamp"] = getCurrentTimeString();
  doc["script_running"] = isScriptRunning;
  doc["script_time"] = scriptTimeSeconds;
  doc["recording"] = systemState.recording;
  doc["locked"] = systemState.lock;
  doc["safety_stop"] = systemState.safetyStop;

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

bool generateStatusJSON(char* buffer, size_t bufferSize) {
  JsonDocument doc;

  doc["type"] = "status";
  doc["timestamp"] = getCurrentTimeString();
  doc["version"] = SOFTWARE_VERSION;
  doc["locked"] = systemState.lock;
  doc["safety_stop"] = systemState.safetyStop;
  doc["recording"] = systemState.recording;
  doc["script_running"] = isScriptRunning;
  doc["script_paused"] = isScriptPaused;
  doc["current_script"] = currentScript.scriptName;
  doc["dark_mode"] = systemState.darkMode;
  doc["external_sd"] = systemState.sdAvailable;
  doc["internal_sd"] = systemState.internalSdAvailable;
  doc["ethernet_connected"] = ethernetConnected;
  doc["fan_speed"] = systemState.fanSpeed;
  doc["update_rate"] = systemState.updateRate;
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

void sendResponse(const char* response, Print* output) {
  output->println(response);
}

void saveNetworkConfig() {
  EEPROM.put(EEPROM_NETWORK_CONFIG_ADDR, networkConfig);
}

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

void loadNetworkFieldsFromConfig() {
  NetworkEditField* networkFields = getNetworkFields();
  int* numNetworkFields = getNumNetworkFields();
  *numNetworkFields = 0;

  // Configure edit fields with current network settings
  struct FieldConfig {
    int x, y, w, h, type;
    String configValue;
  };

  FieldConfig fields[] = {
    {200, 80, 120, 25, 0, ipToString(uint32ToIP(networkConfig.staticIP))},
    {200, 110, 120, 25, 0, ipToString(uint32ToIP(networkConfig.subnet))},
    {200, 140, 120, 25, 0, ipToString(uint32ToIP(networkConfig.gateway))},
    {200, 170, 120, 25, 0, ipToString(uint32ToIP(networkConfig.dns))},
    {200, 200, 80, 25, 1, String(networkConfig.tcpPort)},
    {200, 230, 80, 25, 1, String(networkConfig.udpPort)},
    {200, 260, 80, 25, 2, String(networkConfig.networkTimeout)},
    {340, 260, 80, 25, 2, String(networkConfig.dhcpTimeout)}
  };

  for (int i = 0; i < 8; i++) {
    networkFields[*numNetworkFields].x = fields[i].x;
    networkFields[*numNetworkFields].y = fields[i].y;
    networkFields[*numNetworkFields].w = fields[i].w;
    networkFields[*numNetworkFields].h = fields[i].h;
    networkFields[*numNetworkFields].fieldType = fields[i].type;
    strcpy(networkFields[*numNetworkFields].value, fields[i].configValue.c_str());
    (*numNetworkFields)++;
  }
}

IPAddress uint32ToIP(uint32_t ip) {
  return IPAddress((ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
}

uint32_t ipToUint32(IPAddress ip) {
  return ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) | ((uint32_t)ip[2] << 8) | (uint32_t)ip[3];
}

String ipToString(IPAddress ip) {
  return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
}

void printNetworkStatus() {
  if (networkInitialized) {
    Serial.print("Network ready. IP: ");
    Serial.println(ipToString(Ethernet.localIP()));
  } else {
    Serial.println("Network initialization failed or disabled");
  }
}

// Helper function to get update status text for initialization screen
String getNetworkInitStatusText() {
  return lastInitStatusText;
}

// Helper function to update initialization screen status
void updateNetworkInitStatus(unsigned long currentTime) {
  if (currentTime - lastInitScreenUpdate < 250) return;
  lastInitScreenUpdate = currentTime;

  String statusText = "";

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
        break;
      case NET_CHECKING_LINK:
        statusText = "• Network: Checking cable... " + timeStr;
        break;
      case NET_INITIALIZING:
        statusText = "• Network: Initializing... " + timeStr;
        break;
      case NET_DHCP_WAIT:
        statusText = "• Network: Getting IP... " + timeStr;
        break;
      case NET_INITIALIZED:
        statusText = "• Network: Ready";
        break;
      case NET_FAILED:
        statusText = "• Network: Failed";
        break;
    }
  } else {
    statusText = "• Network: Disabled";
  }

  lastInitStatusText = statusText;
}
