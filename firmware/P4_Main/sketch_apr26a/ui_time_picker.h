#ifndef UI_TIME_PICKER_H
#define UI_TIME_PICKER_H

#ifdef __cplusplus
extern "C" {
#endif

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

void ui_time_picker_init(void);
void ui_time_picker_bind_label(lv_obj_t *label);

#ifdef __cplusplus
}
#endif

#endif
