#ifndef UI_STATUS_BAR_H
#define UI_STATUS_BAR_H

#include <stdbool.h>
#include <stdint.h>

#include "ui/ui.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_status_bar_register_wifi_icon(lv_obj_t *icon);
void ui_status_bar_set_wifi_connected(bool connected);
void ui_status_bar_set_wifi_state_text(const char *text, uint32_t color_hex);
bool ui_status_bar_is_wifi_connected(void);
void ui_status_bar_register_wifi_state_label(lv_obj_t *label);
void ui_status_bar_register_wifi_detail_label(lv_obj_t *label);
void ui_status_bar_set_wifi_detail(const char *text);
void ui_status_bar_register_c6_state_label(lv_obj_t *label);
void ui_status_bar_set_c6_state_text(const char *text, uint32_t color_hex);
void ui_status_bar_register_cloud_state_label(lv_obj_t *label);
void ui_status_bar_set_cloud_state_text(const char *text, uint32_t color_hex);
void ui_status_bar_register_wechat_state_label(lv_obj_t *label);
void ui_status_bar_set_wechat_state_text(const char *text, uint32_t color_hex);
void ui_status_bar_register_local_judgement_label(lv_obj_t *label);
void ui_status_bar_set_local_judgement_text(const char *text, uint32_t color_hex);
void ui_status_bar_update_time_labels(const char *date_slash, const char *time_hm);
void ui_status_bar_register_date_label(lv_obj_t *label);
void ui_status_bar_register_time_label(lv_obj_t *label);

#ifdef __cplusplus
}
#endif

#endif
