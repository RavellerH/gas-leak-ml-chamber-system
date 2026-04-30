# Gas Leak Sensor ML System Documentation

## Read This First

This project is a gas leak detector that uses several gas sensors together instead of trusting one sensor. The 8 MQ sensors act like a small electronic nose: each gas creates a different pattern across the sensors, and the machine-learning model reads that pattern.

Simple flow:

```text
gas in air
  -> 8 MQ sensors produce voltage pattern
  -> ESP32-S3 reads the voltages
  -> ML model predicts gas type, leak status, severity, and ppm estimate
  -> LoRa sends the result to gateway/dashboard
```

Current status in one sentence:

```text
The software direction is good, but the current dataset is still prototype-grade; production accuracy needs controlled chamber data with calibrated reference measurements.
```

Beginner reading path:

1. Read Section 1 for the project summary.
2. Read Section 2 for the system flow.
3. Read Section 3 to understand why 8 sensors are used.
4. Read Section 9 to understand why the current dataset is not production-ready.
5. Read Section 11 for the gas test chamber plan.
6. Read Section 18 for the glossary if any term is unclear.

## Simple Diagrams

System block diagram:

```text
+------------------+      +------------------+      +------------------+
| 8 MQ Gas Sensors | ---> | ESP32-S3 + ML    | ---> | LoRa Transmitter |
+------------------+      +------------------+      +------------------+
                                   |
                                   v
                         local alarm decision

+------------------+      +------------------+      +------------------+
| Cluster Head     | ---> | Gateway          | ---> | MQTT / Dashboard |
+------------------+      +------------------+      +------------------+
```

Training data flow:

```text
test chamber
  -> gas exposure sessions
  -> MQ voltages + BME280 + reference ppm + chamber state
  -> per-board training dataset
  -> model + scaler + validation report
  -> firmware integration
```

On-device inference flow:

```text
read 8 MQ voltages
  -> scale with board-specific mean/std
  -> run TensorFlow Lite Micro model
  -> decode outputs
  -> apply confidence/fail-safe checks
  -> transmit 32-byte LoRa payload
```

## 1. Executive Summary

This project builds a gas leak detection system using an array of 8 MQ gas sensors, local embedded machine learning, LoRa communication, and a gateway for upstream reporting.

The key idea is that one MQ sensor alone is not selective enough for reliable gas identification. Instead, the system uses the combined response pattern of 8 MQ sensors as an electronic-nose sensor array. The machine-learning model learns the response fingerprint produced by the whole array.

Target outputs:

- gas type: `normal`, `methane`, `H2S`, `butane`, `propane`, `CO`
- leak present: `yes/no`
- severity: `normal`, `low`, `medium`, `high`
- concentration: ppm estimate after calibrated reference data is available

Current dataset status:

- usable for prototype validation
- not yet production-grade
- no calibrated ppm labels yet
- current ppm output must be treated as a proxy until chamber data collection is complete

The next major technical milestone is building a controlled test chamber and collecting production-grade time-series data with reference ppm, BME280 environmental readings, valve states, and chamber states.

## 2. System Architecture

High-level flow:

```text
8 MQ sensor array
  -> ADS1256 ADC
  -> ESP32-S3 gas sensor node
  -> TensorFlow Lite Micro inference
  -> local alarm decision
  -> LoRa packet
  -> cluster head
  -> gateway
  -> MQTT / dashboard / database
```

The gas sensor node performs local detection, so the system does not depend on cloud processing for alarm decisions.

## 3. Why Use 8 MQ Sensors?

Metal-oxide gas sensors are cross-sensitive. This means one sensor may respond to several gases, humidity, temperature, and other environmental factors. A single MQ sensor is therefore weak for gas classification.

An electronic nose solves this by using a sensor array. Each sensor responds differently, and the combined pattern becomes a fingerprint.

Mathematically, each sample is represented as a vector:

```text
x = [MQ135V, MQ2V, MQ3V, MQ4V, MQ7V, MQ5V, MQ6V, MQ8V]
```

The model does not ask, "What does MQ2 say?" It asks, "What pattern do all 8 sensors produce together?"

This is aligned with electronic-nose literature, where sensor arrays plus pattern recognition are used because individual metal-oxide sensors have limited selectivity.

## 4. Current Firmware ML Flow

The existing firmware already follows the correct embedded inference pattern.

Current firmware flow:

```text
read 8 ADC channels
  -> map channels to 8 MQ feature order
  -> scale features
  -> copy features to TFLite tensor
  -> invoke model
  -> decode output
  -> transmit result through LoRa
```

Feature scaling:

```text
z_i = (x_i - mean_i) / std_i
```

Where:

- `x_i` = raw MQ voltage
- `mean_i` = training-set mean for that sensor feature
- `std_i` = training-set standard deviation for that sensor feature
- `z_i` = normalized model input

The current original model expects:

```text
input: 8 float features
output: one softmax class vector
```

The improved production model should expect:

```text
input: 8 MQ features + optional BME280 features
outputs:
  gas_type
  leak_present
  severity
  ppm_estimate
```

## 4.1 Sensor Read Flow For Beginners

The sensor-read path is not a special "null process." It is an initialization and acquisition sequence.

In simple terms:

```text
power on
  -> start hardware
  -> read 8 MQ sensors
  -> scale the values
  -> run the model
  -> decide what gas is present
  -> send the result
```

Important idea:

- `init` means the hardware is ready
- `null` means something is missing or not ready

The firmware must initialize the ADC and related hardware before it can read the gas sensors. If the model wrapper is not initialized, prediction should stop safely instead of using invalid data.

Current sensor-read path in the firmware:

1. `setup()` starts the board.
2. `StartADS()` initializes the ADC hardware.
3. `takeDataMQ()` reads the analog sensor channels.
4. The raw voltages are stored in `voltValues[8]`.
5. The values are scaled using `feature_means[]` and `feature_stds[]`.
6. `machineLearning()` sends the scaled values to the model.
7. `prepareDataToSend()` packages the result for LoRa.

The model does not read the sensor directly. The ADC reads the sensor first, then the firmware prepares the data, then the ML model interprets it.

## 5. Gas Label Design

The command label format is:

```text
param:a-b-c-d-e
```

Confirmed mapping:

| Position | Gas |
| --- | --- |
| `a` | methane |
| `b` | H2S |
| `c` | butane / LPG-related gas |
| `d` | propane |
| `e` | CO |

Class encoding:

| Class ID | Gas |
| --- | --- |
| `0` | normal |
| `1` | methane |
| `2` | H2S |
| `3` | butane |
| `4` | propane |
| `5` | CO |

Example:

```text
param:0-0-0-0-0 = normal
param:1-0-0-0-0 = methane
param:0-1-0-0-0 = H2S
param:0-0-1-0-0 = butane
param:0-0-0-1-0 = propane
param:0-0-0-0-1 = CO
```

## 6. Model Design

The proposed production model is a multi-task model.

One shared feature extractor learns the sensor-array fingerprint, then separate output heads solve related tasks:

```text
input features
  -> shared dense layers
      -> gas_type output
      -> leak_present output
      -> severity output
      -> ppm_estimate output
```

### 6.1 Gas Type Classification

Output:

```text
p_gas = softmax(logits)
```

Softmax:

```text
p_i = exp(s_i) / sum_j exp(s_j)
```

The predicted class is:

```text
gas_type = argmax(p_gas)
```

Confidence:

```text
gas_confidence = max(p_gas)
```

### 6.2 Leak Present Classification

Leak detection is binary:

```text
leak_probability = sigmoid(s)
```

Sigmoid:

```text
sigmoid(s) = 1 / (1 + exp(-s))
```

Decision:

```text
leak_present = leak_probability >= threshold
```

The current firmware threshold for alarm behavior is:

```text
gas_confidence >= 0.80
```

### 6.3 Severity Classification

Severity classes:

```text
normal, low, medium, high
```

For prototype data, severity can only be derived from relative response strength. For production, severity should be derived from gas-specific ppm thresholds.

### 6.4 PPM Estimation

PPM regression should predict:

```text
reference_ppm
```

But the current dataset does not include calibrated ppm. Therefore, current ppm output is only a proxy:

```text
ppm_proxy = f(relative sensor response)
```

Production ppm requires real chamber data:

```text
MQ array + BME280 + reference gas meter -> reference_ppm
```

## 7. Loss Function

The multi-task training objective combines classification and regression losses:

```text
L_total =
  w1 * L_gas_type
  + w2 * L_leak_present
  + w3 * L_severity
  + w4 * L_ppm
```

Where:

```text
L_gas_type = sparse categorical cross entropy
L_leak_present = binary cross entropy
L_severity = sparse categorical cross entropy
L_ppm = mean squared error or mean absolute error
```

Categorical cross entropy:

```text
L = -sum_i y_i log(p_i)
```

Binary cross entropy:

```text
L = -[y log(p) + (1-y) log(1-p)]
```

Mean squared error:

```text
MSE = (1/n) * sum_i (ppm_true_i - ppm_pred_i)^2
```

Mean absolute error:

```text
MAE = (1/n) * sum_i |ppm_true_i - ppm_pred_i|
```

## 8. ML Improvement Roadmap

The best way to improve the model is to improve the data and features first, then tune the neural network.

Recommended next input features:

- raw/scaled 8 MQ voltages
- BME280 temperature, humidity, and pressure
- clean-air baseline per MQ sensor
- delta from clean-air baseline
- ratio to clean-air baseline
- moving average over a short window
- slope/rate of change
- maximum response in the window
- recovery slope
- time since injection or chamber state transition

Recommended time-window design:

```text
last 10 to 60 seconds of sensor readings
current value + mean + slope + max + delta from baseline
```

This is better than one isolated row because MQ sensors respond slowly and have recovery behavior.

Class imbalance handling:

- collect more data for minority classes first
- use class weights during training
- oversample minority classes only in the training split
- never oversample validation/test data
- report per-class results, not only total accuracy
- keep a confusion matrix per board

Unknown and interference handling:

- low-confidence predictions should become `uncertain`
- out-of-distribution inputs should fail safe
- false-positive cases should include alcohol vapor, perfume, smoke, cleaning chemicals, humid air, hot air, dust, and exhaust-like conditions
- an explicit `interference` state can be added later if the product needs to separate non-target vapors from clean air

Recommended production metrics:

- gas type accuracy
- precision per gas
- recall per gas
- F1 score per gas
- false alarm rate
- missed leak rate
- confusion matrix
- leak-present precision and recall
- severity confusion matrix
- ppm MAE per gas after calibrated ppm labels exist
- metrics by board
- metrics by session/day
- metrics by humidity and temperature range

The most important safety metric is missed leak rate. Total accuracy alone is not enough.

Practical ESP32-S3 model shape:

```text
input:
  8 scaled MQ voltages
  8 baseline delta features
  8 slope/window features
  temperature
  humidity
  pressure

network:
  Dense 64, ReLU
  Dense 32, ReLU
  Shared Dense 16, ReLU

outputs:
  gas_type softmax, 6 classes
  leak_present sigmoid
  severity softmax, 4 classes
  ppm_estimate linear regression
```

This is still small enough for TensorFlow Lite Micro testing but stronger than the current single-row 8-feature model.

## 9. Why Current Dataset Is Not Production Grade

The current dataset is useful for proof of concept, but not enough for production.

Current limitations:

- around 300 to 450 samples per usable board
- no calibrated ppm labels
- not enough H2S, propane, and CO data
- not enough temperature and humidity variation
- not enough false-positive gases/vapors
- no long-term drift data
- no session/day-based validation

Production risk:

Random row splitting can make accuracy look too high because adjacent samples from the same run are similar. Production validation should test on unseen sessions or days.

Better validation:

```text
train: sessions from days 1-5
test: sessions from day 6
```

or:

```text
train: some chamber runs
test: completely unseen chamber runs
```

## 10. Production Dataset Plan

Each row should include:

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

The chamber state must be logged because MQ sensors have response and recovery dynamics.

Recommended chamber states:

```text
baseline_clean_air
gas_injection
mixing_rise
stable_target_ppm
recovery_venting
post_recovery
```

## 11. Test Chamber Design

Planned chamber:

- open-flow test box
- clean-air inlet
- outlet hole
- relay-controlled solenoid valve for gas injection
- internal fan for mixing
- 8 MQ sensor board
- Figaro TGS2610 reference channel
- BME280 environmental sensor

Recommended test sequence:

1. Purge chamber with clean air.
2. Record baseline.
3. Inject gas using solenoid valve.
4. Mix air with fan.
5. Wait for target/reference ppm window.
6. Record stable samples.
7. Vent chamber.
8. Record recovery.
9. Repeat across multiple sessions/days.

Recommended chamber hardware:

- `10 L` to `30 L` clear acrylic or polycarbonate chamber
- gasketed lid
- clean-air inlet and exhaust outlet bulkheads
- sealed cable glands
- internal low-voltage brushless DC mixing fan
- 8-MQ sensor board
- BME280 or SHT31/SHT35 environmental sensor
- Figaro TGS2610 reference channel for LPG-family tests
- normally-closed gas solenoid valve outside the chamber
- normally-closed clean-air solenoid valve outside the chamber
- controlled exhaust valve or exhaust fan
- gas cylinder regulator
- needle valve, flow meter, or mass flow controller
- calibrated reference detector or analyzer for the target gas
- independent external safety alarm
- manual shutoff valve
- safe ventilation or exhaust path

Preferred flow layout:

```text
clean air / calibration gas
  -> regulator
  -> flow controller or needle valve
  -> normally-closed solenoid
  -> chamber with mixing fan
  -> exhaust outlet
  -> safe ventilation
```

Hardware safety notes:

- Keep relays, switching contacts, and spark-producing parts outside the gas space.
- Keep valves outside the chamber where possible and route gas through tubing.
- Do not estimate ppm by injecting an unknown amount of gas into a sealed box.
- Use calibrated gas references or controlled calibration gas flow for ppm labels.
- Start with LPG-family gases before attempting methane, CO, or H2S.
- Do not test CO or H2S in a DIY chamber without certified instruments, ventilation, gas-rated safety equipment, and supervision.

Recommended reference instruments:

| Gas | Minimum reference recommendation |
| --- | --- |
| butane / LPG | calibrated LPG or LEL detector; TGS2610 as reference/cross-check |
| propane | calibrated LPG or LEL detector; TGS2610 as reference/cross-check |
| methane | calibrated methane or LEL detector |
| CO | calibrated CO detector/logger or CO analyzer |
| H2S | calibrated H2S detector/logger or H2S analyzer |

Recommended external safety monitors:

- LPG/LEL alarm for methane, butane, propane, and LPG-family tests
- CO alarm for CO work
- H2S alarm for H2S work
- room ventilation or extraction fan
- manual emergency shutoff

Current chamber drawing review:

The file `Gas Test Chamber Design.jpeg` is a good mechanical start. It shows a clear acrylic chamber, removable plate, detector mounting plate, exhaust hole, locking/clamping concept, and separated acrylic/plate/block parts.

Required revisions:

1. Add controlled-flow ports.
   - The drawing shows an exhaust hole, but it needs clearly labeled:
     - `gas_inlet`
     - `clean_air_inlet`
     - `exhaust_outlet`
     - `cable_gland`
     - `reference_meter_sampling_port`

2. Reconsider chamber volume.
   - The drawing appears close to `400 mm x 500 mm x 300 mm`, around `60 L`.
   - This can work, but it needs more gas, longer mixing time, and longer recovery.
   - `10 L` to `30 L` is more practical for early ML testing.

3. Add internal mixing fan detail.
   - Add a small low-voltage brushless fan inside the chamber.
   - Log fan state during every sample.

4. Add gasket/seal detail.
   - The lock alone is not enough for repeatable sealing.
   - Add silicone or rubber gasket detail around the door/plate contact area.

5. Avoid wood inside the chamber.
   - Wood can absorb gas/vapor and affect recovery data.
   - Use acrylic, aluminum, stainless steel, PTFE, or another low-absorption material.

6. Define sensor placement.
   - Mark the 8-MQ board, TGS2610/reference sensor, and BME280/SHT31/SHT35 positions.
   - Keep the environmental sensor away from direct hot airflow from MQ heaters.

7. Keep risky electronics outside.
   - Put solenoids, relays, high-current switching, and spark-producing parts outside the gas space.
   - Use sealed cable glands for low-voltage sensor and fan wiring.

Recommended revised flow:

```text
gas cylinder / clean air
  -> regulator
  -> flow controller or needle valve
  -> solenoid outside chamber
  -> gas inlet bulkhead
  -> chamber + mixing fan + sensors
  -> exhaust outlet bulkhead
  -> safe ventilation
```

## 12. Reference Sensor Role

The current reference sensor is Figaro TGS2610.

TGS2610 is useful for:

- butane
- propane
- LPG-family leak tests

It should not be treated as a universal ppm reference for:

- methane
- H2S
- CO

For methane, H2S, and CO, production ppm training requires gas-specific calibrated reference instruments.

Recommended alternatives:

| Target gas | Recommended reference option |
| --- | --- |
| butane / LPG | NevadaNano MPS flammable gas sensor or calibrated LPG/LEL detector |
| propane | NevadaNano MPS flammable gas sensor or calibrated LPG/LEL detector |
| methane | NevadaNano MPS flammable gas sensor or calibrated methane/LEL detector |
| CO | Alphasense CO electrochemical sensor or calibrated CO detector/logger |
| H2S | Alphasense H2S electrochemical sensor or calibrated H2S detector/logger |
| VOC / false positives | Sensirion SGP40 or similar VOC sensor as interference context only |

Recommended reference setup:

```text
MQ array = model input sensors
NevadaNano MPS or calibrated LEL detector = methane / propane / butane / LPG reference
Alphasense CO or calibrated CO logger = CO reference
Alphasense H2S or calibrated H2S logger = H2S reference
BME280 or SHT35 = environment reference
optional VOC sensor = interference context
```

Notes:

- NevadaNano MPS is useful for flammable-gas work because `%LEL` is directly relevant to leak safety.
- CO and H2S should use electrochemical reference sensors or calibrated detectors/loggers.
- A complete calibrated multi-gas detector/logger is preferred when trusted labels and safety alarms are required.
- Raw sensors still need calibration, drift management, and environmental compensation.
- VOC sensors should not be used as ppm ground truth; use them only to label or detect interference conditions.

## 13. LoRa Payload

The improved payload is compact and versioned.

Payload size:

```text
32 bytes
```

Payload fields:

| Field | Type | Meaning |
| --- | --- | --- |
| version | `uint8_t` | payload format version |
| gas type | `uint8_t` | gas class ID |
| leak present | `uint8_t` | yes/no |
| severity | `uint8_t` | severity class |
| gas confidence | `uint16_t` | confidence x1000 |
| leak probability | `uint16_t` | probability x1000 |
| severity confidence | `uint16_t` | confidence x1000 |
| ppm estimate | `uint16_t` | ppm proxy now, real ppm later |
| inference time | `uint32_t` | microseconds |
| MQ voltages | `int16_t[8]` | millivolts |

This fits inside the existing `64 byte` message limit.

## 14. Engineering Critique And Improvements

The architecture is technically sound, but the current data is still prototype-grade. The system should not be presented as production-ready until chamber collection, calibrated ppm references, and session-based validation are complete.

Highest-value improvements:

1. Fix the data collection first.
   - Collect time-series chamber data, not isolated rows.
   - Include clean air, injection, rise, stable ppm, recovery, and post-recovery phases.
   - Log `session_id`, `board_id`, `gas_type`, `reference_ppm`, temperature, humidity, pressure, valve states, fan state, and chamber state.
   - Validate using held-out sessions or days, not random row splits.

2. Do not claim real ppm yet.
   - Current `ppm_estimate` is only a response proxy.
   - Real ppm needs calibrated reference instruments for each gas.
   - TGS2610 is useful for LPG, butane, and propane-family reference work, but it is not a universal ppm reference for methane, H2S, or CO.

3. Keep the six-class contract now.
   - Use the stable class IDs: `normal`, `methane`, `H2S`, `butane`, `propane`, `CO`.
   - Even if current data only has normal, methane, and butane/LPG-related samples, the firmware and payload IDs should not change later.

4. Add fail-safe behavior.
   - Low-confidence results should be treated as uncertain.
   - Firmware should check for impossible sensor values, disconnected sensors, saturated ADC readings, stale data, and out-of-distribution inputs.

5. Verify firmware sensor order.
   - The ADC mapping must exactly match the training feature order:

```text
MQ135V, MQ2V, MQ3V, MQ4V, MQ7V, MQ5V, MQ6V, MQ8V
```

   - A swapped channel can silently break real inference even if the training report looks good.

6. Version everything.
   - Payload version, model version, scaler version, board ID, dataset version, and chamber session metadata should be tracked together.

7. Decode the gateway payload soon.
   - Raw byte forwarding is acceptable for the first rebuild.
   - Named MQTT fields will make debugging, dashboards, and client review much easier.

Biggest risk:

Random row splits can overestimate accuracy because neighboring samples from the same gas exposure are highly correlated. The chamber workflow and session/day-based validation are the most important next steps.

## 15. Production Acceptance Criteria

The ML model should not be treated as production-grade until:

- gas classification is validated on unseen sessions/days
- ppm regression is validated against calibrated reference instruments
- error is reported per gas and per board
- tests include humidity and temperature variation
- tests include false-positive vapors
- drift is tested over days/weeks/months
- firmware validates model version, scaler version, and tensor shape
- low-confidence or out-of-distribution readings fail safely

## 16. References

Local reference folder:

```text
reference/web_methodology/
```

Important references:

1. Figaro TGS2610 product information and technical documentation  
   Used to confirm TGS2610 is appropriate for LP gas, especially butane and propane.

2. UCI Gas Sensor Array Drift at Different Concentrations  
   Used as reference for multi-gas, multi-concentration sensor-array datasets.

3. UCI Gas Sensor Array Under Dynamic Gas Mixtures  
   Used as reference for continuous time-series acquisition with gas mixtures and concentration variation.

4. UCI Gas Sensor Array Low-Concentration Dataset  
   Used as reference for datasets with gas labels and concentration labels.

5. Scientific Data long-term drift dataset  
   Used as reference for long-term drift, repeated sessions, baseline/sample/purge phases, and environmental context.

6. Electronic nose review literature  
   Used to support the sensor-array plus pattern-recognition methodology and response-curve feature extraction.

## 17. Client Explanation

The simple explanation:

This system works like a small electronic nose. Instead of trusting one gas sensor, it watches how 8 different MQ sensors respond together. Each gas creates a different response pattern. Machine learning reads that pattern and decides what gas is present, whether there is a leak, how severe it is, and later, after proper calibration, the gas concentration in ppm.

The technical reason:

MQ sensors are sensitive but not selective. Their weakness becomes useful when combined as an array. The model learns the multi-sensor fingerprint, while temperature, humidity, chamber state, and reference ppm make the dataset reliable enough for production training.

## 18. Glossary

| Term | Beginner Meaning |
| --- | --- |
| MQ sensor | Low-cost gas sensor family. It reacts to gases but is not very selective by itself. |
| Sensor array | Several sensors used together so their combined pattern is more useful than one sensor alone. |
| Electronic nose | A system that identifies gases by reading a pattern from multiple sensors. |
| ADC | Analog-to-digital converter. It turns sensor voltage into numbers the ESP32 can process. |
| ESP32-S3 | Microcontroller that reads sensors, runs the ML model, and sends LoRa messages. |
| LoRa | Long-range wireless communication used to send results from sensor nodes. |
| Gateway | Device that receives LoRa data and forwards it to MQTT, dashboard, or database. |
| MQTT | Lightweight messaging protocol often used for IoT dashboards. |
| Tensor | Data container used by ML frameworks. In this project, the input tensor contains sensor features. |
| TensorFlow Lite Micro | Tiny ML runtime used to run models on microcontrollers. |
| Feature | A number given to the model, such as MQ voltage, temperature, humidity, or slope. |
| Scaling | Normalizing values so the model receives inputs in a similar range to training. |
| Softmax | Converts model scores into class probabilities for gas type. |
| Sigmoid | Converts one model score into a probability for yes/no decisions. |
| MSE | Mean squared error. A regression error metric that strongly penalizes large errors. |
| MAE | Mean absolute error. Average absolute difference between predicted and true value. |
| PPM | Parts per million. Gas concentration unit. Current ppm output is only a proxy until calibrated. |
| Reference ppm | PPM measured by a calibrated reference instrument during chamber testing. |
| LEL | Lower explosive limit. `%LEL` is common for flammable gas safety detectors. |
| BME280 | Sensor for temperature, humidity, and pressure. Useful because MQ sensors drift with environment. |
| Chamber state | Current test phase, such as baseline, injection, stable ppm, or recovery. |
| Session-based validation | Testing on different chamber sessions/days instead of randomly mixed rows. |
| Out-of-distribution | Input unlike the training data. The firmware should treat this as uncertain or fail-safe. |
| False positive | Model says gas leak, but the cause is something else such as perfume or alcohol vapor. |
| Missed leak | Dangerous case where gas is present but the system does not detect it. |
