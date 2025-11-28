#include <pgmspace.h>
#include <ESP8266WiFi.h>  // ESP8266,
#include <espnow.h>       // ESP8266
#include <RadioLib.h>
#include <vector>
#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager
#include <EEPROM.h>
#include <queue>
#include <ArduinoJson.h>

// LoRa Module Pins (LLCC68)
#define LORA_SS 15
#define LORA_RST 16
#define LORA_DIO1 5
#define LORA_BUSY 4
LLCC68 radio = new Module(LORA_SS, LORA_DIO1, LORA_RST, LORA_BUSY);

#define MAX_INTRA_CLUSTER 20
#define TO_GATEWAY 0
#define TO_NODE 1
#define ACTIVE_NODE_TIMER 3600000
#define LORA_TRANSMIT_TIMER 5000
#define EEPROM_SIZE 512  // Define EEPROM size based on need
#define MAX_MESSAGE_QUEUE 10
#define LED 2
#define CONFIG_PIN 0
#define MAX_PAYLOAD_SIZE 64
#define CTRLWORD 0x60
#define LORA_CAD_TIMEOUT 1000  // Channel Activity Detection timeout (ms)
#define MAX_BACKOFF_MS 3000    // Maximum random backoff time
#define timeoutTimer 5 * 60 * 1000
#define MAGIC_BYTE 0xA5

bool debugEnabled = true;
unsigned long timeOut = millis();
unsigned long timeOutLoRa = millis();
String inputString = "";

#define DEBUG_print(x) \
  if (debugEnabled) Serial.print(x)
#define DEBUG_println(x) \
  if (debugEnabled) Serial.println(x)
#define DEBUG_printf(format, ...) \
  if (debugEnabled) Serial.printf(format, ##__VA_ARGS__)
#define DEBUG_printF(x) \
  if (debugEnabled) Serial.print(F(x))
#define DEBUG_printlnF(x) \
  if (debugEnabled) Serial.println(F(x))

volatile bool transmitFlag = false;
volatile bool operationDone = false;

// Interrupt handler for LoRa
ICACHE_RAM_ATTR void LoRaInterruptHandler() {
  operationDone = true;
}

typedef struct idConfig {
  uint8_t clusterId;    //ESPNow ID, to receive and transmit data in intra-cluster, also LoRa node ID
  uint8_t networkId;    //ESPNow netwokID
  uint8_t toGatewayId;  //LoRa neighbor bound to server
  uint8_t toNodeId;     //LoRa neighbor bound to node
  float loraFreq;       //LoRa ferquency
} idConfig;

idConfig id;

uint8_t clusterHeadMAC[6] = { 0x24, 0x6F, 0x28, id.networkId, id.clusterId, 0xFF };

// Structure to hold data
typedef struct struct_message {
  uint8_t sourceId;  // ID of the sender
  uint8_t targetId;  // ID of the recipient
  uint8_t networkId;
  uint8_t direction;                  // 0 = toGateway, 1 = toNode
  uint8_t messageSize;                // Actual size of the message
  uint8_t message[MAX_PAYLOAD_SIZE];  // Message content (max 64 bytes)
} struct_message;

struct_message outgoingData;

// Queue to hold ESP-NOW received messages
std::queue<struct_message> messageQueueLora;

// Structure to store client details
struct ClientInfo {
  String macAddress;
  unsigned long lastSeen;  // Timestamp in milliseconds
};
std::vector<ClientInfo> clusterMembers;

// Structure to store msg to ESPnow client details
struct ClientESPNow {
  String macAddress;
  struct_message msg;
  unsigned long lastSeen;  // Timestamp in milliseconds
};

std::queue<ClientESPNow> messageQueueESPNow;

unsigned long lastTransmitTime = millis();
unsigned long lastClientCheckTime = millis();

String macToStr(const uint8_t *mac);
bool isLocalClient(const String &macAddress);  //
void addOrUpdateClient(const uint8_t *mac);
void removeInactiveClients();
void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus);
void onESPNowReceive(uint8_t *mac, uint8_t *incomingData, uint8_t len);
void sendToESPNowClient(struct_message data, const uint8_t *peerMac);
void forwardLoRa(struct_message data);
void processReceivedLoRa();
uint8_t serializeMessage(struct_message *msg, uint8_t *buffer, int bufferSize);
bool deserializeMessage(uint8_t *buffer, int bufferLen, struct_message *msg);  // filtering
void sendNextLoRaMessage();
bool ensurePeerExists(const uint8_t *peerAddress);
void saveConfigToEEPROM();
bool loadConfigFromEEPROM();
void removeInactiveESPNowClients();
bool isChannelFree();  // lora
void eraseConfig();
void parseJSON(String jsonString);
void printConfig();
void checkCommand();

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);
  pinMode(CONFIG_PIN, INPUT_PULLUP);
  delay(1000);
  EEPROM.begin(EEPROM_SIZE);

  // Load config from EEPROM or run WiFiManager if invalid
  if (!loadConfigFromEEPROM()) {
    Serial.println("No config found.");
    Serial.println("Send JSON like:");
    Serial.println("{\"clusterId\":10,\"networkId\":1,\"toGatewayId\":1,\"toNodeId\":1,\"loraFreq\":921.5}");   
    while (1) {
      checkCommand();
      delay(10);
    }
    delay(1000);
    ESP.restart();
  }
  // Setup ESP-NOW
  WiFi.mode(WIFI_STA);
  wifi_set_macaddr(STATION_IF, &clusterHeadMAC[0]);
  if (esp_now_init() != 0) {
    DEBUG_printlnF("ESP-NOW Initialization Failed");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(onESPNowReceive);
  esp_now_register_send_cb(OnDataSent);

  DEBUG_printlnF("ESP-NOW Ready");

  // Setup LoRa
  int state = radio.begin(id.loraFreq, 125.0, 9, 7, RADIOLIB_SX127X_SYNC_WORD, 17, 8, 0);
  if (state == RADIOLIB_ERR_NONE) {
    DEBUG_printlnF("LoRa Initialized Successfully");
  } else {
    DEBUG_printlnF("LoRa Initialization Failed");
    while (true)
      ;
  }

  radio.setDio1Action(LoRaInterruptHandler);
  state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    DEBUG_printlnF("Error starting receive mode!");
    while (true)
      ;
  }
  DEBUG_printlnF("System Ready.");
  ESP.wdtEnable(8000);
}

void loop() {
  ESP.wdtFeed();

  if (operationDone) {
    operationDone = false;
    if (transmitFlag) {
      transmitFlag = false;
      DEBUG_printlnF("Transmission complete.");
      timeOutLoRa = millis();
      messageQueueLora.pop();
      radio.startReceive();
    } else {
      processReceivedLoRa();
    }
  }
  if ((millis() - timeOut > timeoutTimer) || (millis() - timeOutLoRa > timeoutTimer)) {
    if (messageQueueLora.empty() && messageQueueESPNow.empty()) {
      ESP.restart();
    }
  }

  unsigned long currentMillis = millis();

  // Process LoRa messages every 2 seconds
  if (currentMillis - lastTransmitTime >= random(LORA_TRANSMIT_TIMER, LORA_TRANSMIT_TIMER + 3000)) { //5000-8000
    lastTransmitTime = currentMillis;
    if (!messageQueueLora.empty()) {
      sendNextLoRaMessage();
    }
  }

  // Check for inactive clients every 10 seconds
  if (currentMillis - lastClientCheckTime >= 120000) {
    lastClientCheckTime = currentMillis;
    removeInactiveClients();
    removeInactiveESPNowClients();
  }

  checkCommand();
}

void checkCommand() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (inputString.length() > 0) {
        if (inputString.startsWith("{")) {
          parseJSON(inputString);
        } else if (inputString.equalsIgnoreCase("show")) {
          if (loadConfigFromEEPROM()) {
            printConfig();
          } else {
            Serial.println("No config in EEPROM.");
          }
        } else if (inputString.equalsIgnoreCase("reset")) {
          eraseConfig();
          delay(1000);
          ESP.restart(); 
        } else if (inputString.equalsIgnoreCase("DEBUG_ON")) {     
          debugEnabled = true;
          Serial.println("Debugging enabled");
        } else if (inputString.equalsIgnoreCase("DEBUG_OFF")) {
          debugEnabled = false;
          Serial.println("Debugging disabled");
        } else {
          Serial.println("Invalid command. Send JSON, or type 'show' or 'reset' or 'DEBUG_ON' or 'DEBUG_OFF'.");
        }
        inputString = "";
      }
    } else {
      inputString += c;
    }
  }
}

void eraseConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(0, 0x00);  // Clear magic byte
  EEPROM.commit();
  EEPROM.end();
  Serial.println("Config erased from EEPROM.");
}

void parseJSON(String jsonString) {
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, jsonString);

  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    return;
  }
  Serial.println("{\"clusterId\":10,\"networkId\":1,\"toGatewayId\":1,\"toNodeId\":1,\"loraFreq\":921.5}");   
    
  if (doc.containsKey("clusterId") && doc.containsKey("networkId") &&
      doc.containsKey("toGatewayId") && doc.containsKey("toNodeId") &&
      doc.containsKey("loraFreq")) {

    id.clusterId = doc["clusterId"];
    id.networkId = doc["networkId"];
    id.toGatewayId = doc["toGatewayId"];
    id.toNodeId = doc["toNodeId"];
    id.loraFreq = doc["loraFreq"];

    Serial.println("Config loaded from JSON:");
    printConfig();
    saveConfigToEEPROM();
    Serial.println("Rebooting...");
    delay(1000);
    ESP.restart();
  } else {
    Serial.println("Missing required fields in JSON.");
  }
}

void printConfig() {
  Serial.println("== Current Config ==");
  Serial.printf("clusterId: %d\n", id.clusterId);
  Serial.printf("networkId: %d\n", id.networkId);
  Serial.printf("toGatewayId: %d\n", id.toGatewayId);
  Serial.printf("toNodeId: %d\n", id.toNodeId);
  Serial.printf("loraFreq: %d\n", id.loraFreq);
}
// Check if the LoRa channel is clear (using Carrier Activity Detection)
bool isChannelFree() {
  DEBUG_printlnF("Checking channel activity...");
  int state = radio.scanChannel();
  if (state == RADIOLIB_CHANNEL_FREE) {
    DEBUG_println("Channel is free.");
    return true;
  }
  DEBUG_println("Channel is busy.");
  return false;
}

// Function to convert MAC address to string format
String macToStr(const uint8_t *mac) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}

// Function to check if a MAC address belongs to this cluster
bool isLocalClient(const String &macAddress) {
  for (const auto &member : clusterMembers) {
    if (member.macAddress == macAddress) {
      return true;
    }
  }
  return false;
}

// Function to add a new client to the cluster dynamically
void addOrUpdateClient(const uint8_t *mac) {
  String macStr = macToStr(mac);
  bool found = false;

  for (auto &member : clusterMembers) {
    if (member.macAddress == macStr) {
      member.lastSeen = millis();  // Update last seen time
      found = true;
      break;
    }
  }

  if (!found) {
    if (clusterMembers.size() < MAX_INTRA_CLUSTER) {
      clusterMembers.push_back({ macStr, millis() });
      DEBUG_printF("New client added to cluster: ");
      DEBUG_println(macStr);
    } else {
      DEBUG_printlnF("Maximum number of clients reached. Cannot add more.");
    }
  }
}

// Function to remove inactive clients after 1 hour of inactivity
void removeInactiveClients() {
  unsigned long currentMillis = millis();
  unsigned long oneHour = ACTIVE_NODE_TIMER;  // 1 hour in milliseconds

  for (size_t i = 0; i < clusterMembers.size();) {
    if (currentMillis - clusterMembers[i].lastSeen > oneHour) {
      DEBUG_printF("Removing inactive client: ");
      DEBUG_println(clusterMembers[i].macAddress);
      clusterMembers.erase(clusterMembers.begin() + i);  // Remove inactive client
    } else {
      i++;  // Only increment if no deletion happened
    }
  }
}

// ESP-NOW Receive Callback
void onESPNowReceive(uint8_t *mac, uint8_t *incomingData, uint8_t len) {

  if (len != sizeof(struct_message)) {
    DEBUG_printF("len : ");
    DEBUG_println(len);
    DEBUG_printlnF("Invalid ESP-NOW message size.");
    return;
  }
  timeOut = millis();

  struct_message msg;
  memcpy(&msg, incomingData, sizeof(struct_message));
  addOrUpdateClient(mac);
  DEBUG_printF("incomingData from : ");
  DEBUG_println(msg.sourceId);
  String macStr = macToStr(mac);

  std::queue<ClientESPNow> tempQueue;
  bool messageSent = false;

  // Process queued messages
  while (!messageQueueESPNow.empty()) {
    ClientESPNow queuedMsg = messageQueueESPNow.front();
    messageQueueESPNow.pop();

    if (queuedMsg.macAddress == macStr) {
      sendToESPNowClient(queuedMsg.msg, mac);
      messageSent = true;
      DEBUG_printlnF("Queued message sent to ESP-NOW client.");
    } else {
      tempQueue.push(queuedMsg);
    }
  }

  messageQueueESPNow = tempQueue;  // Restore remaining messages

  if (!messageSent) {
    DEBUG_printlnF("No queued messages for this client.");
  }

  // Add message to queue for processing in the main loop
  if (messageQueueLora.size() < MAX_MESSAGE_QUEUE) {  // Max queue size to prevent overflow
    messageQueueLora.push(msg);
    DEBUG_printlnF("Message added to queue.");
  } else {
    DEBUG_printlnF("Message queue full, dropping message.");
  }
}

// Ensure peer exists before sending
bool ensurePeerExists(const uint8_t *peerAddress) {
  if (esp_now_is_peer_exist((uint8_t *)peerAddress)) {
    DEBUG_printlnF("Peer already exists.");
    return true;
  }
  // Always try to add the peer on ESP8266, it handles duplicates internally
  if (esp_now_add_peer((uint8_t *)peerAddress, ESP_NOW_ROLE_COMBO, 0, NULL, 0) != 0) {
    DEBUG_printlnF("Failed to add peer.");
    return false;
  } else {
    DEBUG_printlnF("Peer added successfully.");
    return true;
  }
}

// Callback function to receive send status
void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  DEBUG_printF("ESP-NOW Send Status: ");
  if (sendStatus == 0) {
    DEBUG_printlnF("Delivery Success");
    // Delete the peer after successful transmission
    if (esp_now_del_peer(mac_addr) == 0) {
      DEBUG_printlnF("Peer deleted successfully.");
      timeOut = millis();
    } else {
      DEBUG_printlnF("Failed to delete peer.");
    }
  } else {
    DEBUG_printlnF("Delivery Failed");
  }
}

// Function to send data via ESP-NOW to a local client
void sendToESPNowClient(struct_message data, const uint8_t *peerMac) {
  if (ensurePeerExists(peerMac)) {
    esp_now_send((uint8_t *)peerMac, (uint8_t *)&data, sizeof(data));
    DEBUG_printlnF("Forwarded message to ESP-NOW client.");
  } else {
    DEBUG_printlnF("ESP-NOW send failed, queuing message.");
    if (messageQueueESPNow.size() < MAX_MESSAGE_QUEUE) {
      ClientESPNow queuedMessage;
      String macStr = macToStr(peerMac);
      queuedMessage.macAddress = macStr;
      queuedMessage.msg = data;
      queuedMessage.lastSeen = millis();
      messageQueueESPNow.push(queuedMessage);
      DEBUG_printlnF("Message added to ESPNow queue for retry.");
    } else {
      DEBUG_printlnF("ESP-NOW message queue full, dropping message.");
    }
  }
}

// Function to handle received LoRa data
void processReceivedLoRa() {
  uint8_t buffer[69];
  uint8_t length = radio.getPacketLength();
  int len = radio.readData(buffer, length);



  if (length > 0) {
    struct_message receivedData;
    if (deserializeMessage(buffer, length, &receivedData)) {
      // Get RSSI and other parameters
      int rssi = radio.getRSSI();                        // Get the signal strength (RSSI)
      long snr = radio.getSNR();                         // Get the Signal-to-Noise Ratio (SNR)
      float frequencyError = radio.getFrequencyError();  // Get frequency error

      // Output received data and parameters
      DEBUG_println(millis());
      DEBUG_println("RSSI: ");
      DEBUG_println(rssi);  // RSSI in dBm
      DEBUG_println("SNR: ");
      DEBUG_println(snr);  // SNR in dB
      DEBUG_println("Frequency Error: ");
      DEBUG_println(frequencyError);  // Frequency Error in Hz
      DEBUG_println("----------------------------");
      timeOutLoRa = millis();
      // DEBUG_printf("Received: SourceID: %d, TargetID: %d, MessageSize: %s\n",
      //  receivedData.sourceId, receivedData.targetId, receivedData.messageSize);
      uint8_t peerMac[6] = { 0x24, 0x6F, 0x28, id.networkId, id.clusterId, receivedData.targetId };
      String strMac = macToStr(peerMac);
      if (isLocalClient(strMac)) {

        sendToESPNowClient(receivedData, peerMac);
      } else {
        forwardLoRa(receivedData);
      }
    }
  } else {
    DEBUG_printlnF("Error reading LoRa message.");
  }
}

// Function to forward data to another cluster using LoRa
void forwardLoRa(struct_message data) {

  // Add message to queue for processing in the main loop
  if (messageQueueLora.size() < 10) {  // Max queue size to prevent overflow
    messageQueueLora.push(data);
    DEBUG_printlnF("Message added to queue.");
  } else {
    DEBUG_printlnF("Message queue full, dropping message.");
  }
}

// Function to serialize struct_message to a byte array
uint8_t serializeMessage(struct_message *msg, uint8_t *buffer, int bufferSize) {
  int totalLen = 1 + sizeof(msg->sourceId) + sizeof(msg->targetId) + sizeof(msg->networkId) +  // 1 for LoRa node addressing
                 sizeof(msg->direction) + sizeof(msg->messageSize) + msg->messageSize;         // Calculate actual size
  DEBUG_print("msg size : ");
  DEBUG_println(sizeof(msg));
  DEBUG_print("Buffer Size : ");
  DEBUG_println(bufferSize);
  DEBUG_print("totalLen : ");
  DEBUG_println(totalLen);
  if (totalLen > bufferSize) {
    DEBUG_printlnF("Buffer size too small for serialization.");
    return -1;
  }
  if (msg->direction == TO_GATEWAY) {
    buffer[0] = id.toGatewayId;
  } else {
    if (msg->direction == TO_NODE) {
      buffer[0] = id.toNodeId;
    }
  }

  buffer[1] = msg->sourceId;
  buffer[2] = msg->targetId;
  buffer[3] = msg->networkId;
  buffer[4] = msg->direction;
  buffer[5] = msg->messageSize;  // Store the actual message size

  memcpy(&buffer[6], msg->message, msg->messageSize);  // Copy only actual message content

  return totalLen;  // Return the actual length of the serialized message
}

// Function to deserialize byte array back to struct_message
bool deserializeMessage(uint8_t *buffer, int bufferLen, struct_message *msg) {
  if (bufferLen < 4) {  // Minimum bytes required for metadata
    DEBUG_printlnF("Received buffer too small for deserialization.");
    return false;
  }

  if (buffer[0] != id.clusterId) {
    // DEBUG_printlnF("not for this clusterId.");
    return false;
  }
  msg->sourceId = buffer[1];
  msg->targetId = buffer[2];
  msg->networkId = buffer[3];
  if (msg->networkId != id.networkId) {
    DEBUG_printlnF("not for this networkId.");
    return false;
  }
  msg->direction = buffer[4];
  msg->messageSize = buffer[5];

  if (msg->messageSize > 80 || bufferLen < (5 + msg->messageSize)) {
    DEBUG_printlnF("Received message exceeds buffer limit.");
    return false;
  }

  memcpy(msg->message, &buffer[6], msg->messageSize);
  msg->message[msg->messageSize] = '\0';  // Ensure null termination

  return true;
}

// Modified function to send LoRa messages with collision avoidance
void sendNextLoRaMessage() {
  if (!messageQueueLora.empty()) {
    unsigned long startTime = millis();
    bool channelFree = false;

    // Wait for channel to be free or timeout
    // while (!channelFree && (millis() - startTime < LORA_CAD_TIMEOUT)) {
    //   channelFree = isChannelFree();
    //   delay(10);
    // }

    if (true) {
      struct_message msg = messageQueueLora.front();
      uint8_t buffer[80];
      int len = serializeMessage(&msg, buffer, sizeof(buffer));
      if (len > 0) {
        int state = radio.startTransmit(buffer, len);
        if (state == RADIOLIB_ERR_NONE) {
          DEBUG_printlnF("LoRa transmission started...");
          transmitFlag = true;
        } else {
          DEBUG_printlnF("LoRa transmission failed!");
        }
      }
    } else {
      // Channel busy: Wait random backoff time before retrying
      long backoffTime = random(100, MAX_BACKOFF_MS);
      DEBUG_printf("Channel busy. Backing off for %ld ms.\n", backoffTime);
      delay(backoffTime);
    }
  }
}

// Load idConfig from EEPROM
bool loadConfigFromEEPROM() {

  EEPROM.begin(EEPROM_SIZE);
  if (EEPROM.read(0) != MAGIC_BYTE) {
    EEPROM.end();
    return false;
  }
  EEPROM.get(1, id);
  EEPROM.end();

  clusterHeadMAC[3] = id.networkId;
  clusterHeadMAC[4] = id.clusterId;
  DEBUG_println();
  DEBUG_printF("clusterHeadMAC   : ");
  DEBUG_println(macToStr(clusterHeadMAC));
  DEBUG_printF("LoRaNodeID       : ");
  DEBUG_println(id.clusterId);
  DEBUG_printF("LoRa toNodeID    : ");
  DEBUG_println(id.toNodeId);
  DEBUG_printF("LoRa toGatewayId : ");
  DEBUG_println(id.toGatewayId);
  DEBUG_printF("LoRa Frequency   : ");
  DEBUG_println(id.loraFreq);
  return true;
}

void saveConfigToEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(0, MAGIC_BYTE);
  EEPROM.put(1, id);
  EEPROM.commit();
  EEPROM.end();
  Serial.println("Config saved to EEPROM.");
}

void removeInactiveESPNowClients() {
  unsigned long currentMillis = millis();
  std::queue<ClientESPNow> tempQueue;

  while (!messageQueueESPNow.empty()) {
    ClientESPNow queuedMsg = messageQueueESPNow.front();
    messageQueueESPNow.pop();

    if (currentMillis - queuedMsg.lastSeen > ACTIVE_NODE_TIMER) {
      DEBUG_printF("Removing inactive ESP-NOW client: ");
      DEBUG_println(queuedMsg.macAddress);
    } else {
      tempQueue.push(queuedMsg);
    }
  }

  messageQueueESPNow = tempQueue;  // Restore non-expired messages
}
