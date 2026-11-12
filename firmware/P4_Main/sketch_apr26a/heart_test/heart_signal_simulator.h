#ifndef HEART_SIGNAL_SIMULATOR_H
#define HEART_SIGNAL_SIMULATOR_H

#include <Arduino.h>

typedef struct {
  int32_t irValue;
  float targetBpm;
  bool fingerPresent;
} HeartSignalSample;

void HeartSignalSimulator_Reset(uint32_t nowMs);
HeartSignalSample HeartSignalSimulator_Next(uint32_t nowMs);

#endif
