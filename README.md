# Gas Leak Sensor ML System

[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32--S3-blue)](https://platformio.org/)
[![TFLite Micro](https://img.shields.io/badge/ML-TensorFlow_Lite_Micro-orange)](https://www.tensorflow.org/lite/microcontrollers)
[![LoRa](https://img.shields.io/badge/Comm-LoRaWAN-green)](https://www.thethingsnetwork.org/)

An **electronic nose** gas leak detection system using an **8-sensor MQ array**, **machine learning on ESP32-S3**, **LoRa communication**, and a **gateway/cluster-head** architecture. Each sensor board runs TensorFlow Lite Micro inference locally and transmits results via LoRa.

> **Status:** Prototype / Lab-stage system. Not certified for production or safety-critical use.

---

## Features

- **8 MQ Sensor Array (Electronic Nose)** — MQ135, MQ2, MQ3, MQ4, MQ5, MQ6, MQ7, MQ8
- **6-Class Gas Detection** — Normal, Methane, H2S, Butane, Propane, CO
- **4-Output ML Model** — Gas type, leak presence, severity level, PPM estimate
- **On-Device Inference** — TensorFlow Lite Micro on ESP32-S3
- **LoRaWAN Telemetry** — 32-byte binary payload with full sensor + inference data
- **Per-Board Calibration** — Each board has its own trained model and scaler
- **Local Alarm** — GPIO-triggered alert on leak detection
- **Gateway + MQTT** — Cluster head aggregates LoRa, gateway publishes to MQTT

---

## System Architecture

```
8 MQ Sensors --> ADS1256 ADC --> ESP32-S3 --> TFLite Micro
                                          |
                        Local Alarm ---->| Inference
                                          |
                    32-byte LoRa --------> Cluster Head --> Gateway --> MQTT Dashboard
```

- **Input:** 8 scaled MQ voltage readings `[MQ135V, MQ2V, MQ3V, MQ4V, MQ7V, MQ5V, MQ6V, MQ8V]`
- **Output:** `gas_type` (6-class), `leak_present` (binary), `severity` (4-class), `ppm_estimate` (linear proxy)

---

## Model Outputs

| Output | Type | Classes / Range |
|---|---|---|
| `gas_type` | Softmax | 0=Normal, 1=Methane, 2=H2S, 3=Butane, 4=Propane, 5=CO |
| `leak_present` | Sigmoid | 0=No, 1=Yes |
| `severity` | Softmax | 0=Normal, 1=Low, 2=Medium, 3=High |
| `ppm_estimate` | Linear | Proxy estimate (requires calibrated chamber data) |

---

## Hardware

### Sensor Node (per board)
- **MCU:** ESP32-S3 DevKit
- **ADC:** ADS1256 24-bit Delta-Sigma (8 channels)
- **Gas Sensors:** MQ135, MQ2, MQ3, MQ4, MQ5, MQ6, MQ7, MQ8
- **Env Sensor:** BME280 (temp/humidity/pressure) — library included
- **Comms:** SX1276/SX1280 LoRa module
- **Active boards:** 1, 3, 4, 5, 6, 7, 9, 10, 11

### Cluster & Gateway
- **Cluster Head:** ESP8266 NodeMCU (or ESP32 variant)
- **Gateway:** ESP8266 NodeMCU + SPIFFS + MQTT (PubSubClient)

---

## Project Structure

```
gas-leak-ml-chamber-system/
├── src/
│   ├── Gasleak/         # Board-specific firmware (Board1..11)
│   ├── loraMeshGateway/ # LoRaWAN gateway firmware
│   ├── loraMeshClusterHead/       # Cluster head (ESP8266)
│   └── loraMeshClusterHead_ESP32/ # Cluster head (ESP32 variant)
├── lib/
│   ├── tfmicro/                 # TensorFlow Lite Micro
│   ├── RadioLib/                # LoRa driver
│   ├── ADS1256-main/            # ADC driver
│   ├── Adafruit_BME280_Library/
│   ├── PubSubClient/            # MQTT
│   ├── ArduinoJson/
│   └── ... (18 libraries total)
├── improvement_program/         # Python ML training pipeline
│   ├── gasleak_improved/
│   ├── firmware/
│   ├── simulate.py
│   └── train_multitask.py
├── reference/                   # Supporting documentation
├── include/                     # Header files
├── test/                        # Unit tests
├── design.md                    # System architecture doc
├── documentation.md             # Full system documentation
├── firmware_design.md           # Firmware design spec
├── session_notes.md             # Engineering session log
├── present.md                   # Client overview
├── rab_gas_test_chamber.md      # Test chamber budget (IDR)
├── todo.md                      # Engineering task checklist
└── platformio.ini               # Build environments
```

---

## Quick Start

### Firmware (PlatformIO)

```bash
# Install PlatformIO CLI
pip install platformio

# Build for a specific board
cd gas-leak-ml-chamber-system
pio run -e loraMeshGasleak1

# Upload (replace COM port as needed)
pio run -e loraMeshGasleak1 -t upload

# Monitor
pio device monitor -e loraMeshGasleak1
```

### Available Build Environments

| Environment | Board | Platform |
|---|---|---|
| `loraMeshGasleak1`..`11` | ESP32-S3 | espressif32 |
| `loraMeshGateway` | NodeMCU | ESP8266 |
| `loraMeshClusterHead` | NodeMCU | ESP8266 |
| `loraMeshClusterHead_ESP32` | ESP32 | espressif32 |

### ML Training (Python)

```bash
cd improvement_program

# Simulate inference on all active boards
python simulate.py

# Train multi-task models (outputs: models/, reports/)
python train_multitask.py
```

Generated artifacts: `gasleak_model.tflite`, `model_data.cc/h`, `scaler_params.txt`

---

## LoRa Payload (32 bytes)

```cpp
struct GasLeakPayloadV1 {
  uint8_t  version;               // 1 byte
  uint8_t  gasType;               // 1 byte (0..5)
  uint8_t  leakPresent;           // 1 byte
  uint8_t  severity;              // 1 byte (0..3)
  uint16_t gasConfidenceX1000;    // 2 bytes
  uint16_t leakProbabilityX1000;  // 2 bytes
  uint16_t severityConfidenceX1000; // 2 bytes
  uint16_t ppmEstimate;           // 2 bytes
  uint32_t inferenceTimeUs;       // 4 bytes
  int16_t  mqMillivolts[8];       // 16 bytes
} __attribute__((packed));
```

---

## Documentation

| File | Description |
|---|---|
| `design.md` | System rebuild architecture and model design |
| `documentation.md` | Full system documentation and methodology |
| `firmware_design.md` | Detailed firmware design and LoRa contract |
| `session_notes.md` | Engineering session log (2026-04-30) |
| `present.md` | Client-facing project overview |
| `rab_gas_test_chamber.md` | Test chamber build budget (RAB, IDR) |
| `todo.md` | Engineering task checklist |

---

## Current Limitations

- Dataset is **prototype-grade** with no calibrated PPM labels
- `ppm_estimate` is a response proxy, not real concentration
- Training data covers primarily Normal, Methane, and Butane/LPG
- Models use **random splits** instead of **session/day-based** validation
- BME280 sensor data not yet integrated into firmware
- **Not safety-certified** — this is a lab/educational tool

---

## Roadmap

- [ ] Build controlled-flow gas test chamber (see `rab_gas_test_chamber.md`)
- [ ] Collect calibrated time-series dataset with reference instruments
- [ ] Add BME280 environmental compensation
- [ ] Add time-window features (moving average, slope, delta-from-baseline)
- [ ] Implement OOD (out-of-distribution) detection
- [ ] Session/day-based ML validation
- [ ] Gateway MQTT field decoder

---

## License

No license specified. See [LICENSE](LICENSE) if applicable.

---

**Contributors:** [RavellerH](https://github.com/RavellerH), [fadlurrahmanf](https://github.com/fadlurrahmanf)

**Location:** Bandung, West Java, Indonesia
