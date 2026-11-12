#ifndef HEARTRATE_H
#define HEARTRATE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  HEART_RATE_EVENT_NONE = 0,
  HEART_RATE_EVENT_NO_FINGER,
  HEART_RATE_EVENT_WARMUP,
  HEART_RATE_EVENT_READY
} HeartRateEvent;

typedef struct {
  float heartRate;
  int32_t irValue;
} HeartRateReading;

typedef struct {
  float filteredIR;
  float prevFilteredIR;
  float dcFilter;
  float acValue;
  float dynamicThreshold;

  uint32_t lastBeatTime;
  uint32_t startTime;
  uint32_t lastPrintTime;

  float heartRate;
  float resyncCandidateBPM;
  uint8_t resyncCount;

  bool peakDetected;
  bool started;
  bool pendingHeartRatePrint;
} HeartRateState;

void HeartRate_Init(HeartRateState *state);
HeartRateEvent HeartRate_Update(HeartRateState *state, int32_t irValue, uint32_t nowMs, HeartRateReading *reading);

#ifdef __cplusplus
}
#endif

#endif
