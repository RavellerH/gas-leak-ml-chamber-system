# Web Methodology References

Downloaded and linked references for production-grade gas sensor array / electronic-nose methodology.

## Downloaded Files

- `Figaro_TGS2610_product_information.pdf`
  - Source: https://www.figaro.co.jp/en/product/docs/tgs2610_product_information_rev03.pdf
  - Used for TGS2610 LP gas sensitivity and operating context.

- `Figaro_TGS2610_technical_information.pdf`
  - Source: https://www.figarosensor.com/product/docs/TGS2610_Technical%20Infomation_rev01.pdf
  - Used for TGS2610 technical behavior, temperature/humidity dependency, response, and long-term characteristics.

- `Figaro_TGS2610_C00_product_page.html`
  - Source: https://www.figarosensor.com/product/entry/tgs2610-c00.html
  - Used for target gas confirmation: butane and propane.

- `Figaro_TGS2610_D00_product_page.html`
  - Source: https://www.figarosensor.com/product/entry/tgs2610-d00.html
  - Used for target gas confirmation: butane and propane, LP gas selectivity.

- `UCI_gas_sensor_array_drift_different_concentrations.html`
  - Source: https://archive.ics.uci.edu/dataset/270
  - Used as methodology reference for multi-gas, multi-concentration sensor-array datasets.

- `UCI_gas_sensor_array_dynamic_gas_mixtures.html`
  - Source: https://archive.ics.uci.edu/ml/datasets/Gas%2Bsensor%2Barray%2Bunder%2Bdynamic%2Bgas%2Bmixtures
  - Used as methodology reference for time-series gas mixture acquisition.

- `UCI_gas_sensor_array_low_concentration.html`
  - Source: https://archive.ics.uci.edu/dataset/1081/gas%2Bsensor%2Barray%2Blow-concentration
  - Used as methodology reference for gas labels plus concentration labels.

- `UCI_gas_sensors_home_activity_monitoring.html`
  - Source: https://archive.ics.uci.edu/ml/datasets/Gas%2Bsensors%2Bfor%2Bhome%2Bactivity%2Bmonitoring
  - Used as methodology reference for 8-MOX arrays with temperature and humidity sensors.

## Linked References Not Fully Downloaded

These sources were used in the methodology review, but automated file download was blocked by the publisher or returned a script/cookie challenge page. Keep the URLs here for manual browser download if needed.

- Long-term drift behavior in metal oxide gas sensor arrays: a one-year dataset from an electronic nose
  - Source: https://www.nature.com/articles/s41597-025-05993-8
  - Download issue: PowerShell received a cookie-support/challenge HTML page instead of the full article/PDF.
  - Used for: long-term drift, time-series collection, baseline/sample/purge phases, temperature/humidity context, repeated sessions.

- Electronic Noses: From Gas-Sensitive Components and Practical Applications to Data Processing
  - Source: https://www.mdpi.com/1424-8220/24/15/4806
  - Download issue: MDPI PDF endpoint returned HTTP 403 to automated download.
  - Used for: e-nose methodology, sensor arrays, feature extraction, drift and environmental effects.

- Metal Oxide Gas Sensor Drift Compensation Using a Two-Dimensional Classifier Ensemble
  - Source: https://www.mdpi.com/1424-8220/15/5/10180
  - Download issue: MDPI PDF endpoint returned HTTP 403 to automated download.
  - Used for: metal-oxide gas sensor drift and compensation methodology.

- E-nose based on a high-integrated and low-power metal oxide gas sensor array
  - Source: https://www.sciencedirect.com/science/article/pii/S0925400523000047
  - Download issue: connection closed unexpectedly from ScienceDirect during automated download.
  - Used for: justification of multi-sensor array pattern recognition because a single MOS sensor has poor selectivity.

## Methodology Conclusions Captured In Design

The main conclusions from these references were added to `design.md`:

- Use all 8 MQ sensors as one electronic-nose sensor array.
- Use BME280 temperature, humidity, and pressure as context or compensation inputs.
- Collect time-series chamber data, not only isolated rows.
- Label chamber phase/state for every sample.
- Validate by held-out sessions/days, not random adjacent rows.
- Include drift, humidity, temperature, and false-positive tests.
- Treat Figaro TGS2610 as a butane/propane/LPG-family reference channel, not as a universal ppm reference for methane, H2S, or CO.
