# Gas Leak Firmware And Improvement Design

## Purpose

This document explains the firmware design, what is improved from the current program, how the dataset and training pipeline connect to firmware, how the runtime states should work, and how to run the improvement tools.

It is meant to answer:

- what the current firmware does
- what the improved firmware should do
- what changed and why
- what data is needed
- how training works
- how to run simulation/training
- how firmware should validate and transmit ML results

## 1. Current Firmware Summary

The existing gas sensor board firmware already has the right basic shape:

```text
read MQ sensor voltages
  -> scale values
  -> copy into TensorFlow Lite Micro input tensor
  -> invoke model
  -> decode class prediction
  -> trigger auto-transmit if gas is detected
```

Current behavior:

- reads 8 ADC channels
- maps channel values into `voltValues[8]`
- scales features using board-specific `feature_means` and `feature_stds`
- runs TensorFlow Lite Micro locally on ESP32-S3
- expects one softmax output
- predicts one class
- triggers auto-transmit when:

```text
predicted_class != normal
confidence >= 0.80
```

Current original model contract:

```text
input: 8 float features
output: one class probability vector
old classes:
  0 = normal
  1 = methane
  2 = LPG / butane-related
```

Current LoRa result is limited:

- predicted class
- confidence
- inference time

It does not yet transmit the full ML result, severity, ppm estimate, or all 8 MQ readings.

## 2. Improved Firmware Goal

The improved firmware should support a production-oriented multi-task model:

```text
input:
  8 MQ sensor features

outputs:
  gas_type
  leak_present
  severity
  ppm_estimate
```

The improved firmware should:

- keep one board-specific model per gas sensor board
- verify model input/output tensor contract before inference
- keep feature order identical to the training dataset
- decode four model outputs
- support six gas classes
- send a versioned 32-byte binary LoRa payload
- fail safe on invalid sensor readings, low confidence, or out-of-distribution input
- keep model, scaler, payload, and board versions traceable

## 3. What Is Improved

| Area | Current Firmware | Improved Firmware |
| --- | --- | --- |
| Gas classes | 3 classes: normal, methane, LPG | 6 classes: normal, methane, H2S, butane, propane, CO |
| Model outputs | one softmax vector | four outputs: gas type, leak present, severity, ppm estimate |
| PPM | not included or proxy only | proxy now, real ppm later after calibrated reference data |
| Severity | not included | normal, low, medium, high |
| Payload | minimal class/confidence | versioned 32-byte binary payload |
| Sensor data in payload | not complete | all 8 MQ millivolt readings |
| Safety checks | limited confidence check | invalid reading, stale data, ADC saturation, tensor contract, low confidence, OOD |
| Versioning | limited | model version, scaler version, payload version, board ID |
| Dataset link | old board Excel data | chamber time-series dataset with session/state/reference metadata |

## 4. Fixed Feature Order

Firmware ADC mapping must match the dataset feature order exactly:

```text
MQ135V
MQ2V
MQ3V
MQ4V
MQ7V
MQ5V
MQ6V
MQ8V
```

This is critical. If two channels are swapped, the model may look correct in training but fail on the actual board.

Firmware should document the ADC channel mapping like this:

| Feature index | Dataset column | Firmware value |
| ---: | --- | --- |
| 0 | `MQ135V` | `voltValues[0]` |
| 1 | `MQ2V` | `voltValues[1]` |
| 2 | `MQ3V` | `voltValues[2]` |
| 3 | `MQ4V` | `voltValues[3]` |
| 4 | `MQ7V` | `voltValues[4]` |
| 5 | `MQ5V` | `voltValues[5]` |
| 6 | `MQ6V` | `voltValues[6]` |
| 7 | `MQ8V` | `voltValues[7]` |

## 5. Gas Class Contract

Stable production class IDs:

| Class ID | Gas |
| ---: | --- |
| 0 | normal |
| 1 | methane |
| 2 | H2S |
| 3 | butane / LPG-related gas |
| 4 | propane |
| 5 | CO |

Label source in datasets:

```text
param:a-b-c-d-e
```

Mapping:

```text
a = methane
b = H2S
c = butane / LPG-related gas
d = propane
e = CO
```

Examples:

```text
param:0-0-0-0-0 -> normal
param:1-0-0-0-0 -> methane
param:0-1-0-0-0 -> H2S
param:0-0-1-0-0 -> butane
param:0-0-0-1-0 -> propane
param:0-0-0-0-1 -> CO
```

## 6. Improved Runtime States

The sensor node should have explicit runtime states so debugging and fail-safe behavior are clear.

Recommended firmware states:

| State | Meaning | Main action |
| --- | --- | --- |
| `BOOT` | Device just powered on | initialize serial, pins, memory |
| `INIT_ADC` | ADC setup | initialize ADS1256 / ADC hardware |
| `INIT_MODEL` | ML setup | load TFLite model and allocate tensors |
| `WARMUP` | MQ sensors warming up | read but do not trust gas predictions yet |
| `IDLE` | ready state | wait for sample interval or command |
| `READ_SENSORS` | sample MQ values | read all 8 channels |
| `VALIDATE_INPUT` | check sensor values | detect invalid, saturated, stale, disconnected |
| `RUN_INFERENCE` | ML invocation | scale features and invoke model |
| `VALIDATE_OUTPUT` | check ML result | confidence threshold, tensor shape, OOD checks |
| `ALARM_DECISION` | decide local output | LED/buzzer/auto-transmit decision |
| `BUILD_PAYLOAD` | encode message | build 32-byte `GasLeakPayloadV1` |
| `TRANSMIT` | LoRa send | send payload to cluster head |
| `ERROR_SAFE` | fail-safe mode | suppress unsafe claims and report error/uncertain |

Simple runtime flow:

```text
BOOT
  -> INIT_ADC
  -> INIT_MODEL
  -> WARMUP
  -> IDLE
  -> READ_SENSORS
  -> VALIDATE_INPUT
  -> RUN_INFERENCE
  -> VALIDATE_OUTPUT
  -> ALARM_DECISION
  -> BUILD_PAYLOAD
  -> TRANSMIT
  -> IDLE
```

Any invalid sensor/model condition should go to:

```text
ERROR_SAFE
```

## 7. Chamber Dataset States

These are not firmware runtime states. These are dataset labels from the gas test chamber.

Recommended chamber states:

| Chamber state | Meaning |
| --- | --- |
| `baseline_clean_air` | clean air before gas injection |
| `gas_injection` | gas valve is open |
| `mixing_rise` | gas is spreading and sensor response is rising |
| `stable_target_ppm` | reference instrument is near target concentration |
| `recovery_venting` | chamber is being purged |
| `post_recovery` | chamber has returned near baseline |

The chamber controller/logger should record this state for every row. This helps the model learn response and recovery behavior instead of mixing all time samples as if they were identical.

## 8. Dataset Design

Current dataset:

- Excel files per board
- 8 MQ voltage columns
- `sequence` column containing `param:a-b-c-d-e`
- mostly normal, methane, and butane/LPG-related rows
- no calibrated ppm labels
- not production-grade yet

Future production dataset should include:

```text
timestamp
board_id
session_id
gas_type
param
target_ppm
reference_ppm
severity_label
temperature_c
humidity_percent
pressure_hpa
chamber_state
gas_valve_state
clean_air_valve_state
outlet_state
fan_state
injection_duration_ms
time_since_injection_ms
MQ135V
MQ2V
MQ3V
MQ4V
MQ7V
MQ5V
MQ6V
MQ8V
reference_sensor_model
reference_sensor_voltage
reference_sensor_rs
reference_sensor_ro
reference_sensor_rs_ro
```

Important dataset rules:

- keep `board_id` because models are board-specific
- keep `session_id` because validation must hold out sessions/days
- keep chamber states because MQ sensors respond slowly
- keep BME280 readings because temperature/humidity affect MQ sensors
- keep `reference_ppm` only when a calibrated reference exists
- do not call the current proxy output real ppm

## 9. Feature Improvements

Current improvement program uses:

```text
8 scaled MQ voltages
```

Next recommended features:

```text
8 scaled MQ voltages
8 baseline delta features
8 ratio-to-baseline features
8 slope/window features
temperature
humidity
pressure
time_since_state_transition
```

Recommended time-window features:

- last `10` to `60` seconds
- moving average
- slope / rate of change
- maximum response
- recovery slope
- delta from clean-air baseline

These features are useful because MQ sensors have slow response and recovery.

## 10. Training Pipeline

Improvement files:

```text
improvement_program/
  simulate.py
  train_multitask.py
  gasleak_improved/common.py
  firmware/payload_contract.h
```

Training responsibilities:

1. Load board dataset.
2. Parse `param:a-b-c-d-e`.
3. Convert labels to gas class ID.
4. Derive `leak_present`.
5. Derive proxy `severity` from response strength.
6. Derive proxy `ppm_estimate` from response strength.
7. Scale features with `StandardScaler`.
8. Train a board-specific multi-task TensorFlow model.
9. Export `.tflite`.
10. Generate C model files.
11. Generate scaler C files.
12. Generate metrics and confusion matrices.

Current model outputs:

```text
gas_type: six-class softmax
leak_present: sigmoid
severity: four-class softmax
ppm_estimate: linear regression
```

Current ppm warning:

```text
ppm_estimate is a proxy until calibrated reference_ppm data exists.
```

## 11. Running The Improvement Program

Run from repository root.

Simulation:

```powershell
python improvement_program\simulate.py
```

Simulation for one board:

```powershell
python improvement_program\simulate.py --boards Board1
```

Training all active boards:

```powershell
python improvement_program\train_multitask.py
```

Training smoke test:

```powershell
python improvement_program\train_multitask.py --boards Board1 --epochs 1
```

Expected output folders:

```text
improvement_program/output/simulation/
improvement_program/output/models/<Board>/
improvement_program/output/reports/<Board>/
```

Generated model artifacts:

```text
gasleak_model.tflite
model_data.cc
model_data.h
scaler_params.cc
scaler_params.h
```

The improvement program does not overwrite existing `src/Gasleak/Board*` firmware folders. Generated artifacts should be reviewed before copying into board firmware.

## 12. Firmware Model Integration

Integration steps:

1. Train or simulate improved model.
2. Review metrics and confusion matrices.
3. Copy generated model/scaler artifacts into target board firmware folder only after review.
4. Update `NeuralNetwork` wrapper to support four outputs.
5. Validate input tensor:
   - type is `float32` for current export
   - shape is `[1, feature_count]` or compatible
6. Validate output tensors:
   - `gas_type` size = 6
   - `leak_present` size = 1
   - `severity` size = 4
   - `ppm_estimate` size = 1
7. Add confidence and fail-safe rules.
8. Build PlatformIO environment.
9. Flash one board and compare serial logs against expected results.
10. Test LoRa payload decode at gateway.

If future model export changes to int8, firmware must explicitly handle quantization:

```text
float sensor value
  -> scale
  -> quantize to int8 using input scale/zero-point
  -> invoke
  -> dequantize outputs
```

Do not write floats into an int8 tensor.

## 13. LoRa Payload Contract

Current improved payload:

```text
version
gas_type
leak_present
severity
gas_confidence_x1000
leak_probability_x1000
severity_confidence_x1000
ppm_estimate
inference_time_us
mq_millivolts[8]
```

Payload size:

```text
32 bytes
```

C contract:

```cpp
struct GasLeakPayloadV1 {
  uint8_t version;
  uint8_t gasType;
  uint8_t leakPresent;
  uint8_t severity;
  uint16_t gasConfidenceX1000;
  uint16_t leakProbabilityX1000;
  uint16_t severityConfidenceX1000;
  uint16_t ppmEstimate;
  uint32_t inferenceTimeUs;
  int16_t mqMillivolts[8];
} __attribute__((packed));
```

Why 32 bytes:

```text
4 uint8 fields = 4 bytes
4 uint16 fields = 8 bytes
1 uint32 field = 4 bytes
8 int16 fields = 16 bytes
total = 32 bytes
```

This fits inside the current 64-byte message limit.

## 14. Gateway Improvement

First integration can keep raw byte forwarding if needed.

Recommended next gateway improvement:

- detect payload version `1`
- decode binary payload
- publish named MQTT fields:
  - `gasType`
  - `gasName`
  - `leakPresent`
  - `severity`
  - `gasConfidence`
  - `leakProbability`
  - `severityConfidence`
  - `ppmEstimate`
  - `inferenceTimeUs`
  - `mqVoltages`

Named fields make debugging, dashboards, and client review much easier than raw bytes.

## 15. Validation

Firmware validation:

- build all active gas board environments
- build cluster head
- build gateway
- verify ADC feature order
- verify tensor shapes
- verify payload size is 32 bytes
- verify serial output on one board
- verify gateway receives payload
- verify fail-safe behavior

ML validation:

- use held-out session/day validation
- report precision per gas
- report recall per gas
- report F1 per gas
- report false alarm rate
- report missed leak rate
- report gas confusion matrix
- report severity confusion matrix
- report ppm MAE per gas only after calibrated ppm labels exist

Do not use random row split as the main production validation claim.

## 16. Open Firmware Questions

- What are the LED GPIO pins for each gas sensor board?
- What are the buzzer GPIO pins for each gas sensor board?
- Should gateway decode payload version `1` now or keep raw byte forwarding for first integration?
- What confidence threshold should be used for each gas?
- What ppm/severity threshold should be used per gas?
- What exact reference instrument will provide `reference_ppm` for methane, H2S, and CO?

## 17. Recommended Implementation Order

1. Verify ADC channel mapping against dataset feature order.
2. Keep improvement code in `improvement_program/` until model wrapper is ready.
3. Add tensor contract validation to firmware wrapper.
4. Add four-output decoding to firmware wrapper.
5. Add fail-safe input checks.
6. Add 32-byte payload encoder.
7. Add optional LED/buzzer pin handling.
8. Build one board firmware.
9. Test serial inference output.
10. Test LoRa transmission.
11. Add gateway payload decoder.
12. Collect chamber data.
13. Retrain models with session/day validation.
14. Replace proxy ppm with calibrated ppm only after reference labels exist.
