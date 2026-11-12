#ifndef UI_SCREEN_NAV_H
#define UI_SCREEN_NAV_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_screen_nav_handle_gesture(lv_event_t *e, int current_page);
void ui_screen_nav_next(void);
void ui_screen_nav_prev(void);
void ui_screen_nav_load(int page);
void ui_screen_nav_load_settings(void);

#ifdef __cplusplus
}
#endif

#endif
