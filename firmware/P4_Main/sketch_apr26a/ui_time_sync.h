#ifndef UI_TIME_SYNC_H
#define UI_TIME_SYNC_H

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui_time_sync_init(void);
void ui_time_sync_set_unix(time_t unix_time);
void ui_time_sync_set_hour_minute(uint8_t hour, uint8_t minute);
void ui_time_sync_set_history_time(uint8_t index, time_t unix_time);

#ifdef __cplusplus
}
#endif

#endif
