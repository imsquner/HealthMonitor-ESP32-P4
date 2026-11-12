#pragma once

#include <Arduino.h>

bool p4_vital_sensor_begin(void);
bool p4_vital_sensor_is_available(void);
bool p4_vital_sensor_read_next_sample(long &red_value, long &ir_value,
                                      uint32_t &sample_time_ms);
bool p4_vital_sensor_clear_fifo(void);
