#include "HeartRateCore.h"

#include <string.h>

namespace {
const long HEART_RATE_FINGER_THRESHOLD = 15000L;
const byte HEART_RATE_INTERVAL_WINDOW_SIZE = 5;

const unsigned long WARMUP_MS = 6000;
const unsigned long HEART_RATE_PRINT_INTERVAL_MS = 500;

const unsigned long MIN_BEAT_INTERVAL_MS = 270;
const unsigned long MAX_BEAT_INTERVAL_MS = 1200;
const float MIN_OUTPUT_HEART_RATE = 40.0f;
const float MAX_OUTPUT_HEART_RATE = 200.0f;
const float MAX_HR_JUMP = 35.0f;
const float RESYNC_TOLERANCE = 18.0f;
const byte RESYNC_REQUIRED_COUNT = 2;
const byte MIN_VALID_BEATS_TO_PRINT = 4;

const float DC_FILTER_ALPHA = 0.95f;
const float SIGNAL_FILTER_ALPHA = 0.60f;
const float THRESHOLD_FILTER_ALPHA = 0.90f;
const float THRESHOLD_RATIO = 0.60f;
const float MIN_DYNAMIC_THRESHOLD = 35.0f;

const float MAX_INTERVAL_DEVIATION_RATIO = 0.25f;
const unsigned long MIN_INTERVAL_DEVIATION_TOLERANCE_MS = 120;

struct HeartRateState {
  float filteredIR;
  float prevFilteredIR;
  float dcFilter;
  float acValue;
  float dynamicThreshold;

  unsigned long lastBeatTime;
  unsigned long startTime;
  unsigned long lastPrintTime;
  unsigned long lastUpdateMs;
  unsigned long resyncCandidateInterval;

  float heartRate;
  float resyncCandidateBPM;
  float quality;
  byte resyncCount;
  byte validBeatCount;

  bool peakDetected;
  bool started;
  bool pendingHeartRatePrint;
  bool heartRateValid;
  bool newValueAvailable;

  unsigned long beatIntervals[HEART_RATE_INTERVAL_WINDOW_SIZE];
  byte beatIntervalCount;
  byte beatIntervalIndex;
};

HeartRateState state;

float floatAbs(float value)
{
  return value < 0.0f ? -value : value;
}

unsigned long ulongAbsDiff(unsigned long left, unsigned long right)
{
  return left > right ? left - right : right - left;
}

void clearBeatIntervals()
{
  state.beatIntervalCount = 0;
  state.beatIntervalIndex = 0;
}

void addBeatInterval(unsigned long interval)
{
  state.beatIntervals[state.beatIntervalIndex] = interval;
  state.beatIntervalIndex = (byte)((state.beatIntervalIndex + 1) % HEART_RATE_INTERVAL_WINDOW_SIZE);

  if (state.beatIntervalCount < HEART_RATE_INTERVAL_WINDOW_SIZE) {
    state.beatIntervalCount++;
  }
}

unsigned long medianBeatInterval()
{
  unsigned long sorted[HEART_RATE_INTERVAL_WINDOW_SIZE];

  for (byte i = 0; i < state.beatIntervalCount; i++) {
    sorted[i] = state.beatIntervals[i];
  }

  for (byte i = 1; i < state.beatIntervalCount; i++) {
    unsigned long key = sorted[i];
    int j = (int)i - 1;

    while (j >= 0 && sorted[j] > key) {
      sorted[j + 1] = sorted[j];
      j--;
    }

    sorted[j + 1] = key;
  }

  return sorted[state.beatIntervalCount / 2];
}

bool beatIntervalsAreStable()
{
  if (state.beatIntervalCount < MIN_VALID_BEATS_TO_PRINT) {
    state.quality = 0.0f;
    return false;
  }

  unsigned long medianInterval = medianBeatInterval();
  unsigned long tolerance = (unsigned long)((float)medianInterval * MAX_INTERVAL_DEVIATION_RATIO);

  if (tolerance < MIN_INTERVAL_DEVIATION_TOLERANCE_MS) {
    tolerance = MIN_INTERVAL_DEVIATION_TOLERANCE_MS;
  }

  unsigned long maxDeviation = 0;
  for (byte i = 0; i < state.beatIntervalCount; i++) {
    unsigned long deviation = ulongAbsDiff(state.beatIntervals[i], medianInterval);
    if (deviation > maxDeviation) {
      maxDeviation = deviation;
    }
  }

  if (maxDeviation > tolerance) {
    state.quality = 40.0f;
    return false;
  }

  state.quality = 100.0f - (60.0f * (float)maxDeviation / (float)tolerance);
  if (state.quality < 60.0f) {
    state.quality = 60.0f;
  }
  return true;
}

bool heartRateIsPlausible()
{
  return state.heartRate >= MIN_OUTPUT_HEART_RATE &&
         state.heartRate <= MAX_OUTPUT_HEART_RATE;
}

void acceptBeat(unsigned long interval, bool resetWindow, unsigned long sampleTimeMs)
{
  if (resetWindow) {
    clearBeatIntervals();
    state.validBeatCount = 0;

    if (state.resyncCandidateInterval > 0) {
      addBeatInterval(state.resyncCandidateInterval);
      if (state.validBeatCount < 255) {
        state.validBeatCount++;
      }
    }
  }

  addBeatInterval(interval);

  if (state.validBeatCount < 255) {
    state.validBeatCount++;
  }

  float medianBPM = 60000.0f / (float)medianBeatInterval();

  if (state.heartRate == 0.0f || resetWindow) {
    state.heartRate = medianBPM;
  }
  else {
    state.heartRate = 0.65f * state.heartRate + 0.35f * medianBPM;
  }

  state.resyncCandidateBPM = 0.0f;
  state.resyncCandidateInterval = 0;
  state.resyncCount = 0;

  if (!state.started) {
    state.started = true;
    state.startTime = sampleTimeMs;
  }

  if (!beatIntervalsAreStable() || !heartRateIsPlausible()) {
    state.heartRateValid = false;
    return;
  }

  if (sampleTimeMs - state.startTime >= WARMUP_MS &&
      state.validBeatCount >= MIN_VALID_BEATS_TO_PRINT) {
    state.pendingHeartRatePrint = true;
  }
}

void handleBeat(unsigned long sampleTimeMs)
{
  if (state.lastBeatTime == 0) {
    state.lastBeatTime = sampleTimeMs;
    return;
  }

  unsigned long interval = sampleTimeMs - state.lastBeatTime;

  if (interval < MIN_BEAT_INTERVAL_MS) {
    return;
  }

  if (interval > MAX_BEAT_INTERVAL_MS) {
    state.lastBeatTime = sampleTimeMs;
    clearBeatIntervals();
    state.validBeatCount = 0;
    state.resyncCandidateBPM = 0.0f;
    state.resyncCandidateInterval = 0;
    state.resyncCount = 0;
    state.heartRateValid = false;
    state.quality = 0.0f;
    return;
  }

  float newHR = 60000.0f / (float)interval;

  if (state.heartRate == 0.0f || floatAbs(newHR - state.heartRate) <= MAX_HR_JUMP) {
    acceptBeat(interval, false, sampleTimeMs);
  }
  else {
    if (state.resyncCandidateBPM > 0.0f &&
        floatAbs(newHR - state.resyncCandidateBPM) <= RESYNC_TOLERANCE) {
      state.resyncCount++;
    }
    else {
      state.resyncCandidateBPM = newHR;
      state.resyncCandidateInterval = interval;
      state.resyncCount = 1;
    }

    if (state.resyncCount >= RESYNC_REQUIRED_COUNT) {
      acceptBeat(interval, true, sampleTimeMs);
    }
  }

  state.lastBeatTime = sampleTimeMs;
}

void processIRSample(long irValue, unsigned long sampleTimeMs)
{
  state.dcFilter = DC_FILTER_ALPHA * state.dcFilter + (1.0f - DC_FILTER_ALPHA) * (float)irValue;
  state.acValue = (float)irValue - state.dcFilter;
  state.filteredIR = SIGNAL_FILTER_ALPHA * state.filteredIR + (1.0f - SIGNAL_FILTER_ALPHA) * state.acValue;
  state.dynamicThreshold = THRESHOLD_FILTER_ALPHA * state.dynamicThreshold +
                           (1.0f - THRESHOLD_FILTER_ALPHA) * floatAbs(state.filteredIR);

  float threshold = state.dynamicThreshold * THRESHOLD_RATIO;

  if (threshold < MIN_DYNAMIC_THRESHOLD) {
    threshold = MIN_DYNAMIC_THRESHOLD;
  }

  if (state.filteredIR > state.prevFilteredIR && state.filteredIR > threshold) {
    state.peakDetected = true;
  }

  if (state.peakDetected && state.filteredIR < state.prevFilteredIR) {
    handleBeat(sampleTimeMs);
    state.peakDetected = false;
  }

  state.prevFilteredIR = state.filteredIR;
}
}

void HeartRateCore_reset()
{
  memset(&state, 0, sizeof(state));
}

void HeartRateCore_update(long irValue, unsigned long sampleTimeMs)
{
  if (irValue < HEART_RATE_FINGER_THRESHOLD) {
    HeartRateCore_reset();
    return;
  }

  processIRSample(irValue, sampleTimeMs);

  if (state.started && sampleTimeMs - state.startTime < WARMUP_MS) {
    return;
  }

  if (state.started &&
      state.pendingHeartRatePrint &&
      sampleTimeMs - state.lastPrintTime > HEART_RATE_PRINT_INTERVAL_MS) {
    state.lastPrintTime = sampleTimeMs;
    state.pendingHeartRatePrint = false;
    state.heartRateValid = state.heartRate > 0.0f &&
                           state.validBeatCount >= MIN_VALID_BEATS_TO_PRINT &&
                           beatIntervalsAreStable() &&
                           heartRateIsPlausible();
    state.newValueAvailable = state.heartRateValid;

    if (state.heartRateValid) {
      state.lastUpdateMs = sampleTimeMs;
    }
  }
}

bool HeartRateCore_hasNewValue()
{
  return state.newValueAvailable;
}

bool HeartRateCore_isValid()
{
  return state.heartRateValid;
}

bool HeartRateCore_isWarmingUp(unsigned long sampleTimeMs)
{
  return state.started && sampleTimeMs - state.startTime < WARMUP_MS;
}

float HeartRateCore_getValue()
{
  return state.heartRate;
}

float HeartRateCore_getQuality()
{
  return state.quality;
}

unsigned long HeartRateCore_getLastUpdateMs()
{
  return state.lastUpdateMs;
}

void HeartRateCore_clearNewValueFlag()
{
  state.newValueAvailable = false;
}
