#ifndef UI_HEART_TEST_H
#define UI_HEART_TEST_H

void ui_heart_test_init(void);
void ui_heart_test_poll(void);
void ui_heart_test_debug_start(void);
void ui_heart_test_debug_stop(void);
bool ui_heart_test_is_collecting(void);
void ui_heart_test_debug_toggle(void);
bool ui_heart_test_has_danger_alert(void);
void ui_heart_test_clear_danger_alert(void);

#endif
