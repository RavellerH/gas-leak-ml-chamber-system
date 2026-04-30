# Gas Leak Sensor ML Project - Client Overview

## One-Page Summary

This project builds a gas leak detection system using 8 MQ gas sensors, machine learning on ESP32-S3, LoRa communication, and a gateway for reporting.

The main idea is simple: one MQ sensor is not selective enough to reliably identify gas, but 8 MQ sensors together create a response pattern. The ML model reads that pattern like an electronic nose.

```text
8 MQ sensors
  -> ESP32-S3 machine learning
  -> local alarm decision
  -> LoRa message
  -> gateway / dashboard
```

## Target Outputs

- gas type: normal, methane, H2S, butane, propane, CO
- leak present: yes/no
- severity: normal, low, medium, high
- ppm estimate: proxy now, real ppm after calibrated chamber data

## Current Status

The project direction is technically correct, but the current dataset is not production-grade yet.

Current dataset limitations:

- no calibrated ppm labels
- not enough H2S, propane, and CO data
- limited temperature and humidity variation
- limited false-positive testing
- no long-term drift testing
- no session/day-based validation yet

So the current system should be treated as prototype/lab validation, not final safety certification.

## Why A Test Chamber Is Needed

The model needs better data from controlled gas exposure sessions.

The chamber should record:

- all 8 MQ sensor voltages
- reference ppm from calibrated instruments
- temperature, humidity, and pressure
- gas valve state
- clean-air valve state
- fan state
- chamber state
- session ID and board ID

Recommended chamber states:

```text
baseline_clean_air
gas_injection
mixing_rise
stable_target_ppm
recovery_venting
post_recovery
```

## Hardware Recommendation

Use a controlled-flow chamber, not a sealed box with manual gas injection.

Recommended early chamber:

- 10 L to 30 L acrylic or polycarbonate box
- gasketed lid
- gas inlet
- clean-air inlet
- exhaust outlet
- internal low-voltage mixing fan
- BME280 or SHT35 environmental sensor
- reference gas detector
- external solenoid valves
- safe ventilation

Start with LPG-family gases first. CO and H2S should only be tested with certified instruments, ventilation, safety equipment, and lab supervision.

## Reference Sensors

Recommended references:

| Gas | Reference Recommendation |
| --- | --- |
| butane / LPG | NevadaNano MPS or calibrated LPG/LEL detector |
| propane | NevadaNano MPS or calibrated LPG/LEL detector |
| methane | NevadaNano MPS or calibrated methane/LEL detector |
| CO | Alphasense CO or calibrated CO detector/logger |
| H2S | Alphasense H2S or calibrated H2S detector/logger |

Figaro TGS2610 is useful for LPG-family cross-checking, but it is not a universal ppm reference.

## ML Improvement Plan

Next model improvements:

- add temperature, humidity, and pressure
- add baseline-normalized MQ features
- add 10-60 second time-window features
- add slope and moving-average features
- handle class imbalance
- add uncertain/fail-safe behavior
- validate using held-out sessions and days

Important metrics:

- precision per gas
- recall per gas
- false alarm rate
- missed leak rate
- confusion matrix
- ppm MAE per gas after calibrated ppm exists

## Main Risk

Random row splitting can make model accuracy look too high because nearby samples from the same gas exposure are very similar.

Production validation must test on unseen sessions or days.

## Next Steps

1. Finalize chamber design.
2. Build chamber controller/logger.
3. Collect LPG-family time-series data first.
4. Add calibrated reference instruments.
5. Train per-board six-class multi-task models.
6. Validate on held-out sessions/days.
7. Integrate improved model wrapper into firmware.
8. Decode the 32-byte LoRa payload at the gateway.
