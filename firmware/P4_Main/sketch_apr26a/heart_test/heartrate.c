#include "heartrate.h"

#include <string.h>

static const int32_t FINGER_THRESHOLD = 15000;
static const uint32_t NO_FINGER_PRINT_INTERVAL_MS = 1000;
static const uint32_t WARMUP_MS = 6000;
static const uint32_t HEART_RATE_PRINT_INTERVAL_MS = 500;
static const uint32_t MIN_BEAT_INTERVAL_MS = 270;
static const uint32_t MAX_BEAT_INTERVAL_MS = 1200;
static const float MAX_HR_JUMP = 35.0f;
static const float RESYNC_TOLERANCE = 18.0f;
static const uint8_t RESYNC_REQUIRED_COUNT = 2;

static float floatAbs(float value)
{
  return value < 0 ? -value : value;
}

void HeartRate_Init(HeartRateState *state)
{
  memset(state, 0, sizeof(*state));
}

static void resetMeasurement(HeartRateState *state)
{
  uint32_t lastPrintTime = state->lastPrintTime;

  HeartRate_Init(state);
  state->lastPrintTime = lastPrintTime;
}

static void acceptHeartRate(HeartRateState *state, float newHR, uint32_t nowMs)
{
  if (state->heartRate == 0) {
    state->heartRate = newHR;
  }
  else {
    state->heartRate = 0.6f * state->heartRate + 0.4f * newHR;
  }

  state->resyncCandidateBPM = 0;
  state->resyncCount = 0;

  if (!state->started) {
    state->started = true;
    state->startTime = nowMs;
  }

  if (nowMs - state->startTime >= WARMUP_MS) {
    state->pendingHeartRatePrint = true;
  }
}

static void handleBeat(HeartRateState *state, uint32_t nowMs)
{
  if (state->lastBeatTime == 0) {
    state->lastBeatTime = nowMs;
    return;
  }

  uint32_t interval = nowMs - state->lastBeatTime;

  if (interval >= MIN_BEAT_INTERVAL_MS && interval <= MAX_BEAT_INTERVAL_MS) {
    float newHR = 60000.0f / interval;

    if (state->heartRate == 0 || floatAbs(newHR - state->heartRate) <= MAX_HR_JUMP) {
      acceptHeartRate(state, newHR, nowMs);
    }
    else {
      if (state->resyncCandidateBPM > 0 && floatAbs(newHR - state->resyncCandidateBPM) <= RESYNC_TOLERANCE) {
        state->resyncCount++;
      }
      else {
        state->resyncCandidateBPM = newHR;
        state->resyncCount = 1;
      }

      if (state->resyncCount >= RESYNC_REQUIRED_COUNT) {
        state->heartRate = newHR;
        state->resyncCandidateBPM = 0;
        state->resyncCount = 0;

        if (!state->started) {
          state->started = true;
          state->startTime = nowMs;
        }

        if (nowMs - state->startTime >= WARMUP_MS) {
          state->pendingHeartRatePrint = true;
        }
      }
    }

    state->lastBeatTime = nowMs;
  }
  else if (interval > MAX_BEAT_INTERVAL_MS) {
    state->lastBeatTime = nowMs;
  }
}

static void processIRSample(HeartRateState *state, int32_t irValue, uint32_t nowMs)
{
  state->dcFilter = 0.95f * state->dcFilter + 0.05f * irValue;
  state->acValue = irValue - state->dcFilter;
  state->filteredIR = 0.6f * state->filteredIR + 0.4f * state->acValue;
  state->dynamicThreshold = 0.9f * state->dynamicThreshold + 0.1f * floatAbs(state->filteredIR);

  if (state->filteredIR > state->prevFilteredIR && state->filteredIR > state->dynamicThreshold * 0.6f) {
    state->peakDetected = true;
  }

  if (state->peakDetected && state->filteredIR < state->prevFilteredIR) {
    handleBeat(state, nowMs);
    state->peakDetected = false;
  }

  state->prevFilteredIR = state->filteredIR;
}

HeartRateEvent HeartRate_Update(HeartRateState *state, int32_t irValue, uint32_t nowMs, HeartRateReading *reading)
{
  reading->heartRate = state->heartRate;
  reading->irValue = irValue;

  if (irValue < FINGER_THRESHOLD) {
    bool shouldPrint = nowMs - state->lastPrintTime > NO_FINGER_PRINT_INTERVAL_MS;

    if (shouldPrint) {
      state->lastPrintTime = nowMs;
    }

    resetMeasurement(state);
    reading->heartRate = 0;

    return shouldPrint ? HEART_RATE_EVENT_NO_FINGER : HEART_RATE_EVENT_NONE;
  }

  processIRSample(state, irValue, nowMs);
  reading->heartRate = state->heartRate;

  if (state->started && nowMs - state->startTime < WARMUP_MS) {
    if (nowMs - state->lastPrintTime > 1000) {
      state->lastPrintTime = nowMs;
      return HEART_RATE_EVENT_WARMUP;
    }

    return HEART_RATE_EVENT_NONE;
  }

  if (state->started && state->pendingHeartRatePrint && nowMs - state->lastPrintTime > HEART_RATE_PRINT_INTERVAL_MS) {
    state->lastPrintTime = nowMs;
    state->pendingHeartRatePrint = false;
    return HEART_RATE_EVENT_READY;
  }

  return HEART_RATE_EVENT_NONE;
}
