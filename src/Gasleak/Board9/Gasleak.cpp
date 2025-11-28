#include <Arduino.h>
#include <Wire.h>
#include <TCA9548.h>
#include <MCP4725.h>
#include <SPI.h>
#include <ADS1256.h>
#include <EEPROM.h>
#include <CayenneLPP.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <RadioLib.h>
#include "NeuralNetwork.h"
#include "scaler_params.h"
#include <vector>
#include <cmath>
#include <movingAvgFloat.h>
#include <pgmspace.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <queue>
// Channel 0 MQ8
// Channel 1 MQ135
// Channel 2 MQ3
// Channel 3 MQ5
// Channel 4 MQ4
// Channel 5 MQ7
// Channel 6 MQ6
// Channel 7 MQ2

// #region DEFINE & CONST
#define TO_GATEWAY 0
#define TO_NODE 1
#define CAT5171_ADDR 0x2C
#define TCA_ADDR 0x71
#define dacAddress 0x60
#define intervalTakeData 10
#define intervalMQTT 500
#define intervalLoRa 100
#define intervalMachineLearning 200
#define LORA_RST 38
#define LORA_DIO1 1
#define LORA_BUSY 39
#define IDCONFIG_EEPROM_ADDR 0
#define ThresholdCalibration_V 0.001f
#define GainForCalibration PGA_64 
#define movingaveragelength 5
#define MAX_PAYLOAD_SIZE 64
#define EEPROM_SIZE 512
#define AUTO_TRANSMIT 1
#define LORA_TRANSMIT_TIMER 2500
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

#define CTRL_WORD_VALUE 0xA3
#define PIN_MOSI_ADS  7
#define PIN_MISO_ADS  6
#define PIN_SCK_ADS   18
#define PIN_CS_ADS    16
#define PIN_DRDY_ADS  17

#define PIN_MOSI_LORA 11
#define PIN_MISO_LORA 13
#define PIN_SCK_LORA  12
#define PIN_CS_LORA   10

const int PIN_DRDY = 17;
const int PIN_RESET = 0;
const int PIN_SYNC = 15;
const int I2C_SDA_PIN = 8;
const int I2C_SCL_PIN = 9;
const int RST_I2C_PIN = 5;
const float V_REF = 2.495f;

// #region STRUCT & TYPEDEF
typedef struct idConfig {
  uint8_t ctrlword;
  uint8_t clusterId;    //ESPNow ID, to receive and transmit data in intra-cluster, also LoRa node ID
  uint8_t networkId;    //ESPNow netwokID
  uint8_t targetId;     //LoRa neighbor bound to node (legacy)
  uint8_t mode;         //Operation mode
  uint8_t periode;
  uint8_t toGatewayId;  // legacy single gateway id (kept for backward compat)
  uint8_t toNodeId;     // legacy single node id (kept for backward compat)
  float loraFreq;       //LoRa ferquency
  uint16_t wiperArr[8]; // Array to store wiper values for 8 channels
} idConfig;

typedef struct struct_message {
  uint8_t sourceId;  // ID of the sender
  uint8_t targetId;  // ID of the recipient
  uint8_t networkId;
  uint8_t direction;                  // 0 = toGateway, 1 = toNode
  uint8_t messageSize;                // Actual size of the message
  uint8_t message[MAX_PAYLOAD_SIZE];  // Message content (max 64 bytes)
} struct_message;

// #region GLOBAL VARIABLE
idConfig id;
struct_message incomingData;
struct_message outgoingData;
const int PGA_SETTING = PGA_1;
const int DRATE_SETTING = DRATE_1000SPS;
long lastTransmit;
uint8_t currentMode = 0;
bool setupMQTTDone = false;
bool sendSta = false;
bool debugEnabled = true;
uint8_t modeSPI = 0; // 0=ADS1256, 1=LoRa
unsigned long timeOut = millis();
unsigned long timeOutLoRa = millis();
String inputString = "";
uint8_t ch = 0;
volatile bool transmitFlag = false;
volatile bool operationDone = false;
long lastTransmitTime = 0;
long millisMQTT = 0;
long millisTakeData = 0;
long millisMachineLearning = 0;
const unsigned long timeoutTimer = 60UL * 60UL * 1000UL;

long adcValues[8];
float voltValues[8];
float voltValuesPrev[8] = { 0 };
uint8_t pgaValues[8] = { PGA_SETTING, PGA_SETTING, PGA_SETTING, PGA_SETTING, PGA_SETTING, PGA_SETTING, PGA_SETTING, PGA_SETTING };
int pgaCount[8] = { 2, 2, 2, 2, 2, 2, 2, 2 };
const uint8_t pga_settings[] = {
    PGA_1,//0000
    PGA_2,//0001
    PGA_4,//0010
    PGA_8,//0011
    PGA_16,//0100
    PGA_32,//0101
    PGA_64 };//0110

uint16_t wiperArr[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
float scaled_sensor_data[8];
int predicted_class = -1;
float highest_confidence_score = 0.0f;
unsigned long start_time = 0, end_time = 0, inference_time = 0;
bool needCalibration = false;
// WiFi & MQTT
const char* WIFI_SSID = "TP-Link_5608";
const char* WIFI_PASS = "54947023";
const char* MQTT_SERVER = "192.168.0.101";
const int MQTT_PORT = 1883;
const char* MQTT_USER = "deviot";
const char* MQTT_PASS = "deviot";
const char* MQTT_TOPIC_MQ = "TransferBoard/MQ";
const char* MQTT_TOPIC_ML = "TransferBoard/ML";
const char* MQTT_TOPIC_MQCMD = "TransferBoard/MQCMD";
// #endregion

// #region OBJECT INSTANTIATION
// ----------- OBJEK SPI -----------
std::queue<struct_message> messageQueueLora;
SPIClass SPI_ADS(HSPI);   // gunakan hardware SPI3
SPIClass SPI_LORA(FSPI);  // gunakan hardware SPI2
LLCC68 radio = new Module(PIN_CS_LORA, LORA_DIO1, LORA_RST, LORA_BUSY);
WiFiClient espClient;
PubSubClient mqttClient(espClient);
ADS1256 A(PIN_DRDY_ADS, PIN_RESET, PIN_SYNC, PIN_CS_ADS, V_REF, &SPI_ADS);
TCA9548 mux(TCA_ADDR);
MCP4725 dac(dacAddress);
NeuralNetwork* network;
movingAvgFloat avgVolt0(movingaveragelength);
movingAvgFloat avgVolt1(movingaveragelength);
movingAvgFloat avgVolt2(movingaveragelength);
movingAvgFloat avgVolt3(movingaveragelength);
movingAvgFloat avgVolt4(movingaveragelength);
movingAvgFloat avgVolt5(movingaveragelength);
movingAvgFloat avgVolt6(movingaveragelength);
movingAvgFloat avgVolt7(movingaveragelength);
// #endregion

// #region FUNCTION DECLARATION
uint8_t serializeMessage(struct_message* msg, uint8_t* buffer, int bufferSize);
void gainCalibrate(uint8_t channel);
uint16_t binarySearchVisual(uint16_t low, uint16_t high);
uint16_t max_uint16(uint16_t a, uint16_t b);
uint16_t min_uint16(uint16_t a, uint16_t b);
String macToStr(const uint8_t* mac);
void parseJSON(String jsonString);
void selectAnalogChannel(uint8_t channel);
void preheatSensorDelta(float threshold_mV);
void setAllWiper(uint16_t wiperValue);
String readAllChannels(bool debugPrint, bool readMQ);
bool deserializeMessage(uint8_t* buffer, int bufferLen, struct_message* msg);
void callback(char* topic, byte* payload, unsigned int length);
void takeDataMQ(uint8_t ch);
void setWiperFromWiperArray(uint8_t ch);
void forwardLoRa(struct_message data);
void checkADS1256();
void prepareDataToSend();
void saveConfigToEEPROM();
bool loadConfigFromEEPROM();
bool checkSerialConfigCommand();
void eraseConfig();
void printConfig();
void connectWiFiAndMQTT();
void modeRunning();
void modeTraining();
void LoRaLoop();
void machineLearning();
void processReceivedLoRa();
void modeSetupCalibration();
void reconnectMQTT();
void publishToMQTT();
void publishToMQTTMachineLearning();
void IRAM_ATTR LoRaInterruptHandler();
void i2cScanner();
void StartADS();
void StartLoRa();
void sendNextLoRaMessage();
// #endregion
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
void i2cScanner() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  byte error, address;
  int nDevices = 0;

  for (address = 96; address <= 113; address++) {
    if (address != TCA_ADDR && address != dacAddress) // Lewati alamat TCA9548 dan MCP4725
      continue;
    else {
      Serial.print(" 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      Serial.print("... ");

      Wire.beginTransmission(address);
      error = Wire.endTransmission();
      Serial.print(": ");
      Serial.print(error);
      Serial.println();
      if (error == 0) {
        nDevices++;
      }
      else if (error == 4) {
        Serial.print("!! Kesalahan tak diketahui di alamat 0x");
        if (address < 16) Serial.print("0");
        Serial.println(address, HEX);
      }
    }

  }

  if (nDevices == 0)
    Serial.println("Tidak ada perangkat I2C ditemukan.");
  else
    Serial.println("Pencarian selesai.");
}
void gainCalibrate(uint8_t channel) {
  A.setMUX(SING_0 + (channel * 16));
  long raw = A.readSingle();
  float absVolt = abs(A.convertToVoltage(raw));
  int change_pga_index = 0;

  // urutan dari 64 → 1 (2^6 → 2^0)
  for (int i = 6; i >= 0; i--) {
    if (absVolt < V_REF / (1 << i)) {
      change_pga_index = i;
      break;
    }
  }

  if (pgaValues[channel] != change_pga_index) {
    pgaValues[channel] = change_pga_index;
    A.setPGA(pga_settings[pgaValues[channel]]);
    delay(20);
    A.readSingle();
  }
}
uint16_t max_uint16(uint16_t a, uint16_t b) {
  return (a > b) ? a : b;
}

uint16_t min_uint16(uint16_t a, uint16_t b) {
  return (a < b) ? a : b;
}
void checkADS1256()
{
  uint8_t id = A.readRegister(0x00);
  Serial.print("ADS1256 ID: 0x");
  Serial.println(id, HEX);

  // ID valid biasanya 0x03 atau 0x36
  if (id != 0x03 && id != 0x36)
  { // jika 0x36, itu versi lain dari ADS1256
    Serial.println("❌ ADS1256 not detected! Check connections.");
    // while (1)
    //   delay(1000);
  }

  Serial.println("✅ ADS1256 detected.");
}String macToStr(const uint8_t* mac)
{
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}
bool ensurePeerExists(const uint8_t* peerAddress)
{
  if (esp_now_is_peer_exist(peerAddress))
  {
    DEBUG_println("Peer already exists.");
    return true;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerAddress, 6);
  peerInfo.channel = 0;     // Default channel
  peerInfo.encrypt = false; // Non-encrypted communication

  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    DEBUG_println("Failed to add peer.");
    return false;
  }
  else
  {
    DEBUG_println("Peer added successfully.");
    return true;
  }
}
void eraseConfig()
{
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(0, 0x00); // Clear magic byte
  EEPROM.commit();
  EEPROM.end();
  DEBUG_println("Config erased from EEPROM.");
}
void printConfig() {
  DEBUG_println("== Current Config ==");
  DEBUG_printf("clusterId: %d\n", id.clusterId);
  DEBUG_printf("networkId: %d\n", id.networkId);
  DEBUG_printf("targetId: %d\n", id.targetId);
  DEBUG_printf("mode: %d\n", id.mode);
  DEBUG_printf("periode: %d\n", id.periode);
  DEBUG_printf("toGatewayId: %d\n", id.toGatewayId);
  DEBUG_printf("toNodeId: %d\n", id.toNodeId);
  DEBUG_printf("loraFreq: %.2f\n", id.loraFreq);
  //print wiper
  DEBUG_println("Wiper Values:");
  for (int i = 0; i < 8; i++) {
    DEBUG_printf(" Channel %d: %d\n", i, id.wiperArr[i]);
  }

}
void parseJSON(String jsonString) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, jsonString);

  if (error) {
    DEBUG_print("JSON parse error: ");
    DEBUG_println(error.c_str());
    return;
  }
  // check required common fields (still allow older JSON but prefer new names)
  // {clusterId:51,networkId:10,targetId:30,mode:0,periode:60,toGatewayId:40,toNodeId:61,loraFreq:921}
  if (!doc["clusterId"].isNull() &&
    !doc["networkId"].isNull() &&
    !doc["targetId"].isNull() &&
    !doc["mode"].isNull() &&
    !doc["periode"].isNull() &&
    !doc["loraFreq"].isNull() &&
    !doc["toGatewayId"].isNull() &&
    !doc["toNodeId"].isNull()
    ) {

    id.ctrlword = CTRL_WORD_VALUE;
    id.clusterId = doc["clusterId"];
    id.networkId = doc["networkId"];
    id.targetId = doc["targetId"];
    id.mode = doc["mode"];
    id.periode = doc["periode"];
    id.loraFreq = doc["loraFreq"];
    id.toGatewayId = doc["toGatewayId"];
    id.toNodeId = doc["toNodeId"];

    DEBUG_println("Config loaded from JSON:");
    printConfig();
    saveConfigToEEPROM();
    DEBUG_println("Rebooting...");
    delay(2000);
    ESP.restart();
  }
  else {
    //sebutkan apa yg mising
    DEBUG_printF("JSON missing fields: ");
    if (doc["clusterId"].isNull()) DEBUG_printF("clusterId ");
    if (doc["networkId"].isNull()) DEBUG_printF("networkId ");
    if (doc["targetId"].isNull()) DEBUG_printF("targetId ");
    if (doc["mode"].isNull()) DEBUG_printF("mode ");
    if (doc["periode"].isNull()) DEBUG_printF("periode ");
    if (doc["loraFreq"].isNull()) DEBUG_printF("loraFreq ");
    DEBUG_println("");
    DEBUG_println("Missing required fields in JSON.");
  }
}
bool checkSerialConfigCommand()
{
  while (Serial.available())
  {
    char c = Serial.read();
    if (c == '\n' || c == '\r')
    {
      if (inputString.length() > 0)
      {
        if (inputString.startsWith("{"))
        {
          parseJSON(inputString);
        }
        else if (inputString.equalsIgnoreCase("show"))
        {
          if (loadConfigFromEEPROM())
          {
            printConfig();
          }
          else
          {
            DEBUG_println("No config in EEPROM.");
          }
        }
        else if (inputString.equalsIgnoreCase("reset"))
        {
          eraseConfig();
          delay(1000);
          ESP.restart();
        }
        else if (inputString.equalsIgnoreCase("DEBUG_ON"))
        {
          debugEnabled = true;
          DEBUG_println("Debugging enabled");
        }
        else if (inputString.equalsIgnoreCase("DEBUG_OFF"))
        {
          debugEnabled = false;
          DEBUG_println("Debugging disabled");
        }
        else if (inputString.equalsIgnoreCase("RUNNING"))
        {
          if (currentMode == 0)
          {
            DEBUG_printf("Already in Running Mode\n");
            return false;
          }
          DEBUG_println("Switching to Running mode...");
          currentMode = 0; // Set mode to Running
          return true;
        }
        else if (inputString.equalsIgnoreCase("TRAINING"))
        {
          if (currentMode == 1)
          {
            DEBUG_printf("Already in Training Mode\n");
            return false;
          }
          DEBUG_println("Switching to Training mode...");
          currentMode = 1; // Set mode to Training
          return true;
        }
        else if (inputString.equalsIgnoreCase("SETUP"))
        {
          if (currentMode == 2)
          {
            DEBUG_printf("Already in Setup Mode\n");
            return false;
          }
          DEBUG_println("Switching to SETUP mode...");
          currentMode = 2; // Set mode to SETUP
          return true;
        }
        else if (inputString.equalsIgnoreCase("SEND"))
        {
          sendSta = true;
          lastTransmitTime = millis();
          return true;
        }
        else if (inputString.equalsIgnoreCase("restart"))
        {
          ESP.restart();
          return true;

        }
        else
        {
          DEBUG_println("Invalid command. Send JSON, or type 'show', 'reset', 'DEBUG_ON', 'DEBUG_OFF', 'RUNNING', 'TRAINING'.");
        }
        inputString = "";
      }
    }
    else
    {
      inputString += c;
    }
  }
  return false;
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
void OnDataSent(const uint8_t* mac_addr, esp_now_send_status_t status)
{
  DEBUG_printF("ESP-NOW Send Status: ");
  if (status == ESP_NOW_SEND_SUCCESS)
  {
    DEBUG_printlnF("Delivery Success");
    if (esp_now_del_peer(const_cast<uint8_t*>(mac_addr)) == 0)
    {
      DEBUG_printlnF("Peer deleted successfully.");
      timeOut = millis();
    }
    else
    {
      DEBUG_printlnF("Failed to delete peer.");
    }
  }
  else
  {
    DEBUG_printlnF("Delivery Failed");
  }
}
void prepareDataToSend() {
  outgoingData.sourceId = id.clusterId;
  outgoingData.targetId = id.targetId;
  outgoingData.networkId = id.networkId;
  outgoingData.direction = TO_GATEWAY;

  CayenneLPP lpp(MAX_PAYLOAD_SIZE);
  lpp.reset();
  lpp.addPresence(1, predicted_class);
  lpp.addGenericSensor(2, highest_confidence_score * 100);
  lpp.addPresence(3, inference_time);
  outgoingData.messageSize = lpp.getSize();
  memcpy(outgoingData.message, lpp.getBuffer(), lpp.getSize());

  DEBUG_print("ESP-NOW TX Data: src=");
  DEBUG_print(outgoingData.sourceId);
  DEBUG_print(", tgt=");
  DEBUG_print(outgoingData.targetId);
  DEBUG_print(", net=");
  DEBUG_print(outgoingData.networkId);
  DEBUG_print(", dir=");
  DEBUG_print(outgoingData.direction);
  DEBUG_print(", size=");
  DEBUG_println(outgoingData.messageSize);
  DEBUG_print("Payload: ");
  for (uint8_t i = 0; i < outgoingData.messageSize; i++) {
    DEBUG_print(outgoingData.message[i]);
    DEBUG_print(" ");
  }
  DEBUG_println("");

  forwardLoRa(outgoingData);
}
bool loadConfigFromEEPROM() {
  EEPROM.get(0, id);
  if (id.ctrlword != CTRL_WORD_VALUE) {
    DEBUG_printlnF("Invalid configuration in EEPROM.");
    return false;
  }

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
void saveConfigToEEPROM()
{
  EEPROM.put(0, id); // Simpan struct idConfig mulai dari alamat 1
  EEPROM.commit();
  DEBUG_printlnF("Configuration saved to EEPROM.");
}


void IRAM_ATTR LoRaInterruptHandler()
{
  operationDone = true;
}
void callback(char* topic, byte* payload, unsigned int length)
{
  DEBUG_println("NeedCalibration Active");
  bool ok = mqttClient.publish(MQTT_TOPIC_MQCMD, "Starting Calibration");
  needCalibration = true;
}
void connectWiFiAndMQTT()
{
  DEBUG_println("Scanning WiFi networks...");
  uint8_t n = WiFi.scanNetworks();
  if (n == 0)
  {
    DEBUG_println("No WiFi networks found.");
  }
  else
  {
    DEBUG_printf("Found %d WiFi networks:\n", n);
    for (uint8_t i = 0; i < n; ++i)
    {
      DEBUG_printf("  %s (RSSI: %d)\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    }
  }

  DEBUG_println("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED)
  {
    if (checkSerialConfigCommand())
      return;
    DEBUG_print(".");
    delay(500);
  }
  DEBUG_println("\nWiFi Connected");

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);

  while (!mqttClient.connected())
  {
    if (checkSerialConfigCommand())
      return;
    DEBUG_println("Connecting to MQTT...");
    mqttClient.connect("ESP32S3Client", MQTT_USER, MQTT_PASS);
    mqttClient.subscribe(MQTT_TOPIC_MQCMD);
    mqttClient.setCallback(callback);
    delay(1000);
  }
  DEBUG_println("MQTT Connected");
}
void reconnectMQTT()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    DEBUG_println("Connecting to WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
  while (WiFi.status() != WL_CONNECTED)
  {
    if (checkSerialConfigCommand())
      return;
    DEBUG_print(".");
    delay(500);
    if (WiFi.status() == WL_CONNECTED)
    {
      DEBUG_println("\nWiFi Connected");
    }
  }

  while (!mqttClient.connected())
  {
    if (checkSerialConfigCommand())
      return;
    DEBUG_printf("Connecting to MQTT...   ");
    String mqttClientId = "Deviot" + String(random(1, 101));
    DEBUG_printf("Trying MQTT client ID: %s\n", mqttClientId.c_str());
    mqttClient.connect(mqttClientId.c_str(), MQTT_USER, MQTT_PASS);
    delay(1000);
  }
}
void publishToMQTTMachineLearning()
{
  StaticJsonDocument<1024> doc;

  String key = "predicted_class";
  JsonObject channel = doc.createNestedObject(key);

  channel["predicted_class"] = predicted_class;
  channel["confidence_score"] = highest_confidence_score;
  channel["inference_time"] = inference_time;

  char payload[1024];
  serializeJson(doc, payload, sizeof(payload));
  if (mqttClient.connected())
  {
    bool ok = mqttClient.publish(MQTT_TOPIC_ML, payload);
    // DEBUG_println(ok ? "MQTT Publish: OK" : "MQTT Publish: FAILED");
  }
  else
  {
    DEBUG_println("MQTT not connected");
  }
}
void publishToMQTT()
{
  StaticJsonDocument<1024> doc;
  for (uint8_t ch = 0; ch < 8; ch++)
  {
    String key = "CH" + String(ch + 1);
    JsonObject channel = doc.createNestedObject(key);
    channel["ID"] = id.clusterId;
    channel["voltage"] = voltValues[ch];
    channel["ADC"] = adcValues[ch];
    channel["gain"] = pgaCount[ch];
  }
  char payload[1024];
  serializeJson(doc, payload, sizeof(payload));
  if (mqttClient.connected())
  {
    bool ok = mqttClient.publish(MQTT_TOPIC_MQ, payload);
    // DEBUG_println(ok ? "MQTT Publish: OK" : "MQTT Publish: FAILED");
  }
  else
  {
    DEBUG_println("MQTT not connected");
  }
}
void preheatSensorDelta(float threshold_mV)
{ // 5
  DEBUG_println("Memulai pemanasan sensor (menunggu delta < 10mV semua channel)...");

  // Tunggu hingga semua channel memiliki delta di bawah threshold
  bool stable = false;
  while (!stable)
  {
    if (checkSerialConfigCommand())
      return;
    readAllChannels(false, true);
    stable = true;
    String debugLine = "Delta Volt: ";
    for (int ch = 0; ch < 8; ch++)
    {
      float delta = fabs(voltValues[ch] - voltValuesPrev[ch]);
      if (delta <= threshold_mV / 1000.0)
      {
        debugLine += String(ch + 1) + " : OK(" + String(delta, 4) + ")  ";
      }
      else
      {
        debugLine += String(ch + 1) + " : " + String(delta, 4) + "V  ";
        stable = false;
      }
      voltValuesPrev[ch] = voltValues[ch];
    }
    // Tambahkan spasi agar baris lama tertimpa (minimal 100 spasi)
    while (debugLine.length() < 100)
      debugLine += " ";
    DEBUG_print(debugLine + "\r"); // print di line yang sama
    delay(500);
  }
  DEBUG_println(); // pindah baris setelah selesai
  DEBUG_println("Pemanasan Selesai! Semua channel stabil.");
}
void setAllWiper(uint16_t wiperValue)
{
  for (uint8_t ch = 0; ch < 8; ch++)
  {
    mux.selectChannel(ch);
    dac.begin();dac.setValue(wiperValue);
    DEBUG_printf("[DEBUG] Potensiometer Channel %d di-set ke %d.\n", ch + 1, wiperValue);
    delay(100);
  }
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
      sendSta = true;
      // forwardLoRa(receivedData);
    }
  }
  else {
    DEBUG_printlnF("Error reading LoRa message.");
  }
}
void LoRaLoop()
{
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
  // if ((millis() - timeOutLoRa > timeoutTimer)) {
  //   if (messageQueueLora.empty()) {
  //     ESP.restart();
  //   }
  // }

  unsigned long currentMillis = millis();
  if (currentMillis - lastTransmitTime >= (unsigned long)random(LORA_TRANSMIT_TIMER, LORA_TRANSMIT_TIMER + 3000)) {
    lastTransmitTime = currentMillis;
    if (!messageQueueLora.empty()) {
      sendNextLoRaMessage();
    }
  }
}
String readAllChannels(bool debugPrint, bool readMQ)
{
  if (readMQ)
  {
    for (uint8_t chs = 0; chs < 8; chs++)
    {
      A.setMUX(SING_0 + (chs * 16)); //jika SING_0 dalam decimal adalah 15
      A.readSingle(); // buang bacaan pertama
      adcValues[chs] = A.readSingle();
      voltValues[chs] = A.convertToVoltage(adcValues[chs]);
    }
  }

  if (debugPrint)
  {
    // String voltLine = "Volt: ";
    String voltLine = "";
    for (int chs = 0; chs < 8; chs++) {
      voltLine += String(chs + 1) + ": ";
      voltLine += String(voltValues[chs], 4);
      uint8_t gain = 1;
      switch (pga_settings[pgaValues[chs]]) {
      case PGA_1: gain = 1; break;
      case PGA_2: gain = 2; break;
      case PGA_4: gain = 4; break;
      case PGA_8: gain = 8; break;
      case PGA_16: gain = 16; break;
      case PGA_32: gain = 32; break;
      case PGA_64: gain = 64; break;
      }
      voltLine += String("(Gain") + String(gain) + String(")");
      if (chs < 7) voltLine += "\t";   // pisahkan dengan tab 
    }

    // Serial.println();

    // Tambahkan spasi agar baris lama tertimpa
    // voltLine += "                                                                                   ";
    ;
    // DEBUG_print(voltLine + "\r");
    return voltLine;
  }
  return "";
}

uint16_t binarySearchVisual(uint16_t low, uint16_t high) {
  for (size_t i = 0; i < 8; i++)
  {
    mux.selectChannel(i);
    Serial.printf("🔍 Mencari titik perubahan tegangan pada Channel %d:\n", i);
    A.setMUX(SING_0 + (i * 16));
    uint8_t gain = GainForCalibration;
    float voltages[11];
    uint16_t result;
    A.setPGA(gain);
    DEBUG_printf("ADS1256 Channel %d - PGA set to %dx for calibration.\n", i, gain);
    A.readSingle();
    // ==== hitung baseline otomatis dari MCP 0–10 ====
    for (int i = 0; i <= 10; i++) {
      dac.begin();dac.setValue(i);
      A.readSingle(); // buang bacaan pertama
      voltages[i] = A.convertToVoltage(A.readSingle());
    }

    Serial.println("\n🧭 Mengukur baseline dari MCP Value 0-10:");
    for (int i = 0; i <= 10; i++) {

      Serial.printf("  MCP %2d → %.6f V\n", i, voltages[i]);
    }


    dac.begin();dac.setValue(0); // kembalikan ke 0
    A.readSingle(); // buang bacaan pertama

    // cari baseline (tegangan minimum dari sampel)
    float baseVoltage = voltages[0];
    bool allSame = true;
    for (int i = 1; i <= 10; i++) {
      if (fabs(voltages[i] - voltages[0]) > ThresholdCalibration_V) {
        allSame = false;
        break;
      }
    }

    if (!allSame) {
      baseVoltage = voltages[0];
      for (int i = 1; i <= 10; i++) {
        if (voltages[i] < baseVoltage) baseVoltage = voltages[i];
      }
    }
    Serial.printf("📏 Baseline otomatis terdeteksi: %.6f V\n\n", baseVoltage);

    int left = low;
    int right = high;
    int step = 1;
    int found = -1;
    const int barLen = 60;

    while (left <= right) {
      Serial.print("Channel : ");Serial.println(i);
      int mid = (left + right) / 2;
      dac.begin();dac.setValue(mid);
      delay(10);
      A.readSingle(); // buang bacaan pertama
      float voltage = A.convertToVoltage(A.readSingle());
      float delta = fabs(voltage - baseVoltage);

      dac.begin();dac.setValue(left);
      delay(10);
      A.readSingle(); // buang bacaan pertama
      float vLeft = A.convertToVoltage(A.readSingle());

      dac.begin();dac.setValue(mid);
      delay(10);
      A.readSingle(); // buang bacaan pertama
      float vMid = A.convertToVoltage(A.readSingle());
      dac.begin();dac.setValue(right);
      delay(10);
      A.readSingle(); // buang bacaan pertama
      float vRight = A.convertToVoltage(A.readSingle());

      // buat bar visual sederhana
      char bar[barLen + 3];
      for (int i = 0; i < barLen; i++) bar[i] = '.';
      bar[barLen] = '\0';

      int posLeft = (float)left / high * barLen;
      int posRight = (float)right / high * barLen;
      int posMid = (float)mid / high * barLen;

      bar[posLeft] = 'L';
      bar[posRight] = 'R';
      bar[posMid] = 'O';

      Serial.printf("%02d: Cek MCP Value %d\n", step, mid);
      Serial.printf(" Visual : |%s|\n", bar);
      Serial.printf("           (MCP %-4d)                 (MCP %-4d)                 (MCP %-4d)\n", left, mid, right);
      Serial.printf(" Nilai  : MCP %-4d (%.6f V)    MCP %-4d (%.6f V)    MCP %-4d (%.6f V)\n",
        left, vLeft, mid, vMid, right, vRight);
      Serial.printf(" ΔV     : dari baseline %.6f V\n", delta);
      if (delta <= ThresholdCalibration_V) {
        Serial.println("  ➡️  Masih baseline (rendah) → geser ke kanan\n");
        left = mid + 1;
      }
      else {
        Serial.println("  ⬅️  Tegangan naik ⚡ → geser ke kiri\n");
        found = mid;
        right = mid - 1;
      }

      step++;
      delay(1000);
    }
    float hiddenValue = found;

    if (found != -1) {
      //print Baseline dan area sekitar titik naik

      Serial.println("\n──────────────────────────────────────────────");
      Serial.printf("Baseline tegangan terdeteksi pada %.6f V\n", baseVoltage);
      Serial.println("📊 AREA SEKITAR TITIK NAIK (±5 MCP VALUE)");
      Serial.println("──────────────────────────────────────────────");
      Serial.println("  MCP Value | Tegangan (V) | ΔV dari baseline");
      Serial.println("──────────────────────────────────────────────");

      for (int v = max_uint16(hiddenValue - 5, low); v <= min_uint16(hiddenValue + 10, high); v++) {
        dac.begin();dac.setValue(v);
        A.readSingle(); // buang bacaan pertama
        float volt = A.convertToVoltage(A.readSingle());
        float d = fabs(volt - baseVoltage);
        Serial.printf("  %9d | %.6f | %.6f %s\n", v, volt, d, (v == (int)hiddenValue ? "★" : " "));
      }

      Serial.println("──────────────────────────────────────────────");
      dac.begin();dac.setValue(hiddenValue);
      delay(10);
      A.readSingle(); // buang bacaan pertama
      Serial.printf("✅ Perubahan tegangan mulai di sekitar MCP Value %.0f\n", hiddenValue);
      Serial.printf("   Tegangan ≈ %.6f V\n\n", A.convertToVoltage(A.readSingle()));
    }
    else {
      Serial.println("❌ Tidak terdeteksi kenaikan tegangan\n");
    }
    wiperArr[i] = hiddenValue;
  }
  return true;
}
void setWiperFromWiperArray(uint8_t ch)
{
  mux.selectChannel(ch);
  dac.begin();dac.setValue(wiperArr[ch]);
  // DEBUG_printf("Channel %d = wiper %3d \n", ch + 1, wiperArr[ch]);
  delay(100);
}
void takeDataMQ(uint8_t ch)
{
  A.setMUX(SING_0 + (ch * 16)); //SING0 itu 1111 channel 0.. jika channel 1 maka SING_0 + 15
  A.readSingle(); // buang bacaan pertama
  long defaultRaw = A.readSingle();
  float voltage = A.convertToVoltage(defaultRaw);

  // MOVING AVERAGE ---------------------------
  // movingAvgFloat* avgVoltArr[8] = {
  //     &avgVolt0, &avgVolt1, &avgVolt2, &avgVolt3,
  //     &avgVolt4, &avgVolt5, &avgVolt6, &avgVolt7 };
  // voltValues[ch] = avgVoltArr[ch]->reading(voltage); // dengan movingaverage

  // Tanpa Moving Average 
  uint8_t targetIdx = 0;
  switch(ch){
    case 0: targetIdx = 0; break;
    case 1: targetIdx = 2; break;
    case 2: targetIdx = 5; break;
    case 3: targetIdx = 3; break;
    case 4: targetIdx = 4; break;
    case 5: targetIdx = 6; break;
    case 6: targetIdx = 1; break;
    case 7: targetIdx = 7; break;
  }
  voltValues[targetIdx] = voltage; // tanpa movingaverage
  adcValues[targetIdx] = defaultRaw;
}
void machineLearning()
{
  if (!network->isInitialized())
  {
    DEBUG_println("ERROR: Neural Network failed to initialize. Halting.");
    while (true)
    {
      if (checkSerialConfigCommand())
        return;
      delay(100);
    }
  };
  for (int i = 0; i < 8; ++i)
  {
    scaled_sensor_data[i] = (voltValues[i] - feature_means[i]) / feature_stds[i];
  };
  float* model_input_buffer = network->getInputBuffer();
  memcpy(model_input_buffer, scaled_sensor_data, 8 * sizeof(float));

  if (model_input_buffer != nullptr)
  {
    start_time = micros();
    predicted_class = network->predict(highest_confidence_score);
    end_time = micros();
    inference_time = end_time - start_time;

    DEBUG_printf("Predicted class index : %d\n", predicted_class); //jika bukan no Gas
    DEBUG_printf("Confidence score : %.2f\n", highest_confidence_score); //jika di atas 80%
    DEBUG_printf("Inference time : %d\n", inference_time);

    if (predicted_class != 0 && highest_confidence_score >= 0.80) {
      id.mode = AUTO_TRANSMIT; // set mode ke auto transmit
    }
    else {
      id.mode = !AUTO_TRANSMIT; // set mode ke manual transmit
    }
  }
}

void StartADS() {
  SPI_ADS.begin(PIN_SCK_ADS, PIN_MISO_ADS, PIN_MOSI_ADS, PIN_CS_ADS); // SCK 18, MISO 6, MOSI 7, CS 16
  // SPI.begin(PIN_SCK_ADS, PIN_MISO_ADS, PIN_MOSI_ADS, PIN_CS_ADS); // SCK 18, MISO 6, MOSI 7, CS 16
  pinMode(PIN_DRDY, INPUT);
  pinMode(PIN_RESET, OUTPUT);
  pinMode(PIN_SYNC, OUTPUT);
  pinMode(PIN_CS_ADS, OUTPUT);
  digitalWrite(PIN_CS_ADS, HIGH);

  // Manual reset ADS1256
  digitalWrite(PIN_RESET, LOW);
  delay(100);
  digitalWrite(PIN_RESET, HIGH);
  delay(100);
  A.InitializeADC();
  checkADS1256();
  // Set penguatan & data rate
  A.setPGA(PGA_SETTING);
  A.setDRATE(DRATE_SETTING);
  modeSPI = 0;
  DEBUG_printlnF("ADS1256 SPI mode started.");
}
void StartLoRa() {
  SPI_LORA.begin(PIN_SCK_LORA, PIN_MISO_LORA, PIN_MOSI_LORA, PIN_CS_LORA);  // SCK 12, MISO 13, MOSI 11, CS 10
  // SPI.begin(PIN_SCK_LORA, PIN_MISO_LORA, PIN_MOSI_LORA, PIN_CS_LORA);  // SCK 12, MISO 13, MOSI 11, CS 10
  int state = radio.begin(id.loraFreq, 125.0, 9, 7, RADIOLIB_SX127X_SYNC_WORD, 17, 8, 0);
  radio.setDio1Action(LoRaInterruptHandler);
  state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    DEBUG_printlnF("Error starting receive mode!");
    while (true)
      ;
  }
  else {
    DEBUG_printlnF("LoRa Receiver started!");
  }
  modeSPI = 1;
  DEBUG_printlnF("LoRa SPI mode started.");
}

// --- MODE 1: RUNNING BIASA ---
void modeRunning()
{
  if (millis() - millisTakeData > intervalTakeData)
  {
    millisTakeData = millis();
    if (modeSPI == 0) {
      gainCalibrate(ch);
      takeDataMQ(ch);
      // DEBUG_println(readAllChannels(true, false));
      ch++;
      ch = ch % 8;
    }
  }
  if (millis() - lastTransmitTime > 1000) {
    // kirim data lora
    if (sendSta) {
      DEBUG_println("=================================");
      DEBUG_println("Data Request Triggered...");
      sendSta = false;
      prepareDataToSend(); //send data lora
      lastTransmitTime = millis();
    }
  }
  if (id.mode == AUTO_TRANSMIT)
  {
    if (((millis() - lastTransmit) > (id.periode * 1000)))
    {
      DEBUG_println("=================================");
      DEBUG_println("Interval Kirim Data Triggered...");
      // kirim data lora
      prepareDataToSend(); //send data lora
      lastTransmit = millis();
    }
  }
  if (millis() - millisMachineLearning > intervalMachineLearning)
  {
    millisMachineLearning = millis();
    machineLearning();
  }
  LoRaLoop();
}

// --- MODE 2: TRAINING ---
void modeTraining()
{
  DEBUG_print("Mode: Training ");
  DEBUG_println((int)round(millis() / 1000.0));
  if (millis() - millisTakeData > intervalTakeData)
  {
    millisTakeData = millis();
    takeDataMQ(ch);
    ch++;
    if (ch >= 8)
    {
      ch = 0;
      DEBUG_println();
    }
  }
  if (millis() - millisMQTT > intervalMQTT)
  {
    millisMQTT = millis();
    reconnectMQTT();
    publishToMQTT();
  }
  mqttClient.loop();
  if (needCalibration)
  {
    modeSetupCalibration();
    for (int i = 0; i < 8; i++)
      id.wiperArr[i] = wiperArr[i];
  }
  if (checkSerialConfigCommand())
    return;
}

// --- MODE 3: SETUP KALIBRASI (jika EEPROM belum diinisialisasi) ---
void modeSetupCalibration()
{
  DEBUG_println("Mode: Setup Kalibrasi");
  setAllWiper(1); // set semua potensiometer ke nilai maksimum

  delay(500);
  preheatSensorDelta(10); // sensor preheat sampai delta < 10mV

  for (uint8_t i = 0; i < 5; i++)
  {
    DEBUG_print(readAllChannels(true, true) + "\r");
    delay(200);
  }
  DEBUG_println();

  binarySearchVisual(1, 4095);
  //bikin summary untuk semua channel jdnya angka wiperArrnya berapa saja..
  DEBUG_println("MCP Value Array hasil kalibrasi:");
  for (uint8_t i = 0; i < 8; i++)
  {
    DEBUG_printf("Channel %d : MCP Value %d\n", i + 1, wiperArr[i]);
  }
  DEBUG_println("Mengatur semua potensiometer ke nilai kalibrasi...");

  for (uint8_t ch = 0; ch < 8; ch++)
  {
    mux.selectChannel(ch);
    if (dac.getValue() != wiperArr[ch])
    {
      dac.begin();dac.setValue(wiperArr[ch]);
      delay(100);
    }
  }

  preheatSensorDelta(5);
  needCalibration = false;


  delay(1000);

  for (uint8_t i = 0; i < 8; i++)
  {
    setWiperFromWiperArray(i);
  }

  for (uint8_t i = 0; i < 5; i++)
  {
    // readAllChannels(true);
    DEBUG_print(readAllChannels(true, true) + "\r");
    delay(200);
  }
  DEBUG_println();

  DEBUG_println("Kalibrasi selesai, wiper disimpan.");
}

// --- SETUP ---
void setup()
{
  delay(5000);
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  network = new NeuralNetwork();

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (!mux.begin())
  {
    Serial.println("❌ TCA9548 not detected! Check connections.");
    // while (1)
    //   delay(1000);
  }
  else {
    Serial.println("✅ TCA9548 detected.");
    for (uint8_t ch = 0; ch < 8; ch++)
    {
      mux.selectChannel(ch);
      Serial.printf("Channel %d: \n", ch);
      i2cScanner();
    }
  }

  EEPROM.begin(EEPROM_SIZE);
  bool configValid = loadConfigFromEEPROM();
  if (!configValid) {
    DEBUG_print("No valid config found. ");
    //debug alasannya ga valid
    DEBUG_print("expected :");
    DEBUG_println(CTRL_WORD_VALUE);
    DEBUG_print("found    :");
    DEBUG_println(id.ctrlword);
    long configMillis = millis();
    while (1)
    {
      if (millis() - configMillis >= 10000)
      {
        configMillis = millis();
        DEBUG_println("Send JSON config like:");
        DEBUG_print("{clusterId:1,networkId:1,targetId:0,mode:0,periode:60,toGatewayId:1,toNodeId:0,loraFreq:921}");
      }
      checkSerialConfigCommand();
    }
  }
  else
  {
    DEBUG_println("ID Config di EEPROM valid.");
    for (int i = 0; i < 8; i++)
      wiperArr[i] = id.wiperArr[i];
  }
  wiperArr[0] = 806;
  wiperArr[1] = 314;
  wiperArr[2] = 2049;
  wiperArr[3] = 369;
  wiperArr[4] = 649;
  wiperArr[5] = 444;
  wiperArr[6] = 529;
  wiperArr[7] = 915;
  for (uint8_t i = 0; i < 8; i++)
  {
    setWiperFromWiperArray(i);
    DEBUG_printf("Channel %d di-set ke wiper %d.\n", i + 1, wiperArr[i]);
  }
  DEBUG_println("Wiper di-set dari EEPROM.");

  StartLoRa();
  StartADS();
  DEBUG_println("Setup awal selesai.");
  delay(5000);
}
// #endregion

// #region LOOP
void loop()
{
  if (currentMode == 0)
  {
    modeRunning();
    setupMQTTDone == false;
  }
  else if (currentMode == 1)
  {
    if (setupMQTTDone == false)
    {
      setupMQTTDone = true;
      connectWiFiAndMQTT();
      mqttClient.setBufferSize(1024);
      if (currentMode == 1)
      {
        preheatSensorDelta(10);
      }
    }
    else
    {
      modeTraining();
    }
  }
  else if (currentMode == 2)
  {
    setupMQTTDone == false;
    modeSetupCalibration();
    if (currentMode == 2)
    {
      for (int i = 0; i < 8; i++)
        id.wiperArr[i] = wiperArr[i];
    }
  }
  checkSerialConfigCommand();
  yield();
}
// #endregion