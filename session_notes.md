# Session Notes - Gas Leak Sensor ML Rebuild

Date: 2026-04-30

## Project Goal

Rebuild the gas leak sensor program around production-grade machine learning.

Core idea:

- detect gas using the combined response pattern of 8 MQ sensors
- train one model per board because each MQ board behaves differently
- run inference locally on ESP32-S3 using TensorFlow Lite Micro
- send results through LoRa to cluster head and gateway
- later improve ppm estimation using a controlled chamber and reference measurements

## Main Decisions

### Sensor Input

The model input is the combined 8-MQ sensor vector:

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

The model should learn the multi-sensor fingerprint, not rely on one MQ sensor.

### Gas Label Mapping

Label format:

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

Class IDs:

```text
0 = normal
1 = methane
2 = H2S
3 = butane
4 = propane
5 = CO
```

### Model Outputs

Target production model is multi-task:

```text
gas_type
leak_present
severity
ppm_estimate
```

Current ppm is only a proxy because the current datasets do not contain calibrated ppm labels.

### Dataset Status

Current dataset is prototype/lab-grade, not production-grade.

Limitations:

- around 300 to 450 samples per usable board
- no calibrated ppm labels
- current files mostly contain normal, methane, and butane/LPG-related rows
- no enough H2S, propane, or CO data
- no long-term drift data
- no temperature/humidity variation coverage
- no false-positive interference cases
- no session/day-based validation

Board status:

- `Board1`, `Board3`, `Board4`, `Board5`, `Board6`, `Board7`, `Board9`, `Board10`, `Board11` have usable Excel datasets.
- `Board2-2` has firmware files but no Excel dataset.
- `Board8.xlsx` is empty.

## Chamber Plan

User plans to build a test chamber for production dataset collection.

Chamber components:

- box/chamber with clean-air inlet and outlet
- mechanism to simulate gas leak
- relay-controlled solenoid valve for automatic gas injection
- 8 MQ sensor board
- Figaro TGS2610 reference channel
- BME280 temperature/humidity/pressure sensor
- fan for mixing
- logged valve/fan/chamber states

Recommended chamber states:

```text
baseline_clean_air
gas_injection
mixing_rise
stable_target_ppm
recovery_venting
post_recovery
```

Recommended dataset columns include:

```text
timestamp
board_id
session_id
gas_type
param
target_ppm
reference_ppm
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

## Reference Sensor Decision

The reference sensor is Figaro TGS2610.

Conclusion:

- good for butane/propane/LPG-family reference or cross-check
- not a universal ppm reference for methane, H2S, or CO
- production ppm training for methane, H2S, and CO needs gas-specific calibrated reference instruments

## Firmware Findings

Firmware already uses the combined 8-MQ vector for inference.

Current flow:

```text
read 8 ADC channels
map channels into voltValues[8]
standard-scale using feature_means and feature_stds
copy scaled values into TFLite input tensor
invoke model
decode predicted class and confidence
trigger auto-transmit if class != normal and confidence >= 0.80
```

Current original firmware model contract:

```text
input: 8 float features
output: one softmax vector
old classes: normal, methane, LPG/butane-related
```

Production firmware requirements:

- verify ADC channel mapping vs dataset feature order
- support four model outputs
- support six gas classes
- validate tensor shape and tensor type
- keep float vs int8 export consistent with firmware
- include model/scaler/payload versioning
- fail safe on low confidence or out-of-distribution input

### Sensor Read Flow

The sensor-read path is not a "null process". It is an initialization + acquisition sequence.

```text
power on
  -> setup()
  -> StartADS()
  -> initialize ADS1256 + SPI + pins
  -> initialize mux and channel setup
  -> load EEPROM config
  -> StartLoRa()
  -> enter loop()
  -> modeRunning()
  -> takeDataMQ(ch)
  -> read ADC channel
  -> store raw voltage in voltValues[8]
  -> scale features
  -> machineLearning()
  -> prepareDataToSend()
  -> forward through LoRa
```

Meaning:

- `null` means "not initialized" or "missing pointer/object" in code
- `init` means "hardware and software objects are ready to read"
- the ADC must be initialized before reading the MQ sensors
- the model wrapper must be initialized before calling `predict()`

Important functions:

- [StartADS()](./src/Gasleak/Board3/Gasleak.cpp)
- [takeDataMQ()](./src/Gasleak/Board3/Gasleak.cpp)
- [machineLearning()](./src/Gasleak/Board3/Gasleak.cpp)
- [NeuralNetwork::predict()](./src/Gasleak/Board3/NeuralNetwork.cpp)

### Why Initialization Matters

If initialization is skipped or fails, the board cannot reliably read sensors or run inference.

Typical failure checks:

- ADC not detected
- mux not detected
- model not initialized
- input tensor pointer is null
- output tensor pointer is null

The firmware should stop or fail safe when these checks fail, rather than trying to read or predict from invalid hardware state.

## New Files Created

### Design and Documentation

- `design.md`
  - full project design
  - production ML methodology
  - chamber plan
  - firmware/model contract
  - dataset improvement plan

- `documentation.md`
  - client-facing technical explanation
  - includes architecture, math, model design, chamber method, references, and client explanation

- `session_notes.md`
  - this file

### Improvement Program Folder

Created:

```text
improvement_program/
```

Important files:

- `improvement_program/README.md`
- `improvement_program/simulate.py`
- `improvement_program/train_multitask.py`
- `improvement_program/gasleak_improved/common.py`
- `improvement_program/firmware/payload_contract.h`

Purpose:

- separate experimental improvement path
- does not overwrite existing board firmware
- supports simulation and model training outside current firmware folders

### Simulation Outputs

Simulation was run successfully:

```powershell
python improvement_program\simulate.py
```

Output:

```text
improvement_program/output/simulation/summary.csv
```

Simulation summary:

- gas type accuracy: 1.0 for usable board simulations
- leak present accuracy: 1.0 for usable board simulations
- severity accuracy: about 0.938 to 0.991
- ppm is proxy only

### Training Smoke Test

TensorFlow training/export was smoke-tested:

```powershell
python improvement_program\train_multitask.py --boards Board1 --epochs 1
```

It generated artifacts under:

```text
improvement_program/output/models/Board1/
```

Low metrics are expected because this was only a 1-epoch export test.

## References

Downloaded and linked references were placed in:

```text
reference/web_methodology/
```

Manifest:

```text
reference/web_methodology/README.md
```

Downloaded successfully:

- Figaro TGS2610 product information PDF
- Figaro TGS2610 technical information PDF
- Figaro TGS2610 C00 product page
- Figaro TGS2610 D00 product page
- UCI gas sensor array drift dataset page
- UCI dynamic gas mixture dataset page
- UCI low-concentration gas array dataset page
- UCI home activity monitoring gas sensor page

Some references were blocked by publisher sites during automated download:

- MDPI PDFs returned HTTP 403
- ScienceDirect closed the connection
- Nature returned a cookie/challenge page to PowerShell

Their URLs are recorded in `reference/web_methodology/README.md`.

## Web Methodology Conclusions

Web/literature review supports the methodology:

- use gas sensor array as electronic nose
- use pattern recognition / ML on combined sensor response
- include temperature and humidity because MOS sensors drift with environment
- collect time-series data, not only isolated rows
- label baseline, exposure, stable, and recovery phases
- validate by held-out sessions/days instead of random rows
- account for drift over time
- include false-positive gases/vapors

Important warning:

Random row split can overestimate accuracy because adjacent time samples from the same chamber run are highly correlated.

## LoRa Payload Design

Improved payload is 30 bytes and fits the current 64-byte message limit.

Payload fields:

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

Defined in:

```text
improvement_program/firmware/payload_contract.h
```

## Remaining Open Questions

1. What are the LED and buzzer GPIO pins for each gas sensor board?
2. Should the gateway decode binary payload into named MQTT fields now, or keep raw byte forwarding for the first rebuild?
3. What exact calibrated reference instruments will be used for methane, H2S, and CO?
4. What ppm/severity thresholds should be used per gas?

## Command Issue

User tried:

```powershell
codex login
```

It failed because PowerShell script execution is disabled:

```text
codex.ps1 cannot be loaded because running scripts is disabled on this system
```

Suggested workaround:

```powershell
cmd /c codex login
```

or:

```powershell
codex.cmd login
```

## Next Recommended Work

1. Finalize the chamber data schema.
2. Build the chamber controller/logger.
3. Verify firmware ADC channel mapping against dataset feature order.
4. Collect new time-series chamber data.
5. Add BME280 features to the ML pipeline.
6. Train six-class multi-task models.
7. Validate by held-out sessions/days.
8. Integrate improved model wrapper into firmware.
9. Update gateway to decode named payload fields.

## Continuation - 2026-04-30

Updated the improvement program to match the six-class production gas contract:

```text
0 = normal
1 = methane
2 = H2S
3 = butane / LPG-related gas
4 = propane
5 = CO
```

Changed files:

- `improvement_program/gasleak_improved/common.py`
  - expanded `GAS_LABELS` to six production classes
  - mapped `param:a-b-c-d-e` to the six class IDs
  - added `PAYLOAD_SIZE_BYTES` from the actual Python struct size
- `improvement_program/train_multitask.py`
  - changed `gas_type` model output from 3 classes to `len(GAS_LABELS)`
  - changed gas confusion matrices to include all six classes
  - added fallback summary filenames when Windows locks `all_boards_summary.csv/json`
- `improvement_program/simulate.py`
  - changed gas confusion matrices to include all six classes
  - replaced hardcoded payload size with `PAYLOAD_SIZE_BYTES`
- `improvement_program/firmware/payload_contract.h`
  - expanded gas enum to six classes
  - corrected `GASLEAK_PAYLOAD_SIZE` from 30 to 32 bytes
- `improvement_program/README.md`
  - documented the six-class contract and current dataset limitation

Important correction:

The improved binary payload is 32 bytes, not 30 bytes. The layout is:

```text
4 uint8 fields
4 uint16 fields
1 uint32 field
8 int16 MQ millivolt fields
```

Validation run:

```powershell
python improvement_program\simulate.py --boards Board1
python improvement_program\train_multitask.py --boards Board1 --epochs 1
```

Results:

- Board1 simulation completed successfully with 32-byte decoded payloads.
- TensorFlow one-epoch smoke test completed and exported artifacts.
- Windows blocked overwriting `improvement_program/output/reports/all_boards_summary.csv`; fallback files were written:
  - `all_boards_summary_20260430_063955.csv`
  - `all_boards_summary_20260430_063955.json`

## Documentation Continuation - 2026-04-30

Added engineering critique and improvement notes to:

- `design.md`
- `documentation.md`

Key critique added:

- Current ML direction is correct, but current data is prototype-grade.
- Production claims should wait until chamber data, calibrated references, and session/day validation exist.
- Random row splits can overestimate accuracy because adjacent samples from the same exposure run are correlated.
- `ppm_estimate` is still a proxy until calibrated `reference_ppm` labels exist.
- Firmware should support uncertain / fail-safe behavior for low-confidence or out-of-distribution readings.
- ADC feature order must be verified against:

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

Corrected documentation payload size:

- Previous docs said improved payload was `30 bytes`.
- Correct size is `32 bytes`.
- Reason:

```text
4 uint8 fields = 4 bytes
4 uint16 fields = 8 bytes
1 uint32 field = 4 bytes
8 int16 MQ millivolt fields = 16 bytes
total = 32 bytes
```

Added gas test chamber hardware recommendations to `design.md` and `documentation.md`:

- Build a controlled-flow chamber, not a sealed box with manual gas injection.
- Suggested chamber: `10 L` to `30 L` clear acrylic or polycarbonate box with gasketed lid.
- Include clean-air inlet, exhaust outlet, sealed cable glands, internal low-voltage brushless fan, BME280 or SHT31/SHT35, 8-MQ board, and Figaro TGS2610 for LPG-family cross-checking.
- Keep solenoid valves, relays, switching contacts, and spark-producing electronics outside the gas space where possible.
- Use regulator plus needle valve, flow meter, or mass flow controller.
- Add independent external gas alarms and manual shutoff.

Recommended staged chamber approach:

1. Start with butane / propane / LPG-family tests.
2. Add methane only with calibrated methane or LEL reference detector.
3. Treat CO and H2S as lab-supervised upgrades only.

Reference instrument recommendation:

| Gas | Minimum reference recommendation |
| --- | --- |
| butane / LPG | calibrated LPG or LEL detector; TGS2610 as reference/cross-check |
| propane | calibrated LPG or LEL detector; TGS2610 as reference/cross-check |
| methane | calibrated methane or LEL detector |
| CO | calibrated CO detector/logger or CO analyzer |
| H2S | calibrated H2S detector/logger or H2S analyzer |

Added ML improvement roadmap to `design.md` and `documentation.md`:

- Add BME280 temperature, humidity, and pressure features.
- Add baseline-normalized MQ features:
  - clean-air baseline
  - delta from baseline
  - ratio to baseline
- Add time-window features:
  - last `10` to `60` seconds
  - moving average
  - slope / rate of change
  - max response
  - recovery slope
  - time since chamber state transition
- Handle class imbalance:
  - collect more minority-class data
  - use class weights
  - oversample only inside the training split
  - never oversample validation/test data
- Add unknown/interference handling:
  - low confidence -> uncertain
  - out-of-distribution -> fail safe
  - collect alcohol, perfume, smoke, cleaning chemicals, humid air, hot air, dust, and exhaust-like false-positive cases
- Use stronger metrics:
  - precision per gas
  - recall per gas
  - F1 per gas
  - false alarm rate
  - missed leak rate
  - leak-present precision/recall
  - ppm MAE per gas once calibrated ppm exists
  - metrics split by board, session/day, humidity, and temperature range

Suggested future ESP32-S3 model shape:

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

## Reference Sensor Alternatives - 2026-04-30

Recommendation: do not replace Figaro TGS2610 with one universal sensor. Use gas-specific reference sensors or calibrated instruments.

Recommended setup:

```text
MQ array = ML input sensors
NevadaNano MPS or calibrated LEL detector = flammable gas reference
Alphasense CO or calibrated CO logger = CO reference
Alphasense H2S or calibrated H2S logger = H2S reference
BME280 or SHT35 = environment reference
optional VOC sensor = interference context
```

Reference recommendations by gas:

| Target gas | Recommended reference option |
| --- | --- |
| butane / LPG | NevadaNano MPS flammable gas sensor or calibrated LPG/LEL detector |
| propane | NevadaNano MPS flammable gas sensor or calibrated LPG/LEL detector |
| methane | NevadaNano MPS flammable gas sensor or calibrated methane/LEL detector |
| CO | Alphasense CO electrochemical sensor or calibrated CO detector/logger |
| H2S | Alphasense H2S electrochemical sensor or calibrated H2S detector/logger |
| VOC / false positives | Sensirion SGP40 or similar VOC sensor as interference context only |

Notes added to `design.md` and `documentation.md`:

- TGS2610 remains useful for LPG-family cross-checking.
- NevadaNano MPS is useful for flammable-gas work because `%LEL` is relevant to leak safety.
- CO and H2S should use electrochemical reference sensors or calibrated detectors/loggers.
- A complete calibrated multi-gas detector/logger is preferred for trusted labels and safety alarms.
- Raw sensors still need calibration, drift management, and environmental compensation.
- VOC sensors should not be used as ppm ground truth; use them only for interference context.

## Beginner Documentation Update - 2026-04-30

User asked whether `documentation.md` covers the full project and is beginner-friendly.

Assessment:

- Technical completeness: good.
- Beginner friendliness: medium before update.
- Client presentation readiness: good, but improved with a front section.

Changes made:

- Added `Read This First` section at the top of `documentation.md`.
- Added beginner reading path.
- Added simple system block diagram.
- Added training data flow diagram.
- Added on-device inference flow diagram.
- Added `Glossary` section covering:
  - MQ sensor
  - sensor array
  - electronic nose
  - ADC
  - ESP32-S3
  - LoRa
  - gateway
  - MQTT
  - tensor
  - TensorFlow Lite Micro
  - feature
  - scaling
  - softmax
  - sigmoid
  - MSE
  - MAE
  - ppm
  - reference ppm
  - LEL
  - BME280
  - chamber state
  - session-based validation
  - out-of-distribution
  - false positive
  - missed leak
- Created `present.md` as a shorter client-facing overview separate from the longer technical documentation.

Purpose:

- `documentation.md` remains the complete technical document.
- `present.md` is the client/beginner-friendly summary.

## Firmware Design Document - 2026-04-30

Created:

- `firmware_design.md`

Purpose:

- standalone firmware and improvement design
- explains current firmware behavior
- explains improved firmware target
- compares current vs improved firmware
- documents firmware runtime states
- documents chamber dataset states
- documents dataset schema
- documents training pipeline
- documents how to run simulation and training
- documents model integration steps
- documents 32-byte LoRa payload contract
- documents gateway decoding recommendation
- documents validation requirements
- documents open firmware questions

Important sections:

- Current Firmware Summary
- Improved Firmware Goal
- What Is Improved
- Fixed Feature Order
- Gas Class Contract
- Improved Runtime States
- Chamber Dataset States
- Dataset Design
- Feature Improvements
- Training Pipeline
- Running The Improvement Program
- Firmware Model Integration
- LoRa Payload Contract
- Gateway Improvement
- Validation
- Open Firmware Questions
- Recommended Implementation Order

Key distinction documented:

- Firmware runtime states are device states such as `BOOT`, `INIT_ADC`, `RUN_INFERENCE`, `TRANSMIT`, and `ERROR_SAFE`.
- Chamber dataset states are experiment labels such as `baseline_clean_air`, `gas_injection`, `stable_target_ppm`, and `recovery_venting`.
