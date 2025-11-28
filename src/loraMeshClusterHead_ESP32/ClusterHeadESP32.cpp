#include <Arduino.h>
#include <pgmspace.h>
#include <WiFi.h>
#include <esp_now.h> 
#include <esp_wifi.h>
#include <RadioLib.h>
#include <vector>
#include <EEPROM.h>
#include <queue>
#include <ArduinoJson.h>
// LoRa Module Pins (LLCC68)
#define LORA_SS 5
#define LORA_RST 4
#define LORA_DIO1 25
#define LORA_BUSY 2

#define LORA_MOSI 23  // GPIO7
#define LORA_MISO 19  // GPIO2
#define LORA_SCK  18  // GPIO6

LLCC68 radio = new Module(LORA_SS, LORA_DIO1, LORA_RST, LORA_BUSY);

#define TO_GATEWAY 0
#define TO_NODE 1
#define LORA_TRANSMIT_TIMER 2500
#define EEPROM_SIZE 512  // Define EEPROM size based on need
#define MAX_MESSAGE_QUEUE 10
#define LED 2
#define CONFIG_PIN 0
#define MAX_PAYLOAD_SIZE 64

const unsigned long timeoutTimer = 60UL * 60UL * 1000UL;
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
IRAM_ATTR void LoRaInterruptHandler() {
  operationDone = true;
}

typedef struct idConfig {
  uint8_t clusterId;
  uint8_t networkId;
  uint8_t toGatewayId;
  uint8_t toNodeId;
  float loraFreq;
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


String macToStr(const uint8_t* mac);
void forwardLoRa(struct_message data);
void processReceivedLoRa();
uint8_t serializeMessage(struct_message* msg, uint8_t* buffer, int bufferSize);
bool deserializeMessage(uint8_t* buffer, int bufferLen, struct_message* msg);  // filtering
void sendNextLoRaMessage();
void saveConfigToEEPROM();
bool loadConfigFromEEPROM();
void eraseConfig();
void parseJSON(String jsonString);
void printConfig();
void checkCommand();

void setup() {
  delay(5000);
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
    DEBUG_printlnF("{\"clusterId\":1,\"networkId\":1,\"toGatewayId\":255,\"toNodeId\":0,\"loraFreq\":921.0}");
    while (1) {
      checkCommand();
      delay(10);
    }
    delay(1000);
    ESP.restart();
  }else{
    Serial.println("Config loaded from EEPROM.");
    printConfig();
  }
  
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
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
  if (currentMillis - lastTransmitTime >= (unsigned long)random(LORA_TRANSMIT_TIMER, LORA_TRANSMIT_TIMER + 3000)) { //random antara 2500-5500ms
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
        else if (inputString.equalsIgnoreCase("restart"))
        {
          ESP.restart();
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

  if (!doc["clusterId"].isNull()) id.clusterId = doc["clusterId"];
  if (!doc["networkId"].isNull()) id.networkId = doc["networkId"];
  if (!doc["toGatewayId"].isNull()) id.toGatewayId = doc["toGatewayId"];
  if (!doc["toNodeId"].isNull()) id.toNodeId = doc["toNodeId"];
  if (!doc["loraFreq"].isNull()) id.loraFreq = doc["loraFreq"];

  saveConfigToEEPROM();
  Serial.println("Config loaded & saved. Rebooting...");
  delay(1000);
  ESP.restart();
}

void printConfig() {
  DEBUG_println("== Current Config ==");
  DEBUG_printf("clusterId: %d\n", id.clusterId);
  DEBUG_printf("networkId: %d\n", id.networkId);
  DEBUG_printf("toGatewayId (legacy): %d\n", id.toGatewayId);
  DEBUG_printf("toNodeId (legacy): %d\n", id.toNodeId);
  DEBUG_printf("loraFreq: %.2f\n", id.loraFreq);

  // show queue sizes and pending state for quick diagnostics
  
}

String macToStr(const uint8_t* mac) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
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
      if(receivedData.targetId == id.clusterId){
        receivedData.direction = TO_GATEWAY;
        forwardLoRa(receivedData);  
      }else{
        forwardLoRa(receivedData);
      }
      
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
// Format pesan:
// [0] msgId (3-digit millis)
// [1] nextClusterHead (toGatewayId)
// [2] sourceId
// [3] targetId 
// [4] networkId
// [5] direction
// [6] messageSize
// [7..7+messageSize-1] message payload

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

  // Validate / clamp values (simple safety checks)
  if (id.networkId == 0) id.networkId = 1;
  if (id.clusterId == 0) id.clusterId = 1;
  if (id.toGatewayId == 0) id.toGatewayId = 255;
  if (id.toNodeId == 0) id.toNodeId = 0;
  if (id.loraFreq < 800.0f || id.loraFreq > 950.0f) id.loraFreq = 921.0f;

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

#define CLUSTER_MEMBER_EEPROM_ADDR 100
#define MAX_MEMBER_NAME_LEN 18

void saveClusterMembersToEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t count = clusterMembers.size();
  EEPROM.write(CLUSTER_MEMBER_EEPROM_ADDR, count);
  int addr = CLUSTER_MEMBER_EEPROM_ADDR + 1;
  for (uint8_t i = 0; i < count; i++) {
    for (uint8_t j = 0; j < MAX_MEMBER_NAME_LEN; j++) {
      char c = (j < clusterMembers[i].macAddress.length()) ? clusterMembers[i].macAddress[j] : 0;
      EEPROM.write(addr++, c);
    }
    unsigned long lastSeen = clusterMembers[i].lastSeen;
    for (uint8_t b = 0; b < 4; b++) {
      EEPROM.write(addr++, (lastSeen >> (8 * b)) & 0xFF);
    }
  }
  EEPROM.commit();
  EEPROM.end();
  DEBUG_println("Cluster members saved to EEPROM.");
}

void loadClusterMembersFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t count = EEPROM.read(CLUSTER_MEMBER_EEPROM_ADDR);
  int addr = CLUSTER_MEMBER_EEPROM_ADDR + 1;
  for (uint8_t i = 0; i < count; i++) {
    char macStr[MAX_MEMBER_NAME_LEN];
    for (uint8_t j = 0; j < MAX_MEMBER_NAME_LEN; j++) {
      macStr[j] = EEPROM.read(addr++);
    }
    String macAddress = String(macStr);
    unsigned long lastSeen = 0;
    for (uint8_t b = 0; b < 4; b++) {
      lastSeen |= ((unsigned long)EEPROM.read(addr++)) << (8 * b);
    }
    clusterMembers.push_back({ macAddress, lastSeen });
  }
  EEPROM.end();
  DEBUG_println("Cluster members loaded from EEPROM.");
}

