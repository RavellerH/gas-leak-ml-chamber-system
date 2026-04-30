# Gas Leak Sensor Rebuild Design

## Goal

Rebuild the gas leak sensor program for the existing board folders so each gas sensor board has its own trained model, runs local gas leak inference, triggers local alarm outputs when configured, and sends complete inference plus sensor data through LoRa to the cluster head and gateway.

The rebuild keeps the current deployment shape, while new experimental work is kept in `improvement_program/` until it is ready to be integrated:

- Sensor nodes: `src/Gasleak/Board1`, `Board2-2`, `Board3` through `Board7`, and `Board9` through `Board11`
- Cluster head: `src/loraMeshClusterHead_ESP32` or `src/loraMeshClusterHead`
- Gateway: `src/loraMeshGateway`
- Build system: PlatformIO
- ML runtime: TensorFlow Lite Micro

`Board2-2` is excluded from current training because no `Board2-2.xlsx` dataset exists. `Board8` is excluded because `src/Gasleak/Board8/Board8.xlsx` is empty.

## Current Inputs

### Reference Papers

The rebuild uses the multi-task learning papers as guidance for model structure:

- shared feature extractor
- multiple output heads
- gas classification and leak detection as primary tasks
- severity and concentration estimation as secondary tasks

### Dataset

Usable current Excel datasets:

- `src/Gasleak/Board1/Board1.xlsx`
- `src/Gasleak/Board3/Board3.xlsx`
- `src/Gasleak/Board4/Board4.xlsx`
- `src/Gasleak/Board5/Board5.xlsx`
- `src/Gasleak/Board6/Board6.xlsx`
- `src/Gasleak/Board7/Board7.xlsx`
- `src/Gasleak/Board9/Board9.xlsx`
- `src/Gasleak/Board10/Board10.xlsx`
- `src/Gasleak/Board11/Board11.xlsx`

Known dataset gaps:

- `src/Gasleak/Board2-2/` has firmware files but no Excel dataset.
- `src/Gasleak/Board8/Board8.xlsx` is empty.

Feature columns:

- `MQ135V`
- `MQ2V`
- `MQ3V`
- `MQ4V`
- `MQ7V`
- `MQ5V`
- `MQ6V`
- `MQ8V`

Label source:

- `sequence` field containing `param:a-b-c-d-e`

Known mapping:

- `param:0-0-0-0-0` -> normal / clean air
- `param:1-0-0-0-0` -> methane
- `param:0-1-0-0-0` -> H2S
- `param:0-0-1-0-0` -> butane / LPG-related gas
- `param:0-0-0-1-0` -> propane
- `param:0-0-0-0-1` -> CO

Parameter position mapping:

- `a` = methane
- `b` = H2S
- `c` = butane / LPG-related gas
- `d` = propane
- `e` = CO

## Model Design

### One Model Per Board

Each board gets its own model because each MQ sensor set behaves differently. The model, scaler, and reports are generated per board.

Output files per board:

- `gasleak_model.tflite`
- `model_data.cc`
- `model_data.h`
- `scaler_params.cc`
- `scaler_params.h`

### Multi-Task Outputs

The model uses one shared input body and four output heads:

1. `gas_type`
   - classification
   - classes: `normal`, `methane`, `h2s`, `butane`, `propane`, `co`

2. `leak_present`
   - binary classification
   - values: `0 = no`, `1 = yes`

3. `severity`
   - classification
   - classes: `normal`, `low`, `medium`, `high`

4. `ppm_estimate`
   - regression
   - unit: ppm proxy for current datasets
   - unit: real ppm after chamber data collection includes calibrated `reference_ppm`

### Important PPM Limitation

The current Excel datasets do not contain calibrated ppm labels. Because of that, true ppm regression cannot be trained yet.

For the first rebuild, `ppm_estimate` is a board-local proxy derived from relative sensor response strength. It is useful for ranking low versus high response on the same board, but it is not a calibrated gas concentration.

To make true ppm output, the dataset needs a real ppm column for each sample.

### Severity Derivation

Severity is derived per board:

- normal samples -> `normal`
- gas samples are ranked by relative sensor response
- lower third -> `low`
- middle third -> `medium`
- upper third -> `high`

This is a temporary derived label until real severity or ppm labels are available.

## Training Pipeline

Improvement training and simulation scripts:

- `improvement_program/simulate.py`
- `improvement_program/train_multitask.py`
- shared helpers in `improvement_program/gasleak_improved/common.py`

Responsibilities:

- load each board dataset
- parse labels from `sequence`
- derive `leak_present`, `severity`, and ppm proxy
- scale features using `StandardScaler`
- train one TensorFlow model per board
- export float TFLite model for TensorFlow Lite Micro firmware compatibility
- generate C model files
- generate scaler C files
- generate reports

Reports:

- `improvement_program/output/simulation/<Board>/metrics.json`
- `improvement_program/output/simulation/<Board>/sample_decoded_lora_packets.csv`
- `improvement_program/output/simulation/<Board>/gas_type_confusion_matrix.csv`
- `improvement_program/output/simulation/<Board>/severity_confusion_matrix.csv`
- `improvement_program/output/simulation/summary.csv`
- `improvement_program/output/models/<Board>/gasleak_model.tflite`
- `improvement_program/output/models/<Board>/model_data.cc`
- `improvement_program/output/models/<Board>/model_data.h`
- `improvement_program/output/models/<Board>/scaler_params.cc`
- `improvement_program/output/models/<Board>/scaler_params.h`
- `improvement_program/output/reports/<Board>/metrics.json`
- `improvement_program/output/reports/all_boards_summary.csv`

## ML Improvement Roadmap

The current model path is a good starting point, but the production model should improve in stages so accuracy gains come from better signal and validation instead of only a larger neural network.

### Feature Improvements

The next model input should include more than one static 8-sensor row. Recommended feature groups:

- raw/scaled 8 MQ voltages
- BME280 temperature, humidity, and pressure
- clean-air baseline for each MQ sensor
- delta from clean-air baseline
- ratio to clean-air baseline
- moving average over a short window
- slope/rate of change over a short window
- maximum response in the window
- recovery slope during venting
- time since injection or time since chamber state transition

Recommended first time-window design:

```text
window length: last 10 to 60 seconds
window step: 1 sample or fixed logger interval
features: current value, mean, slope, max, delta from baseline
```

This is important because MQ sensors respond and recover slowly. A time-window model or engineered window features can distinguish baseline drift, rising gas exposure, stable exposure, and recovery better than a single-row model.

### Class Imbalance Handling

Production training must handle uneven class counts. Normal samples are usually easier to collect than gas exposure samples, and H2S, CO, propane, or methane may have fewer examples than butane/LPG.

Recommended handling:

- collect more minority-class data before changing the model architecture
- use class weights for `gas_type` and `severity`
- oversample minority classes only within the training split
- do not oversample validation or test data
- report per-class metrics, not only total accuracy
- keep a confusion matrix for every board

Missing gases should not be treated as solved classes. If H2S, propane, or CO have no real samples, the model contract can still include their class IDs, but production claims for those gases must remain blocked until data exists.

### Unknown And Interference Handling

The model should not be forced to confidently choose one of the six gas classes when the input is outside the training distribution.

Recommended options:

- confidence threshold: below threshold, output `uncertain`
- out-of-distribution score based on distance from training feature range
- optional `interference` label for non-target vapors if the product needs to distinguish false positives
- fail-safe payload flag for invalid sensor values, stale readings, or ADC saturation

False-positive/interference data should include alcohol vapor, perfume, smoke, cleaning chemicals, humid air, hot air, dust, and exhaust-like conditions.

### Stronger Metrics

Production reports should include:

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
- metrics split by board
- metrics split by session/day
- metrics split by humidity and temperature range

The most important safety metric is missed leak rate. A high total accuracy can hide bad recall on rare gas classes.

### Small ESP32-S3 Model Shape

Keep the embedded model small enough for TensorFlow Lite Micro. A practical first production architecture is:

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

This should remain small enough for ESP32-S3 testing while being stronger than the current single-row 8-feature model.

## Firmware Design

### Current Firmware Inference Path

The existing gas sensor firmware already uses the combined 8-MQ sensor pattern as the ML input.

Current flow in each board firmware:

1. `setup()` creates the TensorFlow Lite Micro wrapper with `network = new NeuralNetwork()`.
2. `NeuralNetwork` loads `model_tflite` from `model_data.cc`.
3. TensorFlow Lite Micro allocates tensors using a static tensor arena, currently around `40 KB`.
4. Running mode reads one ADS1256 channel at a time.
5. `takeDataMQ()` maps the physical ADC channel into the `voltValues[8]` feature array.
6. `machineLearning()` standardizes each voltage:
   - `(voltValues[i] - feature_means[i]) / feature_stds[i]`
7. The scaled 8-feature array is copied into the model input tensor.
8. `network->predict()` invokes the model.
9. The original firmware expects one softmax output and returns:
   - `predicted_class`
   - `highest_confidence_score`
10. The node switches to auto-transmit when:
   - `predicted_class != 0`
   - confidence is at least `0.80`

The original LoRa payload only sends:

- predicted class
- confidence
- inference time

It does not yet send all 8 MQ readings, severity, ppm estimate, or six-class gas type.

### Current Firmware Model Contract

The current original model contract is:

- input tensor: 8 float features
- preprocessing: standard scaling with `feature_means[8]` and `feature_stds[8]`
- output tensor: one class-probability vector
- old class layout:
  - `0 = normal`
  - `1 = methane`
  - `2 = LPG / butane-related`

Production ML must replace this with a documented contract:

- input feature order:
  - `MQ135V`
  - `MQ2V`
  - `MQ3V`
  - `MQ4V`
  - `MQ7V`
  - `MQ5V`
  - `MQ6V`
  - `MQ8V`
- output heads:
  - `gas_type`
  - `leak_present`
  - `severity`
  - `ppm_estimate`
- gas class layout:
  - `0 = normal`
  - `1 = methane`
  - `2 = h2s`
  - `3 = butane`
  - `4 = propane`
  - `5 = co`

The ADC channel-to-feature mapping in firmware must be verified against the dataset column order before production training. If the physical channel mapping and dataset order differ, the model can appear accurate in training but fail on-device.

### Production ML Firmware Requirements

For production-grade ML, firmware should enforce these checks before trusting a prediction:

- all 8 MQ readings are fresh
- all 8 MQ readings are within valid voltage range
- scaler parameters match the model version
- input tensor type matches export type
- output tensor count and output dimensions match the expected model contract
- model version and payload version are included in reports and firmware constants
- confidence threshold is applied before alarm or auto-transmit
- out-of-distribution or invalid readings fail safe

The current firmware uses float tensors. If a future model is exported as full int8, the firmware must explicitly handle input/output quantization instead of writing floats into the tensor buffer.

### Sensor Node Flow

1. Read 8 MQ voltage channels.
2. Scale features using board-specific scaler params.
3. Run TensorFlow Lite Micro inference.
4. Decode outputs:
   - gas type
   - gas confidence
   - leak probability
   - severity
   - severity confidence
   - ppm proxy
5. Trigger local alarm if:
   - gas type is not normal
   - gas confidence is at least `0.80`
6. Send full payload through LoRa.

### Alarm Outputs

The buzzer and LED pins are not known yet.

Firmware should support optional compile-time pin definitions:

- `GASLEAK_LED_PIN`
- `GASLEAK_BUZZER_PIN`

If either value is not configured, that output stays disabled.

Alarm threshold:

- keep current threshold: `0.80`

### LoRa Payload

The current transport supports up to 64 bytes in `struct_message.message`.

The rebuilt sensor node should send a compact binary ML payload:

| Field | Type | Description |
| --- | --- | --- |
| payload version | `uint8_t` | starts at `1` |
| gas type | `uint8_t` | `0 normal`, `1 methane`, `2 h2s`, `3 butane`, `4 propane`, `5 co` |
| leak present | `uint8_t` | `0 no`, `1 yes` |
| severity | `uint8_t` | `0 normal`, `1 low`, `2 medium`, `3 high` |
| gas confidence | `uint16_t` | confidence x1000 |
| leak probability | `uint16_t` | probability x1000 |
| severity confidence | `uint16_t` | confidence x1000 |
| ppm estimate | `uint16_t` | rounded ppm proxy now; real ppm after calibrated collection |
| inference time | `uint32_t` | microseconds |
| MQ voltages | `int16_t[8]` | millivolts |

Total payload size:

- 32 bytes

This fits the existing 64-byte limit and includes all requested outputs plus all 8 sensor readings.

### Cluster Head

The cluster head continues forwarding LoRa messages using the existing routing format.

No ML decoding is required at the cluster head for the first rebuild.

### Gateway

The gateway currently publishes received message bytes to MQTT as JSON. That can remain unchanged for the first rebuild.

Optional later improvement:

- decode payload version `1` at the gateway
- publish named fields like `gasType`, `leakPresent`, `severity`, `ppmEstimate`, and `mqVoltages`

## Validation

Training validation:

- gas type accuracy
- leak present accuracy
- severity accuracy
- ppm proxy MAE
- gas type confusion matrix
- severity confusion matrix

Firmware validation:

- PlatformIO build for each active gas board
- PlatformIO build for cluster head
- PlatformIO build for gateway
- serial output sanity check on one board if hardware is connected
- LoRa payload size check

## Known Risks

1. PPM is not real calibrated ppm yet.
   - The current dataset does not contain ppm labels.
   - The first rebuild can only output a relative ppm proxy.

2. Severity is derived, not directly measured.
   - It should be replaced when labeled severity or ppm is available.

3. Buzzer and LED pins are unknown.
   - Firmware will keep them optional until the wiring is known.

4. Board-specific datasets are small.
   - Most boards appear to have around 300 to 450 samples.
   - Reports should be interpreted carefully.

5. Board8 cannot be trained.
   - The dataset file is empty.

6. The current dataset is not production-grade yet.
   - It is suitable for prototype and lab validation, but not enough for safety-critical production deployment.
   - Most usable boards only have around 300 to 450 samples.
   - The current dataset files inspected so far only clearly contain `normal`, `methane`, and butane/LPG-related rows, even though the command format supports `H2S`, `propane`, and `CO`.
   - It does not include enough real-world variation such as humidity, temperature, airflow, sensor warm-up state, power variation, enclosure effects, and sensor aging.
   - It does not include enough false-positive interference cases such as alcohol vapor, perfume, smoke, cleaning chemicals, exhaust, hydrogen, humid air, hot air, or dust.
   - It does not contain calibrated ppm ground-truth measurements, so concentration output must remain a proxy until real ppm labels are collected.

## Engineering Critique And Improvements

The overall ML direction is correct: use the 8-MQ array as an electronic-nose sensor pattern, train per-board models, run inference locally on ESP32-S3, and transmit compact LoRa results. The main critique is that the current dataset is still too weak for production claims. Until controlled chamber data exists, the system should be described as prototype or lab validation, not production-ready safety instrumentation.

Highest-priority improvements:

1. Fix the data collection first.
   - Collect time-series chamber data instead of isolated rows.
   - Include `baseline_clean_air`, `gas_injection`, `mixing_rise`, `stable_target_ppm`, `recovery_venting`, and `post_recovery`.
   - Log `session_id`, `board_id`, `gas_type`, `reference_ppm`, BME280 readings, valve states, fan state, and chamber state.
   - Validate by held-out sessions or days instead of random row splits.

2. Do not claim real ppm yet.
   - Current `ppm_estimate` is only a response proxy.
   - Real ppm training requires calibrated reference instruments per gas.
   - TGS2610 is useful for LPG, butane, and propane-family work, but it is not enough for methane, H2S, or CO.

3. Keep the six-class contract now.
   - The firmware, payload, and model output should keep the IDs `normal`, `methane`, `H2S`, `butane`, `propane`, and `CO`.
   - Current data may only populate normal, methane, and butane/LPG rows, but keeping the full contract prevents later firmware ID churn.

4. Add out-of-distribution protection.
   - Low-confidence predictions should become uncertain or fail-safe, not confident alarms.
   - Firmware should detect impossible voltages, disconnected sensors, ADC saturation, stale readings, and large drift from the training range.

5. Verify firmware sensor order.
   - ADC channel order must exactly match `MQ135V`, `MQ2V`, `MQ3V`, `MQ4V`, `MQ7V`, `MQ5V`, `MQ6V`, `MQ8V`.
   - A swapped channel can make training metrics look valid while on-device inference fails.

6. Version every interface.
   - Include model version, scaler version, payload version, board ID, dataset version, and chamber session metadata.
   - Versioning is required to debug field devices and compare models across boards.

7. Improve gateway decoding.
   - Raw byte forwarding is acceptable for early integration.
   - The gateway should soon decode payload version `1` into named MQTT fields such as `gasType`, `leakPresent`, `severity`, `ppmEstimate`, and `mqVoltages`.

The biggest technical risk is overestimating model accuracy because adjacent samples from one exposure run are highly correlated. A model that performs well on a random row split may still fail on a new day, board, concentration, humidity level, or chamber run.

## Dataset Improvement Plan

The gas leak sensor should detect gas using the combined pattern from all 8 MQ sensor readings, not from a single sensor. Each sample should continue to include:

- `MQ135V`
- `MQ2V`
- `MQ3V`
- `MQ4V`
- `MQ7V`
- `MQ5V`
- `MQ6V`
- `MQ8V`

For production-grade reliability, each board should collect a larger and more diverse dataset:

- at least 1000 to 3000+ samples per class per board
- repeated samples across multiple days and calibration sessions
- clean air baseline under different temperature and humidity conditions
- methane, H2S, butane, propane, and CO at multiple known concentration levels
- calibrated ppm labels from a reference gas meter or controlled gas chamber
- false-positive cases such as alcohol, perfume, smoke, cleaning chemicals, exhaust, humid air, and dust
- sensor warm-up data and long-running drift data
- data from normal operation inside the final enclosure

The current datasets can validate the machine-learning approach, but the model should not be treated as production-safe until these dataset improvements are complete.

## Production ML Methodology Notes

Web and literature review confirms that the project direction is correct: the gas leak detector should be treated as an electronic-nose system. The 8 MQ sensors form a cross-sensitive metal-oxide sensor array, and the ML model should learn the combined response fingerprint instead of relying on a single sensor.

Downloaded and linked methodology references are stored in:

- `reference/web_methodology/`
- `reference/web_methodology/README.md`

For production-grade ML, the methodology must be stronger than the current prototype dataset and random row-based evaluation.

Required methodology changes:

- Use the full 8-MQ sensor pattern as the primary model input.
- Add BME280 temperature, humidity, and pressure as context features or compensation inputs.
- Collect and model time-series behavior, not only isolated rows.
- Preserve chamber state labels:
  - `baseline_clean_air`
  - `gas_injection`
  - `mixing_rise`
  - `stable_target_ppm`
  - `recovery_venting`
  - `post_recovery`
- Extract production features from response curves:
  - baseline-normalized voltage
  - delta from baseline
  - slope / rate of change
  - maximum response
  - response at selected times
  - area under response curve
  - recovery slope
  - time since injection
- Evaluate by session, day, board, gas concentration, and environmental condition, not only random row split.
- Include drift testing across days/weeks/months.
- Include humidity and temperature variation.
- Include false-positive interference cases.
- Include out-of-distribution checks and fail-safe behavior.

Important validation rule:

Random row splits can overestimate model performance because neighboring time samples from the same chamber run are highly correlated. Production validation should hold out complete sessions or days so the test set represents unseen operating conditions.

The current chamber plan is aligned with established electronic-nose practice if it logs:

- 8 MQ sensor voltages
- BME280 environmental readings
- reference sensor raw values
- reference ppm where calibrated
- gas valve state
- clean-air valve state
- outlet state
- fan state
- chamber state
- timestamps for every state transition

Reference sensor note:

- Figaro TGS2610 is appropriate for butane/propane/LPG-family reference or cross-check work.
- It should not be treated as a universal ppm reference for methane, H2S, or CO.
- For methane, H2S, and CO, production ppm training requires gas-specific calibrated reference instruments.

## PPM Ground-Truth Collection Design

The current datasets do not have ppm labels. For the improved production dataset, ppm must be measured during data collection, not inferred afterward.

### Recommended Setup

Use a controlled test chamber so the MQ board and a reference gas measurement device observe the same air. The planned chamber is an open-flow box with clean-air inlet, outlet, automatic gas injection through relay-controlled solenoid valve, the 8-MQ sensor board, Figaro TGS2610 reference channel, and BME280 environmental sensor.

Minimum equipment:

- sealed or semi-sealed test chamber with known volume
- safe gas inlet and outlet
- small internal fan for air mixing
- reference gas meter or calibrated gas detector for the target gas
- BME280 temperature, humidity, and pressure sensor
- data logger or PC connected to the sensor board
- exhaust/ventilation path
- gas safety controls appropriate for flammable and toxic gases
- relay-controlled solenoid valve for gas injection
- logged clean-air inlet and outlet state
- internal mixing fan with logged fan state

For methane, butane, and propane, use a flammable-gas-safe setup. For H2S and CO, do not run tests without proper safety-rated equipment, ventilation, and supervision because both are toxic.

### Recommended Chamber Hardware

The first chamber should be built as a controlled-flow test chamber, not a sealed box with manual gas injection. Controlled flow gives safer operation, repeatable concentration changes, and cleaner ML labels.

Recommended first build:

- `10 L` to `30 L` clear acrylic or polycarbonate chamber
- gasketed lid
- clean-air inlet bulkhead
- exhaust outlet bulkhead
- sealed cable glands for sensor and fan wiring
- internal non-sparking low-voltage brushless DC fan for mixing
- 8-MQ sensor board under test
- BME280 or SHT31/SHT35 environmental sensor
- Figaro TGS2610 reference channel for LPG-family tests
- normally-closed gas solenoid valve outside the chamber
- normally-closed clean-air solenoid valve outside the chamber
- controlled exhaust valve or exhaust fan
- pressure regulator for gas cylinder
- needle valve, flow meter, or mass flow controller
- calibrated reference detector or analyzer for the target gas
- independent external safety alarm for the gas being tested
- manual shutoff valve
- ventilation or exhaust path to a safe outdoor or fume-extraction route

Avoid placing relays, mechanical switching contacts, loose high-current wiring, or spark-producing parts inside the gas space. Keep valves and switching electronics outside the chamber where possible, and use tubing to bring test gas into the chamber.

Recommended flow layout:

```text
clean air / calibration gas
  -> regulator
  -> flow controller or needle valve
  -> normally-closed solenoid
  -> chamber with mixing fan
  -> exhaust outlet
  -> safe ventilation
```

Recommended staged build:

1. Start with LPG-family testing only.
   - Use butane, propane, or LPG-family gas at low concentration.
   - Use TGS2610 as a reference/cross-check channel.
   - Add a calibrated LPG/LEL detector if concentration labels are required.
   - Collect clean air, gas exposure, recovery, and false-positive samples.

2. Add methane only after LPG-family testing works.
   - Use a calibrated methane or LEL detector as the reference instrument.
   - Do not assume TGS2610 provides methane ppm ground truth.

3. Treat CO and H2S as lab-supervised upgrades only.
   - Do not test CO or H2S in a DIY chamber without certified reference instruments, ventilation, gas-rated safety equipment, and supervision.
   - Use independent CO or H2S alarms outside the chamber.
   - Use gas-specific safe concentration limits and emergency procedures.

Recommended reference instruments by gas:

| Gas | Minimum reference recommendation |
| --- | --- |
| butane / LPG | calibrated LPG or LEL detector; TGS2610 as reference/cross-check |
| propane | calibrated LPG or LEL detector; TGS2610 as reference/cross-check |
| methane | calibrated methane or LEL detector |
| CO | calibrated CO detector/logger or CO analyzer |
| H2S | calibrated H2S detector/logger or H2S analyzer |

Recommended external safety monitors:

- LPG/LEL alarm for methane, butane, propane, and LPG-family tests
- CO alarm for any CO work
- H2S alarm for any H2S work
- room ventilation or extraction fan
- manual emergency shutoff

Do not estimate ppm by spraying an unknown amount of gas into the chamber. For ML training, concentration labels should come from a calibrated reference instrument or from certified calibration gas with controlled flow.

### Review Of Current Chamber Drawing

The file `Gas Test Chamber Design.jpeg` is a good mechanical starting point. It shows a clear acrylic chamber, removable front/side plate, mounting plate for the detector, exhaust hole, locking/clamping concept, and separated acrylic/plate/block components.

Required revisions before using it as an ML data-collection chamber:

1. Convert it from a mostly sealed enclosure into a controlled-flow chamber.
   - The drawing shows an exhaust hole, but it does not clearly show clean-air inlet, gas inlet, solenoid connection, flow fitting, tubing port, cable gland, or reference meter sampling port.
   - Add labeled ports:
     - `gas_inlet`
     - `clean_air_inlet`
     - `exhaust_outlet`
     - `cable_gland`
     - `reference_meter_sampling_port`

2. Reconsider chamber volume.
   - The drawing appears close to `400 mm x 500 mm x 300 mm`, around `60 L`.
   - This can work, but it needs more gas, longer mixing time, and longer recovery.
   - For early ML testing, `10 L` to `30 L` is more practical.

3. Add internal mixing fan details.
   - The drawing does not clearly show a fan mount.
   - Add a small low-voltage brushless fan inside the chamber.
   - Log fan state in every dataset row.

4. Add gasket and sealing detail.
   - The lock/clamp does not guarantee repeatable sealing by itself.
   - Add a silicone or rubber gasket around the door/plate contact area.
   - Add a gasket groove or gasket strip detail to the mechanical drawing.

5. Avoid wood inside the chamber.
   - The `balok kayu penyangga` can absorb gas or vapor and slow recovery.
   - Replace internal wood with acrylic, aluminum, stainless steel, PTFE, or another low-absorption material.

6. Define sensor and reference layout.
   - Mark the positions of the 8-MQ board, TGS2610/reference sensor, and BME280 or SHT31/SHT35.
   - Keep the BME280 away from direct hot airflow from MQ sensor heaters.

7. Keep risky electronics outside the gas space.
   - Put solenoids, relays, high-current switching, and spark-producing parts outside the chamber.
   - Pass only low-voltage sensor and fan wiring through sealed cable glands.

Recommended revision flow:

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

Conclusion: the current drawing is mechanically promising, but it needs controlled inlet/outlet flow, sealing detail, fan mounting, cable glands, safer internal materials, and explicit sensor/reference locations before it can produce reliable ML training data.

### Current Reference Sensor: Figaro TGS2610

The planned reference sensor is Figaro TGS2610. This is useful for LP gas work, especially butane and propane, but it should not be treated as a universal calibrated ppm reference for every target gas.

TGS2610 is designed for LP gas detection, with high sensitivity to propane and butane. It is therefore appropriate as a chamber reference or cross-check for:

- butane
- propane
- LPG-family leak tests

It is not sufficient as the only reference sensor for:

- methane
- H2S
- CO

For those gases, the production dataset should use gas-specific calibrated reference meters or certified gas analyzers. Otherwise, the model may learn a response relative to the TGS2610 instead of true ppm concentration.

Recommended role for TGS2610:

- use it as the reference channel for butane/propane experiments
- log its raw output and calculated resistance ratio alongside the 8 MQ sensors
- do not use it as the ppm label source for H2S or CO
- do not use it as the only ppm label source for methane unless it has been externally calibrated and validated for methane in the intended concentration range

Future dataset columns should include both the reference ppm and the reference sensor raw values:

- `reference_sensor_model`
- `reference_sensor_raw`
- `reference_sensor_voltage`
- `reference_sensor_rs`
- `reference_sensor_ro`
- `reference_sensor_rs_ro`
- `reference_ppm`

### Recommended Reference Alternatives

Figaro TGS2610 should remain an LPG-family cross-check, not the universal reference for the full project. For production dataset labels, use gas-specific reference sensors or calibrated instruments.

Recommended reference setup:

```text
MQ array = ML input sensors
NevadaNano MPS or calibrated LEL detector = flammable gas reference
Alphasense CO or calibrated CO logger = CO reference
Alphasense H2S or calibrated H2S logger = H2S reference
BME280 or SHT35 = environment reference
optional VOC sensor = interference context
```

Recommended alternatives by target:

| Target gas | Recommended reference option |
| --- | --- |
| butane / LPG | NevadaNano MPS flammable gas sensor or calibrated LPG/LEL detector |
| propane | NevadaNano MPS flammable gas sensor or calibrated LPG/LEL detector |
| methane | NevadaNano MPS flammable gas sensor or calibrated methane/LEL detector |
| CO | Alphasense CO electrochemical sensor or calibrated CO detector/logger |
| H2S | Alphasense H2S electrochemical sensor or calibrated H2S detector/logger |
| VOC / false positives | Sensirion SGP40 or similar VOC sensor as interference context only |

Notes:

- NevadaNano MPS is useful for flammable gases because it reports combustible-gas response in `%LEL`, which is practical for leak safety.
- Electrochemical sensors are more appropriate for CO and H2S ppm reference labels than LPG-focused MOS sensors.
- A complete calibrated instrument such as a multi-gas detector/logger is preferred when the dataset needs trusted labels and safety alarms.
- Raw sensors still need calibration, drift management, and environmental compensation before their readings can be treated as reference labels.
- VOC sensors such as SGP40 should not be used as gas-type or ppm ground truth; they are useful for detecting interference conditions.

### Chamber State Machine

The chamber controller should log an explicit state for every sample. This is important because MQ sensors have slow response and recovery, so rising and falling periods should not be mixed with stable concentration samples without labels.

Recommended chamber states:

- `baseline_clean_air`
- `gas_injection`
- `mixing_rise`
- `stable_target_ppm`
- `recovery_venting`
- `post_recovery`

The solenoid valve, clean-air inlet, outlet, and fan should be controlled by a script or firmware state machine. Every state change must be timestamped in the same data file as the sensor readings.

### Better Setup

For more accurate concentration control, use:

- certified calibration gas cylinders with known ppm concentration
- pressure regulator
- mass flow controller or flow meter
- clean air source
- mixing chamber
- reference gas analyzer

This allows target ppm levels to be created repeatably instead of guessing concentration from injected gas volume.

### Target PPM Levels

Collect multiple concentration levels for each gas. Example starting plan:

- clean air baseline: `0 ppm`
- very low: `10 ppm`
- low: `25 ppm`
- medium-low: `50 ppm`
- medium: `100 ppm`
- medium-high: `250 ppm`
- high: `500 ppm`
- very high: `1000 ppm`

The exact levels should be adjusted for each gas based on sensor range, safety limits, and legal exposure limits. H2S and CO require much lower and stricter safety limits than LPG-family gases.

The example list must not be used directly for H2S or CO testing. Those gases need gas-specific safe concentration limits, certified reference instruments, and proper ventilation/safety controls before any chamber run.

### Per-Test Procedure

For each board, gas type, and target concentration:

1. Warm up the MQ sensors for a fixed duration.
2. Record clean-air baseline for several minutes.
3. Open the gas solenoid for a controlled pulse or until the reference channel reaches the target range.
4. Close the gas solenoid and wait for chamber mixing and sensor stabilization.
5. Record MQ voltages, reference ppm, BME280 temperature, humidity, and pressure continuously.
6. Stop gas input.
7. Vent the chamber until the reference meter returns to safe baseline.
8. Record recovery data.
9. Repeat the test on different days to capture drift.

### Required Dataset Columns

Future dataset files should include these columns:

- `timestamp`
- `board_id`
- `session_id`
- `gas_type`
- `param`
- `target_ppm`
- `reference_ppm`
- `severity_label`
- `temperature_c`
- `humidity_percent`
- `pressure_hpa`
- `chamber_state`
- `gas_valve_state`
- `clean_air_valve_state`
- `outlet_state`
- `fan_state`
- `injection_duration_ms`
- `time_since_injection_ms`
- `airflow_state`
- `warmup_minutes`
- `calibration_id`
- `MQ135V`
- `MQ2V`
- `MQ3V`
- `MQ4V`
- `MQ7V`
- `MQ5V`
- `MQ6V`
- `MQ8V`

Optional but useful:

- `chamber_volume_l`
- `gas_source`
- `reference_meter_model`
- `reference_meter_calibration_date`
- `enclosure_state`
- `notes`

### Labeling Rules

Use `reference_ppm` as the real regression target for concentration.

Use `gas_type` or `param` as the classification target:

- `0-0-0-0-0` = normal
- `1-0-0-0-0` = methane
- `0-1-0-0-0` = H2S
- `0-0-1-0-0` = butane
- `0-0-0-1-0` = propane
- `0-0-0-0-1` = CO

Severity should be derived from gas-specific ppm thresholds, not from generic response strength. Each gas needs its own threshold table because safety limits differ.

Example structure:

| Gas | Normal | Low | Medium | High |
| --- | --- | --- | --- | --- |
| methane | 0 ppm | gas-specific | gas-specific | gas-specific |
| H2S | 0 ppm | gas-specific | gas-specific | gas-specific |
| butane | 0 ppm | gas-specific | gas-specific | gas-specific |
| propane | 0 ppm | gas-specific | gas-specific | gas-specific |
| CO | 0 ppm | gas-specific | gas-specific | gas-specific |

The exact threshold values must be chosen from safety requirements, sensor range, and project requirements before production use.

### Minimum Dataset Size For PPM Model

For each board and each gas:

- at least 8 to 10 ppm levels
- at least 3 repeated sessions per ppm level
- at least 100 to 300 samples per ppm level after stabilization
- baseline and recovery samples for every session

This produces enough data for the model to learn concentration curves and sensor hysteresis instead of only class separation.

### Production Acceptance Criteria

Before ppm output is considered production-ready:

- ppm regression must be validated against a reference meter
- error must be reported per gas and per board
- validation must include unseen days/sessions
- validation must include environmental variation
- false-positive gas/vapor tests must be included
- the model must fail safely when confidence is low or readings are outside the training range

## Open Questions

1. What are the LED and buzzer GPIO pins for each sensor board?
2. Should the gateway decode the binary payload into named MQTT fields now, or is raw byte forwarding acceptable for the first rebuild?

## Implementation Order

1. Finalize this design.
2. Keep experimental code in `improvement_program/` until reviewed.
3. Build the chamber controller/logger for MQ sensors, TGS2610, BME280, valve states, fan state, and chamber state.
4. Collect improved chamber datasets with `reference_ppm` where available.
5. Add or update the multi-task training script for six gas classes.
6. Train all active board models.
7. Generate reports.
8. Update `NeuralNetwork` wrapper for four model outputs.
9. Update gas node firmware to send the versioned binary payload.
10. Add optional LED and buzzer pin handling.
11. Build active PlatformIO environments.
12. Fix build issues.
13. Document how to collect data, train, build, flash, and interpret payloads.
