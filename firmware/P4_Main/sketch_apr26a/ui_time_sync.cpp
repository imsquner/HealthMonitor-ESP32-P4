#include "ui_time_sync.h"

#include <Arduino.h>
#include <sys/time.h>

#include "ui/ui.h"
#include "ui_status_bar.h"

static lv_timer_t *s_time_timer = nullptr;

static int month_from_name(const char *month)
{
    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    for (int i = 0; i < 12; i++) {
        if (strncmp(month, months[i], 3) == 0) {
            return i;
        }
    }
    return 0;
}

static time_t compile_time_unix(void)
{
    char month_text[4] = {0};
    int day = 1;
    int year = 2026;
    int hour = 0;
    int minute = 0;
    int second = 0;

    sscanf(__DATE__, "%3s %d %d", month_text, &day, &year);
    sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);

    struct tm tm_value = {};
    tm_value.tm_year = year - 1900;
    tm_value.tm_mon = month_from_name(month_text);
    tm_value.tm_mday = day;
    tm_value.tm_hour = hour;
    tm_value.tm_min = minute;
    tm_value.tm_sec = second;
    tm_value.tm_isdst = -1;

    return mktime(&tm_value);
}

static void set_system_time(time_t unix_time)
{
    struct timeval tv = {};
    tv.tv_sec = unix_time;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
}

static void set_label_text(lv_obj_t *label, const char *text)
{
    if (label != nullptr) {
        lv_label_set_text(label, text);
    }
}

static void format_time(time_t unix_time, char *date_colon, size_t date_colon_size,
                        char *date_slash, size_t date_slash_size,
                        char *time_hm, size_t time_hm_size,
                        char *history, size_t history_size)
{
    struct tm local_tm = {};
    localtime_r(&unix_time, &local_tm);

    const int year = local_tm.tm_year + 1900;
    const int month = local_tm.tm_mon + 1;
    const int day = local_tm.tm_mday;
    const int hour = local_tm.tm_hour;
    const int minute = local_tm.tm_min;

    snprintf(date_colon, date_colon_size, "%04d:%d:%d", year, month, day);
    snprintf(date_slash, date_slash_size, "%04d/%d/%d", year, month, day);
    snprintf(time_hm, time_hm_size, "%02d:%02d", hour, minute);
    snprintf(history, history_size, "%04d-%02d-%02d %02d:%02d", year, month, day, hour, minute);
}

static void update_current_time_labels(void)
{
    const time_t now = time(nullptr);

    char date_colon[16];
    char date_slash[16];
    char time_hm[8];
    char history[24];
    format_time(now, date_colon, sizeof(date_colon),
                date_slash, sizeof(date_slash),
                time_hm, sizeof(time_hm),
                history, sizeof(history));

    set_label_text(ui_Labeltimeb, time_hm);
    set_label_text(ui_Labeltime1, time_hm);
    ui_status_bar_update_time_labels(date_slash, time_hm);
}

static void time_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_current_time_labels();
}

void ui_time_sync_init(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();

    if (time(nullptr) < 1704067200) {
        set_system_time(compile_time_unix());
    }

    if (s_time_timer == nullptr) {
        s_time_timer = lv_timer_create(time_timer_cb, 1000, nullptr);
    }

    update_current_time_labels();
}

void ui_time_sync_set_unix(time_t unix_time)
{
    set_system_time(unix_time);
    update_current_time_labels();
}

void ui_time_sync_set_hour_minute(uint8_t hour, uint8_t minute)
{
    if ((hour > 23) || (minute > 59)) {
        return;
    }

    time_t now = time(nullptr);
    struct tm local_tm = {};
    localtime_r(&now, &local_tm);
    local_tm.tm_hour = hour;
    local_tm.tm_min = minute;
    local_tm.tm_sec = 0;
    local_tm.tm_isdst = -1;

    set_system_time(mktime(&local_tm));
    update_current_time_labels();
}

void ui_time_sync_set_history_time(uint8_t index, time_t unix_time)
{
    lv_obj_t *labels[] = {
        ui_Label24,
        ui_Label25,
        ui_Label26,
        ui_Label27,
        ui_Label28,
    };

    if (index >= (sizeof(labels) / sizeof(labels[0]))) {
        return;
    }

    char date_colon[16];
    char date_slash[16];
    char time_hm[8];
    char history[24];
    format_time(unix_time, date_colon, sizeof(date_colon),
                date_slash, sizeof(date_slash),
                time_hm, sizeof(time_hm),
                history, sizeof(history));
    set_label_text(labels[index], history);
}
