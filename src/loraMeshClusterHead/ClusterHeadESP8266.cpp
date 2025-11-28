#include <pgmspace.h>
#include <ESP8266WiFi.h>  // ESP8266,
#include <espnow.h>       // ESP8266
#include <RadioLib.h>
#include <vector>
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
#define LORA_TRANSMIT_TIMER 2000
#define EEPROM_SIZE 512  // Define EEPROM size based on need
#define MAX_MESSAGE_QUEUE 10
#define LED 2
#define CONFIG_PIN 0
#define MAX_PAYLOAD_SIZE 64
#define CTRLWORD 0x60
#define LORA_CAD_TIMEOUT 1000  // Channel Activity Detection timeout (ms)
#define MAX_BACKOFF_MS 3000    // Maximum random backoff time
const unsigned long timeoutTimer = 5UL * 60UL * 1000UL;
#define MAGIC_BYTE 0xA6

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
IRAM_ATTR void LoRaInterruptHandler() {
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

String macToStr(const uint8_t* mac);
bool isLocalClient(const String& macAddress);  //
void addOrUpdateClient(const uint8_t* mac);
void removeInactiveClients();
void OnDataSent(uint8_t* mac_addr, uint8_t sendStatus);
void onESPNowReceive(uint8_t* mac, uint8_t* incomingData, uint8_t len);
void sendToESPNowClient(struct_message data, const uint8_t* peerMac);
void forwardLoRa(struct_message data);
void processReceivedLoRa();
uint8_t serializeMessage(struct_message* msg, uint8_t* buffer, int bufferSize);
bool deserializeMessage(uint8_t* buffer, int bufferLen, struct_message* msg);  // filtering
void sendNextLoRaMessage();
bool ensurePeerExists(const uint8_t* peerAddress);
void saveConfigToEEPROM();
bool loadConfigFromEEPROM();
void removeInactiveESPNowClients();
bool isChannelFree();  // lora
void eraseConfig();
void parseJSON(String jsonString);
void printConfig();
void checkCommand();

// Fungsi pembantu untuk membuat garis judul debug yang simetris
String makeDebugTitle(const char* title) {
  const int totalLen = 49; // total panjang baris
  String t = String(title);
  int midLen = t.length() + 4; // [  ]
  int dash = (totalLen - midLen) / 2;
  String line = "";
  for (int i = 0; i < dash; i++) line += "-";
  line += "[  " + t + "  ]";
  while (line.length() < totalLen) line += "-";
  return line;
}

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

  // Setup LoRa
  int state = radio.begin(id.loraFreq, 125.0, 9, 7, RADIOLIB_SX127X_SYNC_WORD, 17, 8, 0);
  if (state == RADIOLIB_ERR_NONE) {
    DEBUG_printlnF("LoRa Initialized Successfully");
  }
  else {
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
  // ESP.wdtEnable(8000);
}

void loop() {
  if (operationDone) {
    operationDone = false;
    if (transmitFlag) {
      transmitFlag = false;
      DEBUG_printlnF("Transmission complete.");
      timeOutLoRa = millis();
      messageQueueLora.pop();
      radio.startReceive();
    }
    else {
      processReceivedLoRa();
    }
  }
  if ((millis() - timeOutLoRa > timeoutTimer)) {
    if (messageQueueLora.empty()) {
      ESP.restart();
    }
  }

  unsigned long currentMillis = millis();
  if (currentMillis - lastTransmitTime >= (unsigned long)random(LORA_TRANSMIT_TIMER, LORA_TRANSMIT_TIMER + 3000)) {
    lastTransmitTime = currentMillis;
    if (!messageQueueLora.empty()) {
      sendNextLoRaMessage();
    }
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
        }
        else if (inputString.equalsIgnoreCase("show")) {
          if (loadConfigFromEEPROM()) {
            printConfig();
          }
          else {
            Serial.println("No config in EEPROM.");
          }
        }
        else if (inputString.equalsIgnoreCase("reset")) {
          eraseConfig();
          delay(1000);
          ESP.restart();
        }
        else if (inputString.equalsIgnoreCase("DEBUG_ON")) {
          debugEnabled = true;
          Serial.println("Debugging enabled");
        }
        else if (inputString.equalsIgnoreCase("DEBUG_OFF")) {
          debugEnabled = false;
          Serial.println("Debugging disabled");
        }
        else if (inputString.equalsIgnoreCase("show member")) {
          Serial.println("== Cluster Members ==");
          for (size_t i = 0; i < clusterMembers.size(); i++) {
            Serial.print(i); Serial.print(": ");
            Serial.print(clusterMembers[i].macAddress);
            Serial.print(" (lastSeen: ");
            Serial.print(clusterMembers[i].lastSeen);
            Serial.println(")");
          }
        }
        else if (inputString.equalsIgnoreCase("delete member")) {
          clusterMembers.clear();
  
          Serial.println("All cluster members deleted.");
        }
        else {
          Serial.println("Invalid command. Send JSON, or type 'show' or 'reset' or 'DEBUG_ON' or 'DEBUG_OFF'.");
        }
        inputString = "";
      }
    }
    else {
      inputString += c;
    }
  }
}

void eraseConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(0, 0x00);
  EEPROM.commit();
  EEPROM.end();
  Serial.println("Config erased from EEPROM.");
}

void parseJSON(String jsonString) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, jsonString);

  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    return;
  }
  Serial.println("{\"clusterId\":10,\"networkId\":1,\"toGatewayId\":1,\"toNodeId\":1,\"loraFreq\":921}");

  if (!doc["clusterId"].isNull() && !doc["networkId"].isNull() &&
    !doc["toGatewayId"].isNull() && !doc["toNodeId"].isNull() &&
    !doc["loraFreq"].isNull()) {
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
  }
  else {
    Serial.println("Missing required fields in JSON.");
  }
}

void printConfig() {
  Serial.println("== Current Config ==");
  Serial.printf("clusterId: %d\n", id.clusterId);
  Serial.printf("networkId: %d\n", id.networkId);
  Serial.printf("toGatewayId: %d\n", id.toGatewayId);
  Serial.printf("toNodeId: %d\n", id.toNodeId);
  Serial.printf("loraFreq: %.1f\n", id.loraFreq);
}

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

String macToStr(const uint8_t* mac) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}

bool isLocalClient(const String& macAddress) {
  for (const auto& member : clusterMembers) {
    if (member.macAddress == macAddress) {
      return true;
    }
  }
  return false;
}



void removeInactiveClients() {
  unsigned long currentMillis = millis();
  unsigned long oneHour = ACTIVE_NODE_TIMER;

  for (size_t i = 0; i < clusterMembers.size();) {
    if (currentMillis - clusterMembers[i].lastSeen > oneHour) {
      clusterMembers.erase(clusterMembers.begin() + i);
    }
    else {
      i++;
    }
  }
}

void onESPNowReceive(uint8_t* mac, uint8_t* incomingData, uint8_t len) {
  timeOut = millis();

  struct_message msg;
  memcpy(&msg, incomingData, sizeof(struct_message));
  addOrUpdateClient(mac);

  String macStr = macToStr(mac);

  std::queue<ClientESPNow> tempQueue;

  while (!messageQueueESPNow.empty()) {
    ClientESPNow queuedMsg = messageQueueESPNow.front();
    messageQueueESPNow.pop();

    if (queuedMsg.macAddress == macStr) {
      sendToESPNowClient(queuedMsg.msg, mac);
      DEBUG_printlnF("Queued message sent to ESP-NOW client.");
    }
    else {
      tempQueue.push(queuedMsg);
    }
  }

  messageQueueESPNow = tempQueue;

  if (messageQueueLora.size() < MAX_MESSAGE_QUEUE) {
    lastTransmitTime = millis();
    messageQueueLora.push(msg);
    DEBUG_printlnF("Message added to queue.");
  }
  else {
    DEBUG_printlnF("Message queue full, dropping message.");
  }
}

bool ensurePeerExists(const uint8_t* peerAddress) {
  if (esp_now_is_peer_exist((uint8_t*)peerAddress)) {
    DEBUG_printlnF("Peer already exists.");
    return true;
  }
  if (esp_now_add_peer((uint8_t*)peerAddress, ESP_NOW_ROLE_COMBO, 0, NULL, 0) != 0) {
    DEBUG_printlnF("Failed to add peer.");
    return false;
  }
  else {
    DEBUG_printlnF("Peer added successfully.");
    return true;
  }
}

void OnDataSent(uint8_t* mac_addr, uint8_t sendStatus) {
  DEBUG_printF("ESP-NOW Send Status: ");
  if (sendStatus == 0) {
    DEBUG_printlnF("Delivery Success");
    if (esp_now_del_peer(mac_addr) == 0) {
      DEBUG_printlnF("Peer deleted successfully.");
      timeOut = millis();
    }
    else {
      DEBUG_printlnF("Failed to delete peer.");
    }
  }
  else {
    DEBUG_printlnF("Delivery Failed");
  }
}

void sendToESPNowClient(struct_message data, const uint8_t* peerMac) {
  if (ensurePeerExists(peerMac)) {
    esp_now_send((uint8_t*)peerMac, (uint8_t*)&data, sizeof(data));
    DEBUG_printlnF("Forwarded message to ESP-NOW client.");
  }
  else {
    DEBUG_printlnF("ESP-NOW send failed.");
  }
}

void processReceivedLoRa() {
  uint8_t buffer[69];
  uint8_t length = radio.getPacketLength();
  int len = radio.readData(buffer, length);
  if (len < 0) {
    DEBUG_printlnF("Error reading LoRa data.");
    return;
  }
  if (length > 0) {
    struct_message receivedData;
    lastTransmitTime = millis();
    if (deserializeMessage(buffer, length, &receivedData)) {
      int rssi = radio.getRSSI();
      long snr = radio.getSNR();
      float frequencyError = radio.getFrequencyError();

      DEBUG_print("RSSI: ");
      DEBUG_print(rssi);
      DEBUG_print(" - SNR: ");
      DEBUG_print(snr);
      DEBUG_print(" - Frequency Error: ");
      DEBUG_print(frequencyError);
      DEBUG_println("");
      timeOutLoRa = millis();
      uint8_t peerMac[6] = { 0x24, 0x6F, 0x28, id.networkId, id.clusterId, receivedData.targetId };
      String strMac = macToStr(peerMac);
      forwardLoRa(receivedData);
    }
  }
  else {
    DEBUG_printlnF("Error reading LoRa message.");
  }
}

void forwardLoRa(struct_message data) {
  if (messageQueueLora.size() < 10) {
    messageQueueLora.push(data);
    DEBUG_printlnF("Message added to queue.");
  }
  else {
    DEBUG_printlnF("Message queue full, dropping message.");
  }
}

uint8_t serializeMessage(struct_message* msg, uint8_t* buffer, int bufferSize) {
  int totalLen = 1 + sizeof(msg->sourceId) + sizeof(msg->targetId) + sizeof(msg->networkId) +
    sizeof(msg->direction) + sizeof(msg->messageSize) + msg->messageSize;
  if (totalLen > bufferSize) {
    DEBUG_printlnF("Buffer size too small for serialization.");
    return -1;
  }
  if (msg->direction == TO_GATEWAY) {
    buffer[0] = id.toGatewayId;
  }
  else {
    if (msg->direction == TO_NODE) {
      buffer[0] = id.toNodeId;
    }
  }

  buffer[1] = msg->sourceId;
  buffer[2] = msg->targetId;
  buffer[3] = msg->networkId;
  buffer[4] = msg->direction;
  buffer[5] = msg->messageSize;

  memcpy(&buffer[6], msg->message, msg->messageSize);

  return totalLen;
}

bool deserializeMessage(uint8_t* buffer, int bufferLen, struct_message* msg) {
  if (bufferLen < 4) {
    DEBUG_printlnF("Received buffer too small for deserialization.");
    return false;
  }
  //print all buffer
  DEBUG_print("Received buffer: ");
  for (int i = 0; i < bufferLen; i++) {
    DEBUG_print(buffer[i]);
    DEBUG_print(" ");
  }
  DEBUG_println("");

  if (buffer[0] != id.clusterId) {
    return false;
  }
  else {
    DEBUG_printlnF("for this clusterId.");
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
  msg->message[msg->messageSize] = '\0';
  return true;
}

void sendNextLoRaMessage() {
  if (!messageQueueLora.empty()) {
    struct_message msg = messageQueueLora.front();
    uint8_t buffer[80];
    int len = serializeMessage(&msg, buffer, sizeof(buffer));
    if (len > 0) {
      int state = radio.startTransmit(buffer, len);
      if (state == RADIOLIB_ERR_NONE) {
        DEBUG_printlnF("LoRa transmission started...");
        transmitFlag = true;
      }
      else {
        DEBUG_printlnF("LoRa transmission failed!");
      }
    }
  }
}

bool loadConfigFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  if (EEPROM.read(0) != MAGIC_BYTE) {
    EEPROM.end();
    return false;
  }
  EEPROM.get(1, id);
  EEPROM.end();

  DEBUG_println();
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
    }
    else {
      tempQueue.push(queuedMsg);
    }
  }

  messageQueueESPNow = tempQueue;
}


