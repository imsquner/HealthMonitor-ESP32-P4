#include "heart_signal_simulator.h"

#include <math.h>

static const float PI_F = 3.14159265358979323846f;
static const float TWO_PI_F = 6.28318530717958647692f;

static uint32_t s_startMs = 0;
static uint32_t s_lastMs = 0;
static float s_phase = 0.0f;
static uint32_t s_noiseState = 0x12345678;

static uint32_t elapsedSinceStart(uint32_t nowMs)
{
  return nowMs - s_startMs;
}

static int32_t deterministicNoise(int32_t amplitude)
{
  s_noiseState = 1664525UL * s_noiseState + 1013904223UL;
  int32_t centered = (int32_t)((s_noiseState >> 16) & 0x7ff) - 1024;
  return (centered * amplitude) / 1024;
}

static bool isFingerPresent(uint32_t elapsedMs)
{
  if (elapsedMs < 3000) {
    return false;
  }

  if (elapsedMs >= 42000 && elapsedMs < 47000) {
    return false;
  }

  return true;
}

static float targetBpmFor(uint32_t elapsedMs)
{
  if (elapsedMs < 18000) {
    return 72.0f;
  }

  if (elapsedMs < 30000) {
    return 96.0f;
  }

  if (elapsedMs < 42000) {
    return 58.0f;
  }

  return 84.0f;
}

static float pulseShape(float phase)
{
  float sine = sinf(phase);
  float second = 0.35f * sinf(phase * 2.0f - 0.9f);
  float third = 0.12f * sinf(phase * 3.0f + 0.7f);
  return sine + second + third;
}

void HeartSignalSimulator_Reset(uint32_t nowMs)
{
  s_startMs = nowMs;
  s_lastMs = nowMs;
  s_phase = 0.0f;
  s_noiseState = 0x12345678;
}

HeartSignalSample HeartSignalSimulator_Next(uint32_t nowMs)
{
  uint32_t elapsedMs = elapsedSinceStart(nowMs);
  bool fingerPresent = isFingerPresent(elapsedMs);
  float targetBpm = targetBpmFor(elapsedMs);
  uint32_t dtMs = nowMs - s_lastMs;
  s_lastMs = nowMs;

  HeartSignalSample sample;
  sample.targetBpm = targetBpm;
  sample.fingerPresent = fingerPresent;

  if (!fingerPresent) {
    s_phase = 0.0f;
    sample.irValue = 7000 + deterministicNoise(450);
    return sample;
  }

  s_phase += TWO_PI_F * targetBpm * ((float)dtMs / 60000.0f);
  while (s_phase >= TWO_PI_F) {
    s_phase -= TWO_PI_F;
  }

  float baseline = 52000.0f;
  float respiratoryDrift = 1500.0f * sinf(TWO_PI_F * ((float)elapsedMs / 7000.0f));
  float ppg = 6200.0f * pulseShape(s_phase);
  int32_t noise = deterministicNoise(280);

  sample.irValue = (int32_t)(baseline + respiratoryDrift + ppg) + noise;
  return sample;
}
