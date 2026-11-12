#ifndef SPO2_CORE_H
#define SPO2_CORE_H

#include <Arduino.h>

void SpO2Core_reset();
void SpO2Core_update(long redValue, long irValue, unsigned long sampleTimeMs);

bool SpO2Core_hasNewValue();
bool SpO2Core_isValid();
bool SpO2Core_isWarmupFinished();

float SpO2Core_getValue();
float SpO2Core_getPlotValue();
float SpO2Core_getR();
float SpO2Core_getQuality();
unsigned long SpO2Core_getLastUpdateMs();

void SpO2Core_clearNewValueFlag();

#endif
