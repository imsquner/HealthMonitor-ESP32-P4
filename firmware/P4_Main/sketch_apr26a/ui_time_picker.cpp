#include "ui_time_picker.h"

#include <Arduino.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

#include "ui/ui.h"
#include "ui_time_sync.h"

static lv_obj_t *s_overlay = nullptr;
static lv_obj_t *s_textarea = nullptr;
static lv_obj_t *s_keyboard = nullptr;

static void close_picker(void)
{
    if (s_overlay != nullptr) {
        lv_obj_del(s_overlay);
    }
    s_overlay = nullptr;
    s_textarea = nullptr;
    s_keyboard = nullptr;
}

static void current_time_to_digits(char *buf, size_t size)
{
    time_t now = time(nullptr);
    struct tm local_tm = {};
    localtime_r(&now, &local_tm);
    snprintf(buf, size, "%02d%02d", local_tm.tm_hour, local_tm.tm_min);
}

static bool parse_time_text(const char *text, int *hour, int *minute)
{
    char digits[5] = {0};
    uint8_t digit_count = 0;

    for (const char *p = text; *p != '\0'; p++) {
        if (isdigit((unsigned char)*p)) {
            if (digit_count >= 4) {
                return false;
            }
            digits[digit_count++] = *p;
        }
    }

    if (digit_count == 3) {
        *hour = digits[0] - '0';
        *minute = (digits[1] - '0') * 10 + (digits[2] - '0');
    } else if (digit_count == 4) {
        *hour = (digits[0] - '0') * 10 + (digits[1] - '0');
        *minute = (digits[2] - '0') * 10 + (digits[3] - '0');
    } else {
        return false;
    }

    return ((*hour >= 0) && (*hour <= 23) && (*minute >= 0) && (*minute <= 59));
}

static void keyboard_event_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CANCEL) {
        close_picker();
        return;
    }

    if (code != LV_EVENT_READY) {
        return;
    }

    int hour = 0;
    int minute = 0;
    const char *text = lv_textarea_get_text(s_textarea);
    if (!parse_time_text(text, &hour, &minute)) {
        lv_textarea_set_text(s_textarea, "");
        Serial.println("Time input error, use HHMM, for example 1005");
        return;
    }

    ui_time_sync_set_hour_minute((uint8_t)hour, (uint8_t)minute);
    Serial.printf("Time updated from touch keyboard to %02d:%02d\r\n", hour, minute);
    close_picker();
}

static void overlay_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED && lv_event_get_target(e) == s_overlay) {
        close_picker();
    }
}

static void open_picker(void)
{
    if (s_overlay != nullptr) {
        return;
    }

    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_overlay, 120, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_overlay, overlay_event_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *panel = lv_obj_create(s_overlay);
    lv_obj_set_size(panel, 520, 460);
    lv_obj_center(panel);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(panel, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_textarea = lv_textarea_create(panel);
    lv_obj_set_size(s_textarea, 320, 86);
    lv_obj_align(s_textarea, LV_ALIGN_TOP_MID, 0, 24);
    lv_textarea_set_one_line(s_textarea, true);
    lv_textarea_set_max_length(s_textarea, 4);
    lv_textarea_set_accepted_chars(s_textarea, "0123456789");
    lv_obj_set_style_text_align(s_textarea, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_textarea, &ui_font_H1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(s_textarea, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(s_textarea, 12, LV_PART_MAIN | LV_STATE_DEFAULT);

    char initial_text[5];
    current_time_to_digits(initial_text, sizeof(initial_text));
    lv_textarea_set_text(s_textarea, initial_text);

    s_keyboard = lv_keyboard_create(panel);
    lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(s_keyboard, s_textarea);
    lv_obj_set_size(s_keyboard, 480, 300);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_add_event_cb(s_keyboard, keyboard_event_cb, LV_EVENT_ALL, nullptr);
}

static void time_label_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        open_picker();
    }
}

void ui_time_picker_init(void)
{
}

void ui_time_picker_bind_label(lv_obj_t *label)
{
    (void)label;
}
