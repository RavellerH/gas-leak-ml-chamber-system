#ifndef GASLEAK_IMPROVED_PAYLOAD_CONTRACT_H
#define GASLEAK_IMPROVED_PAYLOAD_CONTRACT_H

#include <stdint.h>

#define GASLEAK_PAYLOAD_VERSION 1
#define GASLEAK_PAYLOAD_SIZE 32
#define GASLEAK_CONFIDENCE_SCALE 1000

enum GasLeakGasType : uint8_t {
  GASLEAK_GAS_NORMAL = 0,
  GASLEAK_GAS_METHANE = 1,
  GASLEAK_GAS_H2S = 2,
  GASLEAK_GAS_BUTANE = 3,
  GASLEAK_GAS_PROPANE = 4,
  GASLEAK_GAS_CO = 5
};

enum GasLeakSeverity : uint8_t {
  GASLEAK_SEVERITY_NORMAL = 0,
  GASLEAK_SEVERITY_LOW = 1,
  GASLEAK_SEVERITY_MEDIUM = 2,
  GASLEAK_SEVERITY_HIGH = 3
};

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

static_assert(sizeof(GasLeakPayloadV1) == GASLEAK_PAYLOAD_SIZE, "Unexpected gas leak payload size");

#endif
