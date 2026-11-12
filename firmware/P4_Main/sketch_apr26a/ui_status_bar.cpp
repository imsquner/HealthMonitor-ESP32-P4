#include "ui_status_bar.h"

#include <string.h>

static const uint8_t MAX_STATUS_LABELS = 8;

static lv_obj_t *s_wifi_icons[MAX_STATUS_LABELS] = {};
static lv_obj_t *s_wifi_state_labels[MAX_STATUS_LABELS] = {};
static lv_obj_t *s_wifi_detail_labels[MAX_STATUS_LABELS] = {};
static lv_obj_t *s_c6_state_labels[MAX_STATUS_LABELS] = {};
static lv_obj_t *s_cloud_state_labels[MAX_STATUS_LABELS] = {};
static lv_obj_t *s_wechat_state_labels[MAX_STATUS_LABELS] = {};
static lv_obj_t *s_local_judgement_labels[MAX_STATUS_LABELS] = {};
static lv_obj_t *s_date_labels[MAX_STATUS_LABELS] = {};
static lv_obj_t *s_time_labels[MAX_STATUS_LABELS] = {};
static bool s_wifi_connected = false;
static char s_last_wifi_text[32] = "未连接";
static uint32_t s_last_wifi_color = 0xC00000;
static char s_last_wifi_detail[64] = "";
static char s_last_c6_text[32] = "待连接";
static uint32_t s_last_c6_color = 0xE87500;
static char s_last_cloud_text[32] = "待同步";
static uint32_t s_last_cloud_color = 0xC00000;
static char s_last_wechat_text[32] = "已开启";
static uint32_t s_last_wechat_color = 0x1F9D45;
static char s_last_local_judgement_text[32] = "待检测";
static uint32_t s_last_local_judgement_color = 0x1F9D45;
static char s_last_date[16] = "2026/5/17";
static char s_last_time[8] = "09:41";

static bool is_valid_label(lv_obj_t *label)
{
    return label != nullptr && lv_obj_is_valid(label);
}

static void set_label_text(lv_obj_t *label, const char *text)
{
    if (is_valid_label(label)) {
        lv_label_set_text(label, text);
    }
}

static void register_label(lv_obj_t **labels, lv_obj_t *label)
{
    if (!is_valid_label(label)) {
        return;
    }

    for (uint8_t i = 0; i < MAX_STATUS_LABELS; i++) {
        if (labels[i] != nullptr && !is_valid_label(labels[i])) {
            labels[i] = nullptr;
            continue;
        }
        if (labels[i] == label) {
            return;
        }
    }

    for (uint8_t i = 0; i < MAX_STATUS_LABELS; i++) {
        if (labels[i] == nullptr) {
            labels[i] = label;
            return;
        }
    }
}

static void apply_wifi_state_label(lv_obj_t *label)
{
    if (!is_valid_label(label) || s_last_wifi_text[0] == '\0') {
        return;
    }

    lv_label_set_text(label, s_last_wifi_text);
    lv_obj_set_style_text_color(label,
                                lv_color_hex(s_last_wifi_color),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void apply_state_label(lv_obj_t *label, const char *text, uint32_t color_hex)
{
    if (!is_valid_label(label) || text == nullptr) {
        return;
    }

    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label,
                                lv_color_hex(color_hex),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void set_registered_state(lv_obj_t **labels,
                                 char *last_text,
                                 size_t last_text_size,
                                 uint32_t *last_color,
                                 const char *text,
                                 uint32_t color_hex)
{
    if (text == nullptr) {
        return;
    }

    strncpy(last_text, text, last_text_size - 1);
    last_text[last_text_size - 1] = '\0';
    *last_color = color_hex;

    for (uint8_t i = 0; i < MAX_STATUS_LABELS; i++) {
        if (labels[i] == nullptr) {
            continue;
        }
        if (!is_valid_label(labels[i])) {
            labels[i] = nullptr;
            continue;
        }
        apply_state_label(labels[i], last_text, *last_color);
    }
}

static void register_state_label(lv_obj_t **labels, lv_obj_t *label, const char *text, uint32_t color_hex)
{
    register_label(labels, label);
    apply_state_label(label, text, color_hex);
}

void ui_status_bar_register_wifi_icon(lv_obj_t *icon)
{
    register_label(s_wifi_icons, icon);
    if (is_valid_label(icon)) {
        if (s_wifi_connected) {
            lv_obj_clear_flag(icon, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ui_status_bar_set_wifi_connected(bool connected)
{
    s_wifi_connected = connected;
    for (uint8_t i = 0; i < MAX_STATUS_LABELS; i++) {
        if (s_wifi_icons[i] == nullptr) {
            continue;
        }
        if (!is_valid_label(s_wifi_icons[i])) {
            s_wifi_icons[i] = nullptr;
            continue;
        }
        if (s_wifi_connected) {
            lv_obj_clear_flag(s_wifi_icons[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_wifi_icons[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    for (uint8_t i = 0; i < MAX_STATUS_LABELS; i++) {
        if (s_wifi_state_labels[i] == nullptr) {
            continue;
        }
        if (!is_valid_label(s_wifi_state_labels[i])) {
            s_wifi_state_labels[i] = nullptr;
            continue;
        }
        apply_wifi_state_label(s_wifi_state_labels[i]);
    }
}

void ui_status_bar_set_wifi_state_text(const char *text, uint32_t color_hex)
{
    if (text == nullptr) return;

    /* Skip update if nothing changed (UI throttle) */
    if (strncmp(s_last_wifi_text, text, sizeof(s_last_wifi_text) - 1) == 0
        && s_last_wifi_color == color_hex) {
        return;
    }

    strncpy(s_last_wifi_text, text, sizeof(s_last_wifi_text) - 1);
    s_last_wifi_text[sizeof(s_last_wifi_text) - 1] = '\0';
    s_last_wifi_color = color_hex;

    for (uint8_t i = 0; i < MAX_STATUS_LABELS; i++) {
        if (s_wifi_state_labels[i] == nullptr) continue;
        if (!is_valid_label(s_wifi_state_labels[i])) {
            s_wifi_state_labels[i] = nullptr;
            continue;
        }
        apply_wifi_state_label(s_wifi_state_labels[i]);
    }
}

void ui_status_bar_register_wifi_detail_label(lv_obj_t *label)
{
    register_label(s_wifi_detail_labels, label);
    if (s_last_wifi_detail[0] != '\0') {
        set_label_text(label, s_last_wifi_detail);
    }
}

void ui_status_bar_set_wifi_detail(const char *text)
{
    if (text == nullptr) {
        s_last_wifi_detail[0] = '\0';
    } else {
        strncpy(s_last_wifi_detail, text, sizeof(s_last_wifi_detail) - 1);
        s_last_wifi_detail[sizeof(s_last_wifi_detail) - 1] = '\0';
    }

    for (uint8_t i = 0; i < MAX_STATUS_LABELS; i++) {
        if (s_wifi_detail_labels[i] == nullptr) continue;
        if (!is_valid_label(s_wifi_detail_labels[i])) {
            s_wifi_detail_labels[i] = nullptr;
            continue;
        }
        set_label_text(s_wifi_detail_labels[i], s_last_wifi_detail);
    }
}

bool ui_status_bar_is_wifi_connected(void)
{
    return s_wifi_connected;
}

void ui_status_bar_register_wifi_state_label(lv_obj_t *label)
{
    register_label(s_wifi_state_labels, label);
    apply_wifi_state_label(label);
}

void ui_status_bar_register_c6_state_label(lv_obj_t *label)
{
    register_state_label(s_c6_state_labels, label, s_last_c6_text, s_last_c6_color);
}

void ui_status_bar_set_c6_state_text(const char *text, uint32_t color_hex)
{
    set_registered_state(s_c6_state_labels,
                         s_last_c6_text,
                         sizeof(s_last_c6_text),
                         &s_last_c6_color,
                         text,
                         color_hex);
}

void ui_status_bar_register_cloud_state_label(lv_obj_t *label)
{
    register_state_label(s_cloud_state_labels, label, s_last_cloud_text, s_last_cloud_color);
}

void ui_status_bar_set_cloud_state_text(const char *text, uint32_t color_hex)
{
    set_registered_state(s_cloud_state_labels,
                         s_last_cloud_text,
                         sizeof(s_last_cloud_text),
                         &s_last_cloud_color,
                         text,
                         color_hex);
}

void ui_status_bar_register_wechat_state_label(lv_obj_t *label)
{
    register_state_label(s_wechat_state_labels, label, s_last_wechat_text, s_last_wechat_color);
}

void ui_status_bar_set_wechat_state_text(const char *text, uint32_t color_hex)
{
    set_registered_state(s_wechat_state_labels,
                         s_last_wechat_text,
                         sizeof(s_last_wechat_text),
                         &s_last_wechat_color,
                         text,
                         color_hex);
}

void ui_status_bar_register_local_judgement_label(lv_obj_t *label)
{
    register_state_label(s_local_judgement_labels,
                         label,
                         s_last_local_judgement_text,
                         s_last_local_judgement_color);
}

void ui_status_bar_set_local_judgement_text(const char *text, uint32_t color_hex)
{
    set_registered_state(s_local_judgement_labels,
                         s_last_local_judgement_text,
                         sizeof(s_last_local_judgement_text),
                         &s_last_local_judgement_color,
                         text,
                         color_hex);
}

void ui_status_bar_update_time_labels(const char *date_slash, const char *time_hm)
{
    if (date_slash != nullptr) {
        strncpy(s_last_date, date_slash, sizeof(s_last_date) - 1);
        s_last_date[sizeof(s_last_date) - 1] = '\0';
    }
    if (time_hm != nullptr) {
        strncpy(s_last_time, time_hm, sizeof(s_last_time) - 1);
        s_last_time[sizeof(s_last_time) - 1] = '\0';
    }

    for (uint8_t i = 0; i < MAX_STATUS_LABELS; i++) {
        if (s_date_labels[i] != nullptr && !is_valid_label(s_date_labels[i])) {
            s_date_labels[i] = nullptr;
        }
        if (s_time_labels[i] != nullptr && !is_valid_label(s_time_labels[i])) {
            s_time_labels[i] = nullptr;
        }
        set_label_text(s_date_labels[i], s_last_date);
        set_label_text(s_time_labels[i], s_last_time);
    }
}

void ui_status_bar_register_date_label(lv_obj_t *label)
{
    register_label(s_date_labels, label);
    set_label_text(label, s_last_date);
}

void ui_status_bar_register_time_label(lv_obj_t *label)
{
    register_label(s_time_labels, label);
    set_label_text(label, s_last_time);
}
