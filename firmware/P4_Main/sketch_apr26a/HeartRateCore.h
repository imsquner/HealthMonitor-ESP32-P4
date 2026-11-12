#ifndef HEART_RATE_CORE_H
#define HEART_RATE_CORE_H

#include <Arduino.h>

void HeartRateCore_reset();
void HeartRateCore_update(long irValue, unsigned long sampleTimeMs);

bool HeartRateCore_hasNewValue();
bool HeartRateCore_isValid();
bool HeartRateCore_isWarmingUp(unsigned long sampleTimeMs);

float HeartRateCore_getValue();
float HeartRateCore_getQuality();
unsigned long HeartRateCore_getLastUpdateMs();

void HeartRateCore_clearNewValueFlag();

#endif
