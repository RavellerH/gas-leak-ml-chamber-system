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

## 2026-04-30 Board 3 Dataset Logger Handoff

Board 3 gas leak sensor was detected earlier on `COM3` as `USB-SERIAL CH340`.
Serial boot output confirmed:

- `TCA9548 detected`
- `MCP4725` visible at `0x60`
- `ADS1256 ID: 0x36`
- `ADS1256 detected`
- `LoRa Receiver started`
- ML inference was running and reporting `Predicted class index : 0`

Changes made for Board 3:

- Added serial CSV dataset logging commands in `src/Gasleak/Board3/Gasleak.cpp`.
- Added commands:
  - `LABEL=<name>` sets the dataset label and replaces commas with underscores.
  - `CSV_ON` enables clean CSV output and disables debug output.
  - `CSV_OFF` stops CSV output.
- CSV header:

```csv
timestamp_ms,board_id,label,MQ135V,MQ2V,MQ3V,MQ4V,MQ7V,MQ5V,MQ6V,MQ8V,predicted_class,confidence_score,inference_time_us
```

- Rows are printed after a complete 8-channel read and at about `500 ms` interval.
- Fixed Board 3 compile error in `src/Gasleak/Board3/model_data.h` by changing model declarations to `const`, matching `model_data.cc`.

Build status:

- `loraMeshGasleak3` compiles successfully.
- Built firmware exists at `.pio/build/loraMeshGasleak3/firmware.bin`.

Upload status:

- Upload to `COM3` failed because the board/CH340 COM port disappeared from Windows.
- After unplug/replug, Windows still showed no COM ports.
- Recommended next step is restart PC with board unplugged, then plug board back in after Windows loads.
- Later continuation found `COM3` visible again as `USB-SERIAL CH340`.
- Upload was retried with PlatformIO, but Windows refused the port before flashing:
  - `Could not open COM3`
  - `PermissionError(13, 'A device attached to the system is not functioning.', None, 31)`
- Directly opening `COM3` from PowerShell failed with the same device error.
- No obvious `platformio`, `python`, `arduino`, PuTTY, Tera Term, or serial monitor process was holding the port.
- Current blocker is Windows/CH340/board USB state, not firmware compilation.
- User removed the second stacked sensor board and kept one board connected.
- Upload retry to `COM3` then succeeded.
- Uploaded firmware to ESP32-S3 MAC `d0:cf:13:25:88:94`.
- Short serial boot check after upload showed:
  - `ADS1256 ID: 0x0`
  - `ADS1256 not detected! Check connections.`
  - `ADS1256 detected.`
  - `ADS1256 SPI mode started.`
  - `Setup awal selesai.`
- The contradictory ADS1256 output likely needs follow-up: either ADC wiring/stacking state is still wrong, or the firmware detection path prints success after a failed ID check.
- User reinstalled the two-board sensor stack and asked to check detection.
- With the two-board stack installed, `COM3` still enumerated as `USB-SERIAL CH340`, but PowerShell could not open the serial port:
  - `A device attached to the system is not functioning.`
- Retried with reset/control lines disabled; opening `COM3` still failed.
- Sensor detection could not be verified in firmware logs because the USB serial link fails before any boot output can be read.
- This points to a hardware/power/stacking issue when two sensor boards are connected, because the same firmware and COM port worked with one board installed.
- After changing power/USB setup, the board re-enumerated as `COM5` / `USB-SERIAL CH340`.
- Opening `COM5` still failed with:
  - `A device attached to the system is not functioning.`
- No obvious local serial monitor process was holding the port.
- External power or changed USB path improved enumeration, but serial communication still is not healthy enough to verify sensor detection.
- Later retry showed the board re-enumerated again as `COM6` / `USB-SERIAL CH340`.
- First COM6 read attempt opened briefly, then failed because the port closed before `ReadExisting`.
- Retrying with DTR/RTS enabled also resulted in `PORT_CLOSED_BEFORE_READ`.
- The serial device is now intermittently enumerating but disconnecting/resetting during access, consistent with unstable USB/power/stack hardware state.
- Later retry after cable/power changes showed the board back on `COM3` and serial output became readable.
- Boot log with two-board stack showed:
  - `TCA9548 not detected! Check connections.`
  - EEPROM LoRa config still loads.
  - MCP4725 channel setting begins, e.g. `Channel 1 di-set ke wiper 1029.`
- Current confirmed hardware detection result: ESP32 boots, but the TCA9548 I2C mux on the sensor stack is not detected, so the stacked sensor chain is not visible at the I2C stage.
- User clarified that the upper stack contains the sensors and was not attached during a later check.
- With that sensor stack not attached, the board enumerated as `COM6` / `USB-SERIAL CH340`, but COM6 still closed before data could be read.
- Therefore the latest COM instability is not sufficient to prove sensor-stack detection; it also occurs when the sensor stack is absent.

## 2026-05-01 Board 3 Firmware Hardening

User asked whether firmware could be involved and requested fixing firmware first.

Changes made in `src/Gasleak/Board3/Gasleak.cpp`:

- Added hardware status flags:
  - `tcaDetected`
  - `adsDetected`
- Changed `checkADS1256()` from `void` to `bool`.
- Fixed ADS1256 false-positive behavior:
  - before: firmware printed `ADS1256 detected` even when ID was invalid, e.g. `0x0`
  - now: invalid ID returns `false` and prints not detected only
- `StartADS()` now skips ADS configuration if ADS1256 is not detected.
- Runtime MQ reads, gain calibration, preheat, ML inference, and calibration paths now skip sensor work when required hardware is missing.
- Wiper/mux paths now skip when TCA9548 is not detected.

Build status:

- `loraMeshGasleak3` builds successfully after the firmware hardening.
- Normal build command succeeded:

```powershell
$env:PLATFORMIO_CORE_DIR='.pio_home'; python -m platformio run -e loraMeshGasleak3
```

Upload status:

- First upload retry hit a stale SCons cache lock on `.pio_cache/config`.
- Retried with alternate cache:

```powershell
$env:PLATFORMIO_CORE_DIR='.pio_home'; $env:PLATFORMIO_BUILD_CACHE_DIR='.pio_cache_upload'; python -m platformio run -e loraMeshGasleak3 -t upload --upload-port COM5
```

- Firmware rebuilt successfully, but upload still failed before flashing because Windows could not open `COM5`:
  - `PermissionError(13, 'A device attached to the system is not functioning.', None, 31)`
- Current status: fixed firmware is built locally, but not uploaded yet due to the same USB/CH340/port instability.
- User removed the top sensor board while keeping 5V power on the bottom board.
- COM5 then opened and produced ESP32 boot ROM output.
- Hardened Board 3 firmware upload to `COM5` succeeded.
- Uploaded to ESP32-S3 MAC `d0:cf:13:25:88:94`.
- After successful upload, COM5 stayed enumerated as `USB-SERIAL CH340`, but serial read attempts closed before data could be read.
- Current status: fixed firmware is now flashed; boot-log verification after flashing is still blocked by serial link instability.
- Later COM5 read succeeded and verified hardened firmware behavior.
- Boot log showed:
  - `TCA9548 not detected! Check connections.`
  - wiper operations now skip with messages such as `Channel 1 wiper skipped: TCA9548 not detected.`
  - LoRa starts successfully.
  - `ADS1256 ID: 0x0`
  - `ADS1256 not detected! Check connections.`
  - `ADS1256 SPI mode not started.`
  - `Setup awal selesai.`
- This confirms the hardened firmware is running and no longer prints false `ADS1256 detected` after invalid ADS ID.

## 2026-05-01 Board 4 Firmware Test

User asked to test firmware from another board.

Checked other gas sensor board firmware:

- Board folders use the same sensor hardware pin mapping as Board 3:
  - TCA9548 address `0x71`
  - I2C SDA `GPIO 8`
  - I2C SCL `GPIO 9`
  - ADS1256 MOSI `GPIO 7`
  - ADS1256 MISO `GPIO 6`
  - ADS1256 SCK `GPIO 18`
  - ADS1256 CS `GPIO 16`
  - ADS1256 DRDY `GPIO 17`
- Other board firmware mostly differs in calibration/wiper values.
- Other board firmware still has the old ADS1256 false-positive behavior, unlike hardened Board 3.

Tested `loraMeshGasleak4`:

- Fixed Board 4 compile mismatch in `src/Gasleak/Board4/model_data.h` by changing declarations to `const`, matching `model_data.cc`.
- Uploaded Board 4 firmware successfully to the same ESP32-S3 on `COM6`.
- Upload target MAC remained `d0:cf:13:25:88:94`.
- After upload, COM6 stayed enumerated as `USB-SERIAL CH340`, but serial reads closed before data could be captured:
  - `PORT_CLOSED_BEFORE_READ`
- Current status: Board 4 firmware is flashed for comparison, but boot-log verification is blocked by the same serial instability.

## 2026-05-01 Serial Health Isolation

User clarified the top sensor board was not attached and asked to analyze/investigate.

Actions:

- Reflashed hardened Board 3 firmware to undo Board 4 test.
- Board 3 firmware upload to `COM6` succeeded.
- Serial read after Board 3 reflash still closed before output:
  - `PORT_CLOSED_BEFORE_READ`
- Added a temporary PlatformIO environment:
  - `serialHealthCheck`
- Added minimal serial-only firmware:
  - `src/serialHealthCheck/main.cpp`
  - prints `SERIAL_HEALTH_CHECK_OK <millis>` once per second
- Uploaded `serialHealthCheck` to the same ESP32-S3 on `COM6`.
- Minimal firmware upload succeeded.
- Serial read was stable and repeatedly printed health messages, e.g.:
  - `SERIAL_HEALTH_CHECK_OK 8116`
  - `SERIAL_HEALTH_CHECK_OK 15116`
  - `SERIAL_HEALTH_CHECK_OK 43116`

Conclusion:

- With the top sensor board absent, the USB cable/CH340/ESP32 serial path is currently stable under minimal firmware.
- The `PORT_CLOSED_BEFORE_READ` behavior is triggered by the gas firmware startup path, not by basic USB serial alone.
- Current firmware on the ESP32 is the temporary `serialHealthCheck`, not gas firmware.

Firmware risk found in Board 3:

- `PIN_RESET` for ADS1256 is configured as `GPIO0`.
- Board 3 startup drives this pin low/high in `StartADS()`.
- `GPIO0` is an ESP32-S3 boot/strapping pin on many boards, so using it as ADS reset is risky and may interact with auto-reset/boot circuitry.
- Other startup areas to isolate next:
  - LoRa init with `LORA_DIO1 = GPIO1`
  - ADS reset/init with `PIN_RESET = GPIO0`
  - missing sensor-board behavior after LoRa starts

## 2026-05-01 Board 3 GPIO0 Reset Fix

User asked to fix Board 3.

Root cause isolated:

- Minimal `serialHealthCheck` firmware was stable.
- Gas firmware closed serial before read when the top sensor board was absent.
- Board 3 manually drove `PIN_RESET = GPIO0` low/high for ADS1256 reset.
- ADS1256 library already treats reset pin `0` as "no reset pin", but Board 3 firmware was bypassing that by manually driving GPIO0.
- GPIO0 is an ESP32-S3 boot/strapping pin, so manually driving it is unsafe.

Changes made:

- Board 3 no longer calls `pinMode(PIN_RESET, OUTPUT)`.
- Board 3 no longer manually drives `digitalWrite(PIN_RESET, LOW/HIGH)`.
- `PIN_RESET = 0` remains only as the ADS1256 library's "no reset pin" marker.
- `PIN_SYNC` is explicitly set high before ADS init.
- Added startup switches:
  - `ENABLE_LORA_STARTUP`
  - `ENABLE_ADS_STARTUP`
- Throttled `ML skipped: ADS1256 not detected.` to once every 5 seconds instead of spamming every inference interval.

Verification:

- `loraMeshGasleak3` builds successfully.
- Uploaded fixed Board 3 firmware to the same ESP32-S3 on `COM5`.
- Upload succeeded.
- Serial now stays open with the gas firmware.
- With the top sensor board absent, serial output shows throttled:
  - `ML skipped: ADS1256 not detected.`

Current status:

- ESP32 is running fixed Board 3 gas firmware again.
- Serial stability is fixed for the no-top-sensor-board case.
- Sensor detection is still expected to fail while the top sensor board is absent.

## 2026-05-01 Sensor Board Reattached After GPIO0 Fix

User attached the top sensor board after Board 3 GPIO0 reset fix.

Check result:

- COM5 remained enumerated as `USB-SERIAL CH340`.
- Opening COM5 failed again:
  - `A device attached to the system is not functioning.`
- No other obvious serial monitor process was holding the port.
- The same fixed Board 3 firmware had stable serial before the top sensor board was attached.

Current conclusion:

- The GPIO0 firmware issue is fixed for the no-sensor-board case.
- Reattaching the sensor board still breaks USB serial access.
- This points to a hardware/electrical issue introduced by the top sensor board:
  - power draw or voltage drop
  - pin misalignment
  - short between stack pins
  - sensor board tying an ESP32 boot/reset/USB-related line
  - incorrect 5V/3.3V rail connection
  - bus line conflict
- When the board appears again as `COMx`, upload with:

```powershell
$env:PLATFORMIO_CORE_DIR='.pio_home'; python -m platformio run -e loraMeshGasleak3 -t upload --upload-port COM3
```

Replace `COM3` if Windows assigns a different COM number.

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
