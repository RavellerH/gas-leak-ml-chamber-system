# Gas Leak ML Rebuild Todo

## 1. Data And Chamber

- [ ] Finalize production dataset schema.
- [ ] Build controlled-flow test chamber.
- [ ] Use `10 L` to `30 L` clear acrylic or polycarbonate chamber with gasketed lid.
- [ ] Add clean-air inlet and exhaust outlet.
- [ ] Add sealed cable glands for sensor and fan wiring.
- [ ] Add internal low-voltage brushless DC mixing fan.
- [ ] Keep relays, solenoids, switching contacts, and spark-producing electronics outside the gas space.
- [ ] Add BME280 or SHT31/SHT35 environmental sensor.
- [ ] Add Figaro TGS2610 reference channel for LPG-family tests.
- [ ] Add regulator plus needle valve, flow meter, or mass flow controller.
- [ ] Add independent external LPG/LEL safety alarm.
- [ ] Add manual gas shutoff.
- [ ] Add safe ventilation or exhaust path.
- [ ] Build chamber controller/logger.
- [ ] Log valve states, fan state, chamber state, and timestamps for every sample.

## 2. Safety And Reference Instruments

- [ ] Start with butane / propane / LPG-family tests only.
- [ ] Add calibrated LPG or LEL detector for LPG-family ppm/reference labels.
- [ ] Add methane only after LPG-family workflow is stable.
- [ ] Use calibrated methane or LEL detector for methane tests.
- [ ] Do not test CO in DIY chamber without certified instruments, ventilation, gas-rated safety equipment, and supervision.
- [ ] Do not test H2S in DIY chamber without certified instruments, ventilation, gas-rated safety equipment, and supervision.
- [ ] Add calibrated CO detector/logger before any CO work.
- [ ] Add calibrated H2S detector/logger before any H2S work.

## 3. Dataset Collection

- [ ] Collect clean-air baseline sessions.
- [ ] Collect gas injection sessions.
- [ ] Collect mixing/rise sessions.
- [ ] Collect stable target ppm sessions.
- [ ] Collect recovery/venting sessions.
- [ ] Collect post-recovery sessions.
- [ ] Collect repeated sessions across multiple days.
- [ ] Collect temperature and humidity variation.
- [ ] Collect long-running drift data.
- [ ] Collect false-positive/interference cases:
  - [ ] alcohol vapor
  - [ ] perfume
  - [ ] smoke
  - [ ] cleaning chemicals
  - [ ] humid air
  - [ ] hot air
  - [ ] dust
  - [ ] exhaust-like conditions
- [ ] Include `session_id` in every row.
- [ ] Include `board_id` in every row.
- [ ] Include `gas_type` and `param` in every row.
- [ ] Include `reference_ppm` where calibrated reference data exists.
- [ ] Include all 8 MQ voltages in the exact feature order:
  - [ ] `MQ135V`
  - [ ] `MQ2V`
  - [ ] `MQ3V`
  - [ ] `MQ4V`
  - [ ] `MQ7V`
  - [ ] `MQ5V`
  - [ ] `MQ6V`
  - [ ] `MQ8V`

## 4. ML Pipeline

- [ ] Keep one model per board.
- [ ] Keep six-class gas contract:
  - [ ] `0 = normal`
  - [ ] `1 = methane`
  - [ ] `2 = H2S`
  - [ ] `3 = butane`
  - [ ] `4 = propane`
  - [ ] `5 = CO`
- [ ] Add BME280 features to the training pipeline.
- [ ] Add clean-air baseline features.
- [ ] Add delta-from-baseline features.
- [ ] Add ratio-to-baseline features.
- [ ] Add time-window features for last `10` to `60` seconds.
- [ ] Add moving average features.
- [ ] Add slope/rate-of-change features.
- [ ] Add max-response features.
- [ ] Add recovery-slope features.
- [ ] Add time-since-chamber-state-transition feature.
- [ ] Handle class imbalance with class weights.
- [ ] Oversample minority classes only inside training split if needed.
- [ ] Do not oversample validation or test data.
- [ ] Add unknown/uncertain state for low-confidence predictions.
- [ ] Add out-of-distribution checks.
- [ ] Keep `ppm_estimate` labeled as proxy until calibrated ppm labels exist.
- [ ] Train real ppm regression only after `reference_ppm` exists.

## 5. Validation And Metrics

- [ ] Stop using random row split as the main production validation claim.
- [ ] Add held-out session validation.
- [ ] Add held-out day validation.
- [ ] Report gas type accuracy.
- [ ] Report precision per gas.
- [ ] Report recall per gas.
- [ ] Report F1 per gas.
- [ ] Report false alarm rate.
- [ ] Report missed leak rate.
- [ ] Report leak-present precision and recall.
- [ ] Report severity confusion matrix.
- [ ] Report gas type confusion matrix.
- [ ] Report ppm MAE per gas after calibrated ppm labels exist.
- [ ] Report metrics by board.
- [ ] Report metrics by session/day.
- [ ] Report metrics by humidity and temperature range.

## 6. Firmware Integration

- [ ] Verify ADC channel mapping against dataset feature order.
- [ ] Update firmware model wrapper for four outputs:
  - [ ] `gas_type`
  - [ ] `leak_present`
  - [ ] `severity`
  - [ ] `ppm_estimate`
- [ ] Validate input tensor type and shape.
- [ ] Validate output tensor count and dimensions.
- [ ] Add model version constant.
- [ ] Add scaler version constant.
- [ ] Add payload version constant.
- [ ] Add board ID to reports or payload context.
- [ ] Add invalid sensor reading checks.
- [ ] Add disconnected sensor checks.
- [ ] Add ADC saturation checks.
- [ ] Add stale reading checks.
- [ ] Add confidence threshold behavior.
- [ ] Add uncertain/fail-safe behavior.
- [ ] Keep float model export consistent with firmware input handling.
- [ ] If using int8 later, implement quantized input/output handling explicitly.

## 7. LoRa And Gateway

- [ ] Keep improved binary payload at `32 bytes`.
- [ ] Use payload version `1`.
- [ ] Send gas type, leak present, severity, confidence values, ppm estimate, inference time, and all 8 MQ millivolts.
- [ ] Build active gas board PlatformIO environments.
- [ ] Build cluster head PlatformIO environment.
- [ ] Build gateway PlatformIO environment.
- [ ] Decide whether first integration keeps raw byte forwarding.
- [ ] Add gateway decoder for payload version `1`.
- [ ] Publish named MQTT fields:
  - [ ] `gasType`
  - [ ] `leakPresent`
  - [ ] `severity`
  - [ ] `ppmEstimate`
  - [ ] `mqVoltages`
  - [ ] `inferenceTimeUs`
  - [ ] `confidence`

## 8. Documentation

- [ ] Keep `design.md` updated with hardware, model, firmware, and validation decisions.
- [ ] Keep `documentation.md` client-facing and consistent with `design.md`.
- [ ] Keep `session_notes.md` updated after each major session.
- [ ] Document chamber operating procedure.
- [ ] Document data collection procedure.
- [ ] Document model training command.
- [ ] Document firmware build and flash procedure.
- [ ] Document LoRa payload decoding.
- [ ] Document safety limitations and gas-specific reference instrument requirements.

## 9. Open Questions

- [ ] What are the LED GPIO pins for each gas sensor board?
- [ ] What are the buzzer GPIO pins for each gas sensor board?
- [ ] Should gateway decode binary payload now or keep raw forwarding for the first rebuild?
- [ ] What calibrated reference instrument will be used for methane?
- [ ] What calibrated reference instrument will be used for H2S?
- [ ] What calibrated reference instrument will be used for CO?
- [ ] What ppm thresholds define low, medium, and high severity for each gas?
