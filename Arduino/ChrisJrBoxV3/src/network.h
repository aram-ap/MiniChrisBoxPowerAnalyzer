/**
 * @file network.h
 * @brief Network communication functionality
 */

#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include "network_wrapper.h"  // Use wrapper instead of direct QNEthernet
#include <ArduinoJson.h>
#include "types.h"

// Network functions
void initNetwork();
void updateNetwork();
void waitForNetworkInit();
void handleNetworkCommunication();
void handleDataStreaming(unsigned long currentMillis);
void checkNetworkStatus();
void printNetworkStatus();

// Network configuration
void saveNetworkConfig();
void loadNetworkConfig();
void saveNetworkFieldToConfig(int fieldIndex, const char* value);
void loadNetworkFieldsFromConfig();

// Data generation
bool generateLiveDataJSON(char* buffer, size_t bufferSize);
bool generateStatusJSON(char* buffer, size_t bufferSize);
bool generateScriptListJSON(char* buffer, size_t bufferSize);

// Command processing
void processNetworkCommand(String command, Print* responseOutput);
void sendResponse(const char* response, Print* output);
void sendHeartbeat();

// Utility functions
IPAddress uint32ToIP(uint32_t ip);
uint32_t ipToUint32(IPAddress ip);
String ipToString(IPAddress ip);

// External references
extern NetworkConfig networkConfig;
extern StreamConfig streamConfig;
extern SystemState systemState;
extern bool networkInitialized;
extern bool ethernetConnected;
extern NetworkInitState networkInitState;

#endif // NETWORK_H
