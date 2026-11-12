#include "SpO2Core.h"

#include <math.h>

namespace {
const unsigned long WARMUP_MS = 4000;

const float DC_FILTER_ALPHA = 0.93f;
const float SPO2_SMOOTH_ALPHA_FAST = 0.30f;
const float SPO2_SMOOTH_ALPHA_SLOW = 0.75f;
const byte SWITCH_TO_SLOW_AFTER = 5;

const float SPO2_CAL_A = 110.0f;
const float SPO2_CAL_B = 25.0f;
const float SPO2_CAL_OFFSET = 0.0f;
const bool SPO2_DEBUG = false;
const uint16_t SPO2_WINDOW_SIZE = 150;
const byte MIN_VALID_WINDOWS_TO_PRINT = 1;

const float MIN_AC_RMS = 20.0f;
const float MIN_DC_VALUE = 10000.0f;
const float MIN_RATIO = 0.30f;
const float MAX_RATIO = 2.00f;

const byte MEDIAN_FILTER_SIZE = 3;
const float MIN_AC_ALIGNMENT_ABS = 5.0f;
const uint16_t MIN_AC_ALIGNMENT_SAMPLES = SPO2_WINDOW_SIZE / 3;

const float MIN_PERFUSION_INDEX = 0.002f;
const float MAX_PERFUSION_INDEX = 0.20f;

const float MIN_SIGNAL_QUALITY = 60.0f;

const float MAX_DC_CHANGE_RATIO = 0.15f;
const float MAX_RATIO_CHANGE = 0.25f;

float redDC = 0;
float irDC = 0;

float redAC = 0;
float irAC = 0;

float redACSquareSum = 0;
float irACSquareSum = 0;
float redDCSum = 0;
float irDCSum = 0;

uint16_t spo2SampleCount = 0;
uint16_t acAlignedCount = 0;
uint16_t acMisalignedCount = 0;
byte validWindowCount = 0;

float spo2 = 0;
float rawSpO2 = 0;
float ratioOfRatios = 0;

float signalQuality = 0;
float redPerfusionIndex = 0;
float irPerfusionIndex = 0;

float lastValidRedDCAvg = 0;
float lastValidIrDCAvg = 0;
float lastValidRatio = 0;
bool hasLastValidWindow = false;

unsigned long startTime = 0;
unsigned long lastUpdateTime = 0;

bool filterReady = false;
bool started = false;
bool warmupFinished = false;
bool spo2Valid = false;
bool newValueAvailable = false;

long redMedianBuffer[MEDIAN_FILTER_SIZE] = {0};
long irMedianBuffer[MEDIAN_FILTER_SIZE] = {0};
byte medianBufferIndex = 0;
byte medianBufferCount = 0;

long getMedianValue(long *buffer, byte count)
{
  long temp[MEDIAN_FILTER_SIZE];

  for (byte i = 0; i < count; i++) {
    temp[i] = buffer[i];
  }

  for (byte i = 0; i < count; i++) {
    for (byte j = i + 1; j < count; j++) {
      if (temp[j] < temp[i]) {
        long t = temp[i];
        temp[i] = temp[j];
        temp[j] = t;
      }
    }
  }

  return temp[count / 2];
}

void updateMedianFilter(long redValue, long irValue, long &redFiltered, long &irFiltered)
{
  redMedianBuffer[medianBufferIndex] = redValue;
  irMedianBuffer[medianBufferIndex] = irValue;

  medianBufferIndex++;
  if (medianBufferIndex >= MEDIAN_FILTER_SIZE) {
    medianBufferIndex = 0;
  }

  if (medianBufferCount < MEDIAN_FILTER_SIZE) {
    medianBufferCount++;
  }

  redFiltered = getMedianValue(redMedianBuffer, medianBufferCount);
  irFiltered = getMedianValue(irMedianBuffer, medianBufferCount);
}

float calculateSignalQuality(float redACRMS, float irACRMS, float redDCAvg, float irDCAvg, float ratio)
{
  float quality = 100.0f;

  redPerfusionIndex = redACRMS / redDCAvg;
  irPerfusionIndex = irACRMS / irDCAvg;

  if (redDCAvg < MIN_DC_VALUE || irDCAvg < MIN_DC_VALUE) {
    quality -= 30.0f;
  }

  if (redACRMS < MIN_AC_RMS || irACRMS < MIN_AC_RMS) {
    quality -= 30.0f;
  }

  if (redPerfusionIndex < MIN_PERFUSION_INDEX || irPerfusionIndex < MIN_PERFUSION_INDEX) {
    quality -= 25.0f;
  }

  if (redPerfusionIndex > MAX_PERFUSION_INDEX || irPerfusionIndex > MAX_PERFUSION_INDEX) {
    quality -= 20.0f;
  }

  if (ratio < MIN_RATIO || ratio > MAX_RATIO) {
    quality -= 50.0f;
  }

  if (quality < 0.0f) {
    quality = 0.0f;
  }

  if (quality > 100.0f) {
    quality = 100.0f;
  }

  return quality;
}

void clearSpO2Window()
{
  redACSquareSum = 0;
  irACSquareSum = 0;
  redDCSum = 0;
  irDCSum = 0;
  spo2SampleCount = 0;
  acAlignedCount = 0;
  acMisalignedCount = 0;
}

void rejectSpO2Window(const char* reason)
{
  if (SPO2_DEBUG) {
    Serial.print("SpO2 reject: ");
    Serial.println(reason);
  }
  clearSpO2Window();
}

void updateACAlignment(float redACValue, float irACValue)
{
  if (fabs(redACValue) < MIN_AC_ALIGNMENT_ABS ||
      fabs(irACValue) < MIN_AC_ALIGNMENT_ABS) {
    return;
  }

  if ((redACValue > 0.0f && irACValue > 0.0f) ||
      (redACValue < 0.0f && irACValue < 0.0f)) {
    acAlignedCount++;
  }
  else {
    acMisalignedCount++;
  }
}

bool acAlignmentLooksMotionArtifact()
{
  uint16_t classifiedCount = acAlignedCount + acMisalignedCount;
  if (classifiedCount < MIN_AC_ALIGNMENT_SAMPLES) {
    return false;
  }

  return (uint32_t)acMisalignedCount * 10UL > (uint32_t)acAlignedCount * 3UL;
}

bool isSpO2WindowValid(float redACRMS, float irACRMS, float redDCAvg, float irDCAvg)
{
  if (redDCAvg < MIN_DC_VALUE || irDCAvg < MIN_DC_VALUE) {
    return false;
  }

  if (redACRMS < MIN_AC_RMS || irACRMS < MIN_AC_RMS) {
    return false;
  }

  if (redDCAvg <= 0 || irDCAvg <= 0 || redACRMS <= 0 || irACRMS <= 0) {
    return false;
  }

  return true;
}

bool isSpO2WindowMotionArtifact(float redDCAvg, float irDCAvg, float currentRatio)
{
  if (redPerfusionIndex < MIN_PERFUSION_INDEX || redPerfusionIndex > MAX_PERFUSION_INDEX) {
    return true;
  }

  if (irPerfusionIndex < MIN_PERFUSION_INDEX || irPerfusionIndex > MAX_PERFUSION_INDEX) {
    return true;
  }

  if (!hasLastValidWindow) {
    return false;
  }

  float redDCChangeRatio = fabs(redDCAvg - lastValidRedDCAvg) / lastValidRedDCAvg;
  float irDCChangeRatio = fabs(irDCAvg - lastValidIrDCAvg) / lastValidIrDCAvg;

  if (redDCChangeRatio > MAX_DC_CHANGE_RATIO || irDCChangeRatio > MAX_DC_CHANGE_RATIO) {
    return true;
  }

  if (fabs(currentRatio - lastValidRatio) > MAX_RATIO_CHANGE) {
    return true;
  }

  return false;
}

float calculateSpO2FromRatio(float ratio)
{
  return SPO2_CAL_A - SPO2_CAL_B * ratio + SPO2_CAL_OFFSET;
}

void printSpO2Calculation(float ratio, float value, float redACRMS, float irACRMS,
                          float redDCAvg, float irDCAvg)
{
  if (!SPO2_DEBUG) {
    return;
  }

  Serial.print("SpO2 calc R=");
  Serial.print(ratio, 3);
  Serial.print(" raw=");
  Serial.print(value, 1);
  Serial.print(" redPI=");
  Serial.print(redPerfusionIndex * 100.0f, 2);
  Serial.print("% irPI=");
  Serial.print(irPerfusionIndex * 100.0f, 2);
  Serial.print("% redDC=");
  Serial.print(redDCAvg, 0);
  Serial.print(" irDC=");
  Serial.print(irDCAvg, 0);
  Serial.print(" redAC=");
  Serial.print(redACRMS, 0);
  Serial.print(" irAC=");
  Serial.print(irACRMS, 0);
  Serial.print(" Q=");
  Serial.println(signalQuality, 0);
}

void calculateSpO2Window(unsigned long sampleTimeMs)
{
  if (spo2SampleCount < SPO2_WINDOW_SIZE) {
    return;
  }

  float redACRMS = sqrt(redACSquareSum / spo2SampleCount);
  float irACRMS = sqrt(irACSquareSum / spo2SampleCount);

  float redDCAvg = redDCSum / spo2SampleCount;
  float irDCAvg = irDCSum / spo2SampleCount;

  if (!isSpO2WindowValid(redACRMS, irACRMS, redDCAvg, irDCAvg)) {
    rejectSpO2Window("weak_signal");
    return;
  }

  float currentRatioOfRatios = (redACRMS / redDCAvg) / (irACRMS / irDCAvg);

  signalQuality = calculateSignalQuality(redACRMS, irACRMS, redDCAvg, irDCAvg, currentRatioOfRatios);

  if (acAlignmentLooksMotionArtifact()) {
    rejectSpO2Window("ac_motion");
    return;
  }

  if (signalQuality < MIN_SIGNAL_QUALITY) {
    rejectSpO2Window("low_quality");
    return;
  }

  if (currentRatioOfRatios < MIN_RATIO || currentRatioOfRatios > MAX_RATIO) {
    rejectSpO2Window("ratio_range");
    return;
  }

  if (isSpO2WindowMotionArtifact(redDCAvg, irDCAvg, currentRatioOfRatios)) {
    rejectSpO2Window("motion");
    return;
  }

  rawSpO2 = calculateSpO2FromRatio(currentRatioOfRatios);
  printSpO2Calculation(currentRatioOfRatios, rawSpO2, redACRMS, irACRMS, redDCAvg, irDCAvg);

  if (rawSpO2 > 100.0f) {
    rawSpO2 = 100.0f;
  }

  if (rawSpO2 < 70.0f) {
    rawSpO2 = 70.0f;
  }

  if (!warmupFinished) {
    rejectSpO2Window("warmup");
    return;
  }

  if (spo2 == 0) {
    spo2 = rawSpO2;
  }
  else {
    float alpha = (validWindowCount < SWITCH_TO_SLOW_AFTER)
                    ? SPO2_SMOOTH_ALPHA_FAST
                    : SPO2_SMOOTH_ALPHA_SLOW;
    spo2 = alpha * spo2 + (1.0f - alpha) * rawSpO2;
  }

  ratioOfRatios = currentRatioOfRatios;
  lastValidRedDCAvg = redDCAvg;
  lastValidIrDCAvg = irDCAvg;
  lastValidRatio = currentRatioOfRatios;
  hasLastValidWindow = true;
  lastUpdateTime = sampleTimeMs;

  if (validWindowCount < 255) {
    validWindowCount++;
  }

  if (validWindowCount >= MIN_VALID_WINDOWS_TO_PRINT) {
    newValueAvailable = true;
    spo2Valid = true;
  }

  clearSpO2Window();
}
}

void SpO2Core_reset()
{
  redDC = 0;
  irDC = 0;

  redAC = 0;
  irAC = 0;

  spo2 = 0;
  rawSpO2 = 0;
  ratioOfRatios = 0;

  signalQuality = 0;
  redPerfusionIndex = 0;
  irPerfusionIndex = 0;

  lastValidRedDCAvg = 0;
  lastValidIrDCAvg = 0;
  lastValidRatio = 0;
  hasLastValidWindow = false;

  validWindowCount = 0;

  startTime = 0;
  lastUpdateTime = 0;

  filterReady = false;
  started = false;
  warmupFinished = false;
  spo2Valid = false;
  newValueAvailable = false;

  medianBufferIndex = 0;
  medianBufferCount = 0;

  for (byte i = 0; i < MEDIAN_FILTER_SIZE; i++) {
    redMedianBuffer[i] = 0;
    irMedianBuffer[i] = 0;
  }

  clearSpO2Window();
}

void SpO2Core_update(long redValue, long irValue, unsigned long sampleTimeMs)
{
  if (!started) {
    started = true;
    startTime = sampleTimeMs;
  }

  long redFiltered = redValue;
  long irFiltered = irValue;

  updateMedianFilter(redValue, irValue, redFiltered, irFiltered);

  if (medianBufferCount < MEDIAN_FILTER_SIZE) {
    return;
  }

  if (!filterReady) {
    redDC = redFiltered;
    irDC = irFiltered;
    filterReady = true;
    return;
  }

  redDC = DC_FILTER_ALPHA * redDC + (1.0f - DC_FILTER_ALPHA) * redFiltered;
  irDC = DC_FILTER_ALPHA * irDC + (1.0f - DC_FILTER_ALPHA) * irFiltered;

  redAC = redFiltered - redDC;
  irAC = irFiltered - irDC;
  updateACAlignment(redAC, irAC);

  redACSquareSum += redAC * redAC;
  irACSquareSum += irAC * irAC;

  redDCSum += redDC;
  irDCSum += irDC;

  if (spo2SampleCount < SPO2_WINDOW_SIZE) {
    spo2SampleCount++;
  }

  if (sampleTimeMs - startTime >= WARMUP_MS) {
    warmupFinished = true;
  }

  calculateSpO2Window(sampleTimeMs);
}

bool SpO2Core_hasNewValue()
{
  return newValueAvailable;
}

bool SpO2Core_isValid()
{
  return spo2Valid;
}

bool SpO2Core_isWarmupFinished()
{
  return warmupFinished;
}

float SpO2Core_getValue()
{
  return spo2;
}

float SpO2Core_getPlotValue()
{
  return rawSpO2 > 0.0f ? rawSpO2 : spo2;
}

float SpO2Core_getR()
{
  return ratioOfRatios;
}

float SpO2Core_getQuality()
{
  return signalQuality;
}

unsigned long SpO2Core_getLastUpdateMs()
{
  return lastUpdateTime;
}

void SpO2Core_clearNewValueFlag()
{
  newValueAvailable = false;
}
