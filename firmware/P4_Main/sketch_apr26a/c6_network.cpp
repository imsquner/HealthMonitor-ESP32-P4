#include "c6_network.h"

#include <Arduino.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl_mipilcd_port.h"
#include "ui_login.h"
#include "ui_status_bar.h"
#include "ui_time_sync.h"
#include "ui_screen_nav.h"

static HardwareSerial C6Serial(1);

static const int C6_UART_RX_PIN = 27; // P4 U1RXD / IO27, connect to C6 TX
static const int C6_UART_TX_PIN = 26; // P4 U1TXD / IO26, connect to C6 RX
static const uint32_t C6_UART_BAUD = 115200;
static const uint32_t C6_STATUS_REQUEST_INTERVAL_MS = 3000;
static const uint32_t C6_TIME_SYNC_INTERVAL_MS = 60000;
static const uint32_t C6_RESPONSE_TIMEOUT_MS = 35000;

static uint32_t s_last_c6_ping_ms = 0;
static uint32_t s_last_c6_time_request_ms = 0;
static uint32_t s_last_c6_status_request_ms = 0;
static uint32_t s_last_c6_rx_ms = 0;
static uint32_t s_last_successful_time_sync_ms = 0;
static bool s_c6_handshake_done = false;
static bool s_c6_network_enabled = false;
static bool s_c6_wifi_connected = false;
static bool s_c6_time_request_pending = false;
static bool s_c6_marked_unresponsive = false;

static const size_t C6_AI_BUFFER_SIZE = 768;
static const size_t C6_AI_SCREEN_TEXT_SIZE = 256;
static const uint32_t C6_AI_FAKE_PROGRESS_MS = 10000;
static const uint32_t NETWORK_NOTICE_SHOW_MS = 3000;
static char s_ai_buffer[C6_AI_BUFFER_SIZE] = "";
static size_t s_ai_buffer_len = 0;
static bool s_ai_receiving = false;
static bool s_ai_waiting_to_show = false;
static bool s_ai_progress_complete = false;
static bool s_ai_result_ready = false;
static uint32_t s_ai_progress_start_ms = 0;
static char s_ai_pending_title[24] = "AI诊断";
static char s_ai_pending_text[C6_AI_SCREEN_TEXT_SIZE] = "";
static uint32_t s_ai_pending_cloud_color = 0xD6A600;
static char s_ai_pending_cloud_state[24] = "AI分析中";
static bool s_ai_waiting_for_network = false;
static bool s_ai_panel_dismissed = false;
static lv_obj_t *s_network_notice_panel = nullptr;
static bool s_network_notice_visible = false;
static uint32_t s_network_notice_hide_at_ms = 0;

enum AiScreenLevel {
    AI_SCREEN_LEVEL_UNKNOWN,
    AI_SCREEN_LEVEL_NORMAL,
    AI_SCREEN_LEVEL_WARN,
    AI_SCREEN_LEVEL_DANGER,
};

struct AiScreenSummary {
    bool valid;
    int max_heart;
    int min_heart;
    int avg_heart;
    int max_spo2;
    int min_spo2;
    int avg_spo2;
    int detect_hour;
    bool after_exercise;
    char local_judgement[24];
};

static AiScreenSummary s_ai_summary = {};

/* ---- C6 WiFi rich state ---- */
static c6_wifi_state_t s_c6_wifi_state = C6_WIFI_DISCONNECTED;
static char  s_c6_wifi_ip[24] = "";
static int   s_c6_wifi_rssi = 0;
static uint32_t s_c6_last_wifi_msg_ms = 0;
static const uint32_t C6_WIFI_TIMEOUT_MS = 35000;

/* Prevent spamming the UI with identical updates */
static c6_wifi_state_t s_ui_wifi_state = C6_WIFI_DISCONNECTED;
static char  s_ui_wifi_ip[24] = "";
static int   s_ui_wifi_rssi_bucket = 0;  // RSSI bucketed: strong/medium/weak

static void send_c6_line(const char *line);
static void send_c6_line(const String &line);

static void request_c6_time_sync(bool force);

static void debug_line(const char *text)
{
    Serial.println(text);
    Serial0.println(text);
}

static void debug_line(const String &text)
{
    Serial.println(text);
    Serial0.println(text);
}

static void debug_printf(const char *format, ...)
{
    char buffer[180];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Serial.print(buffer);
    Serial0.print(buffer);
}

static bool is_valid_ui_obj(lv_obj_t *obj)
{
    return obj != nullptr && lv_obj_is_valid(obj);
}

static size_t utf8_prefix_len(const char *text, size_t byte_limit)
{
    size_t len = strlen(text);
    if (len <= byte_limit) {
        return len;
    }

    size_t end = byte_limit;
    while (end > 0 && (((uint8_t)text[end]) & 0xC0) == 0x80) {
        end--;
    }
    return (end > 0) ? end : byte_limit;
}

static size_t utf8_char_len(const char *text)
{
    const uint8_t first = (uint8_t)text[0];
    if (first < 0x80) return 1;
    if ((first & 0xE0) == 0xC0 && text[1] != '\0') return 2;
    if ((first & 0xF0) == 0xE0 && text[1] != '\0' && text[2] != '\0') return 3;
    if ((first & 0xF8) == 0xF0 && text[1] != '\0' && text[2] != '\0' && text[3] != '\0') return 4;
    return 1;
}

static void copy_utf8_chars(const char *src, char *out, size_t out_size, size_t max_chars)
{
    if (out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (src == nullptr || src[0] == '\0' || max_chars == 0) {
        return;
    }

    size_t out_len = 0;
    size_t chars = 0;
    while (*src != '\0' && chars < max_chars && out_len + 1 < out_size) {
        const size_t len = utf8_char_len(src);
        if (out_len + len >= out_size) {
            break;
        }
        memcpy(out + out_len, src, len);
        out_len += len;
        src += len;
        chars++;
    }
    out[out_len] = '\0';
}

static const char *skip_ascii_spaces(const char *text)
{
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
        text++;
    }
    return text;
}

static void copy_trimmed_field(const char *start, const char *end, char *out, size_t out_size)
{
    if (out_size == 0) {
        return;
    }

    while (start < end && (*start == ' ' || *start == '\t')) {
        start++;
    }
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }

    size_t len = (size_t)(end - start);
    if (len >= out_size) {
        len = utf8_prefix_len(start, out_size - 1);
    }
    memcpy(out, start, len);
    out[len] = '\0';
}

static bool extract_elder_text(const char *text, char *out, size_t out_size)
{
    const char *start = skip_ascii_spaces(text);
    if (strncmp(start, "@@|AI|", 6) != 0) {
        return false;
    }

    const char *elder = strstr(start, "ELDER:");
    if (elder == nullptr) {
        return false;
    }
    elder += strlen("ELDER:");

    const char *end = strstr(elder, "|CHILD:");
    if (end == nullptr) end = strstr(elder, "|LEVEL:");
    if (end == nullptr) end = strstr(elder, "|##");
    if (end == nullptr) end = start + strlen(start);

    copy_trimmed_field(elder, end, out, out_size);
    return out[0] != '\0';
}

static bool ascii_token_equals(const char *token, const char *expected)
{
    if (token == nullptr || expected == nullptr) {
        return false;
    }
    while (*token != '\0' && *expected != '\0') {
        char a = *token++;
        char b = *expected++;
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return *token == '\0' && *expected == '\0';
}

static void reset_ai_buffer()
{
    s_ai_buffer_len = 0;
    s_ai_buffer[0] = '\0';
    s_ai_receiving = true;
}

static void append_ai_text(const char *text)
{
    if (text == nullptr || text[0] == '\0') {
        return;
    }

    if (!s_ai_receiving) {
        reset_ai_buffer();
    }

    const size_t available = (C6_AI_BUFFER_SIZE - 1) - s_ai_buffer_len;
    if (available == 0) {
        return;
    }

    const size_t copy_len = utf8_prefix_len(text, available);
    memcpy(s_ai_buffer + s_ai_buffer_len, text, copy_len);
    s_ai_buffer_len += copy_len;
    s_ai_buffer[s_ai_buffer_len] = '\0';
}

static AiScreenLevel ai_level_from_local_judgement(const char *text)
{
    if (text == nullptr) {
        return AI_SCREEN_LEVEL_UNKNOWN;
    }
    if (strstr(text, "异常") != nullptr || strstr(text, "危险") != nullptr) {
        return AI_SCREEN_LEVEL_DANGER;
    }
    if (strstr(text, "关注") != nullptr || strstr(text, "波动") != nullptr) {
        return AI_SCREEN_LEVEL_WARN;
    }
    if (strstr(text, "正常") != nullptr) {
        return AI_SCREEN_LEVEL_NORMAL;
    }
    return AI_SCREEN_LEVEL_UNKNOWN;
}

static AiScreenLevel parse_ai_level(const char *text)
{
    const char *level = strstr(text, "LEVEL:");
    if (level != nullptr) {
        level += strlen("LEVEL:");
        level = skip_ascii_spaces(level);
        char token[16];
        size_t len = 0;
        while (*level != '\0' && *level != '|' && *level != '#' && len + 1 < sizeof(token)) {
            char c = *level++;
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                break;
            }
            token[len++] = c;
        }
        token[len] = '\0';
        if (ascii_token_equals(token, "normal")) {
            return AI_SCREEN_LEVEL_NORMAL;
        }
        if (ascii_token_equals(token, "warn") || ascii_token_equals(token, "warning")) {
            return AI_SCREEN_LEVEL_WARN;
        }
        if (ascii_token_equals(token, "danger")) {
            return AI_SCREEN_LEVEL_DANGER;
        }
    }

    if (strstr(text, "danger") != nullptr || strstr(text, "DANGER") != nullptr
        || strstr(text, "异常") != nullptr || strstr(text, "危险") != nullptr) {
        return AI_SCREEN_LEVEL_DANGER;
    }
    if (strstr(text, "warn") != nullptr || strstr(text, "WARN") != nullptr
        || strstr(text, "关注") != nullptr || strstr(text, "波动") != nullptr) {
        return AI_SCREEN_LEVEL_WARN;
    }
    if (strstr(text, "normal") != nullptr || strstr(text, "NORMAL") != nullptr
        || strstr(text, "正常") != nullptr) {
        return AI_SCREEN_LEVEL_NORMAL;
    }

    return AI_SCREEN_LEVEL_UNKNOWN;
}

static const char *safe_level_text(AiScreenLevel level)
{
    switch (level) {
    case AI_SCREEN_LEVEL_NORMAL:
        return "正常";
    case AI_SCREEN_LEVEL_WARN:
        return "数据波动";
    case AI_SCREEN_LEVEL_DANGER:
        return "异常";
    case AI_SCREEN_LEVEL_UNKNOWN:
    default:
        return "已完成分析";
    }
}

static int ai_level_severity(AiScreenLevel level)
{
    switch (level) {
    case AI_SCREEN_LEVEL_NORMAL:
        return 1;
    case AI_SCREEN_LEVEL_WARN:
        return 2;
    case AI_SCREEN_LEVEL_DANGER:
        return 3;
    case AI_SCREEN_LEVEL_UNKNOWN:
    default:
        return 0;
    }
}

static AiScreenLevel max_ai_level(AiScreenLevel ai_level, AiScreenLevel local_level)
{
    return (ai_level_severity(local_level) > ai_level_severity(ai_level)) ? local_level : ai_level;
}

static const char *safe_local_judgement_text()
{
    return safe_level_text(ai_level_from_local_judgement(s_ai_summary.local_judgement));
}

static const char *safe_condition_text(AiScreenLevel level)
{
    if (!s_ai_summary.valid) {
        return "待检测";
    }
    if (s_ai_summary.min_spo2 > 0 && s_ai_summary.min_spo2 <= 90) {
        return "血氧低";
    }
    if (s_ai_summary.max_heart > (s_ai_summary.after_exercise ? 145 : 120)) {
        return "心率高";
    }
    if (s_ai_summary.min_heart > 0 && s_ai_summary.min_heart < 50) {
        return "心率低";
    }
    if (level == AI_SCREEN_LEVEL_DANGER) {
        return "异常";
    }
    if (level == AI_SCREEN_LEVEL_WARN) {
        return s_ai_summary.after_exercise ? "运动后数据波动" : "数据波动";
    }
    return "数据正常";
}

static const char *screen_condition_line(AiScreenLevel level)
{
    if (!s_ai_summary.valid) {
        return "待检测";
    }
    if (s_ai_summary.min_spo2 > 0 && s_ai_summary.min_spo2 <= 90) {
        return "血氧最低";
    }
    if (s_ai_summary.max_heart > (s_ai_summary.after_exercise ? 145 : 120)) {
        return "心率最高";
    }
    if (s_ai_summary.min_heart > 0 && s_ai_summary.min_heart < 50) {
        return "心率最低";
    }
    if (level == AI_SCREEN_LEVEL_DANGER) {
        return "数据异常";
    }
    if (level == AI_SCREEN_LEVEL_WARN) {
        return "数据波动";
    }
    return "数据正常";
}

static void build_local_elder_text(AiScreenLevel level, char *out, size_t out_size)
{
    if (out_size == 0) {
        return;
    }

    if (!s_ai_summary.valid) {
        snprintf(out, out_size, "评估未完成\n请先完成一次有效检测\n系统将生成风险分层\n异常提示：请复测并联系监护人");
    } else if (level == AI_SCREEN_LEVEL_DANGER) {
        snprintf(out, out_size, "综合评估：高风险\n异常重点：心率%d-%d次/分 血氧最低%d%%\n处置建议：立即停止活动并静息复测\n升级条件：胸闷气短头晕请联系监护人",
                 s_ai_summary.min_heart,
                 s_ai_summary.max_heart,
                 s_ai_summary.min_spo2);
    } else if (level == AI_SCREEN_LEVEL_WARN) {
        snprintf(out, out_size, "综合评估：需重点关注\n异常重点：心率%d-%d次/分 血氧最低%d%%\n处置建议：静息5分钟后复测\n升级条件：反复波动需记录症状并咨询",
                 s_ai_summary.min_heart,
                 s_ai_summary.max_heart,
                 s_ai_summary.min_spo2);
    } else {
        snprintf(out, out_size, "综合评估：低风险\n指标概览：心率%d-%d次/分 血氧最低%d%%\n处置建议：继续日常监测\n升级条件：不适或趋势下降需及时复测",
                 s_ai_summary.min_heart,
                 s_ai_summary.max_heart,
                 s_ai_summary.min_spo2);
    }
    out[out_size - 1] = '\0';
}

static void format_elder_screen_text(const char *elder_text, AiScreenLevel level, char *out, size_t out_size)
{
    if (out_size == 0) {
        return;
    }

    char source[192];
    if (elder_text != nullptr && elder_text[0] != '\0') {
        copy_utf8_chars(elder_text, source, sizeof(source), 54);
    } else {
        build_local_elder_text(level, source, sizeof(source));
    }

    size_t out_len = 0;
    int line_count = 1;
    for (const char *p = source; *p != '\0' && out_len + 1 < out_size;) {
        const unsigned char c0 = (unsigned char)p[0];
        if ((p[0] == ';' || p[0] == '/' || p[0] == '|') && line_count < 3) {
            out[out_len++] = '\n';
            p++;
            line_count++;
            continue;
        }
        if (c0 == 0xEF && (unsigned char)p[1] == 0xBC && (unsigned char)p[2] == 0x9B && line_count < 3) {
            out[out_len++] = '\n';
            p += 3;
            line_count++;
            continue;
        }

        const size_t len = utf8_char_len(p);
        if (out_len + len >= out_size) {
            break;
        }
        memcpy(out + out_len, p, len);
        out_len += len;
        p += len;
    }
    out[out_len] = '\0';
}

static void build_ai_screen_text(AiScreenLevel ai_level, const char *elder_text, char *out, size_t out_size)
{
    if (out_size == 0) {
        return;
    }
    (void)elder_text;

    const AiScreenLevel local_level = ai_level_from_local_judgement(s_ai_summary.local_judgement);
    ai_level = max_ai_level(ai_level, local_level);
    if (ai_level == AI_SCREEN_LEVEL_UNKNOWN) ai_level = local_level;

    build_local_elder_text(ai_level, out, out_size);
    out[out_size - 1] = '\0';
}

static void build_ai_screen_text(AiScreenLevel ai_level, char *out, size_t out_size)
{
    build_ai_screen_text(ai_level, nullptr, out, out_size);
}

static void build_ai_detail_log_text(AiScreenLevel ai_level, char *out, size_t out_size)
{
    if (out_size == 0) {
        return;
    }

    const AiScreenLevel local_level = ai_level_from_local_judgement(s_ai_summary.local_judgement);
    ai_level = max_ai_level(ai_level, local_level);
    if (ai_level == AI_SCREEN_LEVEL_UNKNOWN) ai_level = local_level;

    if (s_ai_summary.valid) {
        snprintf(out, out_size,
                 "AI判断:%s 本地:%s 情况:%s 心率%d-%d均%d 血氧%d-%d均%d",
                 safe_level_text(ai_level),
                 safe_local_judgement_text(),
                 safe_condition_text(ai_level),
                 s_ai_summary.min_heart,
                 s_ai_summary.max_heart,
                 s_ai_summary.avg_heart,
                 s_ai_summary.max_spo2,
                 s_ai_summary.min_spo2,
                 s_ai_summary.avg_spo2);
    } else {
        snprintf(out, out_size, "%s 待检测", safe_level_text(ai_level));
    }
    out[out_size - 1] = '\0';
}

static void show_ai_panel(const char *title, const char *text, uint32_t cloud_color,
                          const char *cloud_state, bool switch_to_history, int progress)
{
    if (lvgl_mipilcd_port_lock(0)) {
        if (cloud_state != nullptr) {
            ui_status_bar_set_cloud_state_text(cloud_state, cloud_color);
        }
        const bool reveal_panel = !(s_ai_panel_dismissed && switch_to_history);
        if (!reveal_panel) {
            lvgl_mipilcd_port_unlock();
            return;
        }
        if (is_valid_ui_obj(ui_Label30)) {
            lv_label_set_text(ui_Label30, title);
        }
        if (is_valid_ui_obj(ui_Label31)) {
            lv_label_set_text(ui_Label31, text);
        }
        if (is_valid_ui_obj(ui_Panel21)) {
            lv_obj_clear_flag(ui_Panel21, LV_OBJ_FLAG_HIDDEN);
        }
        if (is_valid_ui_obj(ui_BarAiProgress)) {
            int value = progress;
            if (value < 0) value = 0;
            if (value > 100) value = 100;
            lv_bar_set_value(ui_BarAiProgress, value, LV_ANIM_ON);
        }
        if (switch_to_history && ui_login_is_authenticated() && is_valid_ui_obj(ui_Screen4)) {
            lv_scr_load(ui_Screen4);
        }
        lvgl_mipilcd_port_unlock();
    }
}

static void show_network_required_notice()
{
    if (lvgl_mipilcd_port_lock(100)) {
        ui_status_bar_set_cloud_state_text("连接网络", 0xC00000);
        if (!is_valid_ui_obj(s_network_notice_panel)) {
            s_network_notice_panel = lv_obj_create(lv_scr_act());
            lv_obj_set_size(s_network_notice_panel, 420, 160);
            lv_obj_center(s_network_notice_panel);
            lv_obj_clear_flag(s_network_notice_panel, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(s_network_notice_panel, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(s_network_notice_panel, lv_color_hex(0x202A33), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(s_network_notice_panel, 235, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(s_network_notice_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

            lv_obj_t *title = lv_label_create(s_network_notice_panel);
            lv_label_set_text(title, "连接网络");
            lv_obj_set_width(title, 380);
            lv_obj_set_pos(title, 20, 28);
            lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(title, &ui_font_cn_28, LV_PART_MAIN | LV_STATE_DEFAULT);

            lv_obj_t *body = lv_label_create(s_network_notice_panel);
            lv_label_set_text(body, "WiFi未连接");
            lv_obj_set_width(body, 380);
            lv_obj_set_pos(body, 20, 86);
            lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(body, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(body, &ui_font_cn_24, LV_PART_MAIN | LV_STATE_DEFAULT);
        } else {
            lv_obj_move_foreground(s_network_notice_panel);
            lv_obj_clear_flag(s_network_notice_panel, LV_OBJ_FLAG_HIDDEN);
        }
        s_network_notice_visible = true;
        s_network_notice_hide_at_ms = millis() + NETWORK_NOTICE_SHOW_MS;
        lvgl_mipilcd_port_unlock();
    }
}

static void update_network_required_notice()
{
    if (!s_network_notice_visible) {
        return;
    }
    if ((int32_t)(millis() - s_network_notice_hide_at_ms) < 0) {
        return;
    }

    if (lvgl_mipilcd_port_lock(0)) {
        if (is_valid_ui_obj(s_network_notice_panel)) {
            lv_obj_add_flag(s_network_notice_panel, LV_OBJ_FLAG_HIDDEN);
        }
        lvgl_mipilcd_port_unlock();
    }
    s_network_notice_visible = false;
}

static void start_fake_ai_progress()
{
    s_ai_waiting_to_show = true;
    s_ai_progress_complete = false;
    s_ai_result_ready = false;
    s_ai_waiting_for_network = false;
    s_ai_panel_dismissed = false;
    s_ai_progress_start_ms = millis();
    s_ai_pending_text[0] = '\0';
    strncpy(s_ai_pending_title, "AI诊断", sizeof(s_ai_pending_title) - 1);
    s_ai_pending_title[sizeof(s_ai_pending_title) - 1] = '\0';
    strncpy(s_ai_pending_cloud_state, "AI分析中", sizeof(s_ai_pending_cloud_state) - 1);
    s_ai_pending_cloud_state[sizeof(s_ai_pending_cloud_state) - 1] = '\0';
    s_ai_pending_cloud_color = 0xD6A600;
}

static void cache_ai_result(const char *title, const char *text, uint32_t cloud_color, const char *cloud_state)
{
    strncpy(s_ai_pending_title, title, sizeof(s_ai_pending_title) - 1);
    s_ai_pending_title[sizeof(s_ai_pending_title) - 1] = '\0';
    strncpy(s_ai_pending_text, text, sizeof(s_ai_pending_text) - 1);
    s_ai_pending_text[sizeof(s_ai_pending_text) - 1] = '\0';
    strncpy(s_ai_pending_cloud_state, cloud_state, sizeof(s_ai_pending_cloud_state) - 1);
    s_ai_pending_cloud_state[sizeof(s_ai_pending_cloud_state) - 1] = '\0';
    s_ai_pending_cloud_color = cloud_color;
    s_ai_result_ready = true;
    if (!s_ai_waiting_to_show || s_ai_progress_complete) {
        s_ai_waiting_to_show = false;
        s_ai_progress_complete = true;
        show_ai_panel(s_ai_pending_title, s_ai_pending_text,
                      s_ai_pending_cloud_color, s_ai_pending_cloud_state, true, 100);
    }
}

static void update_fake_ai_progress()
{
    if (!s_ai_waiting_to_show) {
        return;
    }

    const uint32_t elapsed = millis() - s_ai_progress_start_ms;
    int progress = (int)((elapsed * 100UL) / C6_AI_FAKE_PROGRESS_MS);
    if (progress > 99 && elapsed < C6_AI_FAKE_PROGRESS_MS) {
        progress = 99;
    }
    if (progress > 100) {
        progress = 100;
    }

    show_ai_panel("AI诊断", "等待结果", 0xD6A600, "AI分析中", true, progress);

    if (elapsed >= C6_AI_FAKE_PROGRESS_MS) {
        if (s_ai_result_ready) {
            s_ai_waiting_to_show = false;
            s_ai_progress_complete = true;
            s_ai_waiting_for_network = false;
            show_ai_panel(s_ai_pending_title, s_ai_pending_text,
                          s_ai_pending_cloud_color, s_ai_pending_cloud_state, true, 100);
        } else if (s_ai_waiting_for_network || !s_c6_wifi_connected) {
            s_ai_waiting_to_show = false;
            s_ai_progress_complete = true;
            s_ai_waiting_for_network = false;
            show_network_required_notice();
        } else {
            char fallback[C6_AI_SCREEN_TEXT_SIZE];
            build_ai_screen_text(AI_SCREEN_LEVEL_UNKNOWN, fallback, sizeof(fallback));
            s_ai_waiting_to_show = false;
            s_ai_waiting_for_network = false;
            show_ai_panel("AI诊断", fallback, 0xD6A600, "等待结果", true, 100);
            s_ai_progress_complete = true;
        }
    }
}

static void set_c6_online_state(const char *text, uint32_t color_hex)
{
    if (lvgl_mipilcd_port_lock(0)) {
        ui_status_bar_set_c6_state_text(text, color_hex);
        lvgl_mipilcd_port_unlock();
    }
}

static int rssi_bucket(int rssi)
{
    if (rssi >= -55) return 2;       // 强
    if (rssi >= -75) return 1;       // 中
    return 0;                         // 弱
}

/*
 * Push the current C6 WiFi state to all registered LVGL labels.
 * Only updates the UI when something actually changed (throttle).
 */
static void sync_wifi_ui()
{
    const char *state_text;
    uint32_t    state_color;

    switch (s_c6_wifi_state) {
    case C6_WIFI_CONNECTING:
        state_text  = "连接中";
        state_color = 0xD6A600;  // yellow
        break;
    case C6_WIFI_CONNECTED:
        state_text  = "已连接";
        state_color = 0x1F9D45;  // green
        break;
    case C6_WIFI_RECONNECTING:
        state_text  = "连接中";
        state_color = 0xD6A600;  // yellow
        break;
    case C6_WIFI_DISCONNECTED:
        state_text  = "未连接";
        state_color = 0xC00000;  // red
        break;
    case C6_WIFI_UNRESPONSIVE:
    default:
        state_text  = "未连接";
        state_color = 0xC00000;  // red
        break;
    }

    const int bucket = rssi_bucket(s_c6_wifi_rssi);
    const bool state_changed  = (s_ui_wifi_state != s_c6_wifi_state);
    const bool ip_changed     = (strcmp(s_ui_wifi_ip, s_c6_wifi_ip) != 0);
    const bool rssi_changed   = (s_ui_wifi_rssi_bucket != bucket);

    if (!state_changed && !ip_changed && !rssi_changed) {
        return;  // nothing worth redrawing
    }

    s_ui_wifi_state     = s_c6_wifi_state;
    strncpy(s_ui_wifi_ip, s_c6_wifi_ip, sizeof(s_ui_wifi_ip) - 1);
    s_ui_wifi_ip[sizeof(s_ui_wifi_ip) - 1] = '\0';
    s_ui_wifi_rssi_bucket = bucket;

    if (lvgl_mipilcd_port_lock(0)) {
        ui_status_bar_set_wifi_state_text(state_text, state_color);

        /* Detail label: IP + RSSI when connected, else clear */
        if (s_c6_wifi_state == C6_WIFI_CONNECTED && s_c6_wifi_ip[0] != '\0') {
            char detail[64];
            snprintf(detail, sizeof(detail), "IP:%s  RSSI:%d",
                     s_c6_wifi_ip, s_c6_wifi_rssi);
            ui_status_bar_set_wifi_detail(detail);
        } else {
            ui_status_bar_set_wifi_detail(nullptr);
        }

        lvgl_mipilcd_port_unlock();
    }
}

static void apply_wifi_ui_state(bool connected)
{
    s_c6_wifi_connected = connected;
    if (!connected) {
        s_c6_time_request_pending = false;
        if (lvgl_mipilcd_port_lock(0)) {
            ui_status_bar_set_cloud_state_text("等待网络", 0xE87500);
            lvgl_mipilcd_port_unlock();
        }
    }
    if (lvgl_mipilcd_port_lock(0)) {
        ui_status_bar_set_wifi_connected(connected);
        if (connected) {
            ui_status_bar_set_cloud_state_text("待上传", 0x1F9D45);
        }
        lvgl_mipilcd_port_unlock();
    }
}

static void apply_network_time(time_t unix_time)
{
    if (unix_time < 1704067200) {
        debug_printf("[P4 SELFTEST] Ignore invalid C6 network time: %lld\r\n", (long long)unix_time);
        s_c6_time_request_pending = false;
        return;
    }

    if (lvgl_mipilcd_port_lock(100)) {
        ui_time_sync_set_unix(unix_time);
        lvgl_mipilcd_port_unlock();
        debug_printf("[P4 SELFTEST] Time synced from C6: %lld\r\n", (long long)unix_time);
        s_last_successful_time_sync_ms = millis();
        s_c6_time_request_pending = false;
    } else {
        debug_line("[P4 SELFTEST] Time sync from C6 skipped: LVGL busy");
        s_c6_time_request_pending = false;
    }
}

static void handle_c6_line(const String &line)
{
    debug_printf("[P4 SELFTEST][C6 <-] %s\r\n", line.c_str());
    s_last_c6_rx_ms = millis();
    s_c6_marked_unresponsive = false;
    set_c6_online_state("在线", 0x1F9D45);

    if (!s_c6_handshake_done) {
        s_c6_handshake_done = true;
        s_c6_last_wifi_msg_ms = millis();  // any C6 message resets timeout
        debug_line("[P4 SELFTEST] UART link OK: received data from C6");
        send_c6_line("WECHAT STATUS?");
    }

    if (line == "PONG") {
        debug_line("[P4 SELFTEST] UART PONG received from C6");
        return;
    }

    if (line == "AI BUSY") {
        show_ai_panel("AI诊断", "等待结果", 0xD6A600, "AI分析中", true, 25);
        return;
    }

    if (line == "AI BEGIN") {
        reset_ai_buffer();
        return;
    }

    if (line.startsWith("AI TEXT")) {
        const char *text = "";
        if (line.length() > 8 && line.charAt(7) == ' ') {
            text = line.c_str() + 8;
        }
        append_ai_text(text);
        return;
    }

    if (line == "AI END") {
        char screen_text[C6_AI_SCREEN_TEXT_SIZE];
        char elder_text[192] = "";
        const AiScreenLevel ai_level = parse_ai_level(s_ai_buffer);
        const AiScreenLevel local_level = ai_level_from_local_judgement(s_ai_summary.local_judgement);
        const AiScreenLevel screen_level = max_ai_level(ai_level, local_level);
        extract_elder_text(s_ai_buffer, elder_text, sizeof(elder_text));
        build_ai_screen_text(screen_level, elder_text, screen_text, sizeof(screen_text));
        debug_printf("[P4 SELFTEST] AI level=%d local=%d screen=%d text=\"%s\"\r\n",
                     (int)ai_level, (int)local_level, (int)screen_level, screen_text);
        char detail_log[192];
        build_ai_detail_log_text(screen_level, detail_log, sizeof(detail_log));
        debug_printf("[P4 SELFTEST] AI detail %s\r\n", detail_log);
        s_ai_receiving = false;
        cache_ai_result("AI诊断", screen_text, 0x1F9D45, "AI完成");
        return;
    }

    if (line.startsWith("AI ERROR")) {
        char error_text[C6_AI_SCREEN_TEXT_SIZE];
        build_ai_screen_text(AI_SCREEN_LEVEL_UNKNOWN, error_text, sizeof(error_text));
        debug_printf("[P4 SELFTEST] AI error fallback screen=\"%s\"\r\n", error_text);
        s_ai_receiving = false;
        cache_ai_result("AI诊断", error_text, 0xD32F2F, "AI异常");
        return;
    }

    if (line == "CLOUD STATUS ON") {
        if (lvgl_mipilcd_port_lock(0)) {
            ui_status_bar_set_cloud_state_text("已同步", 0x1F9D45);
            lvgl_mipilcd_port_unlock();
        }
        return;
    }

    if (line == "CLOUD STATUS OFF") {
        if (lvgl_mipilcd_port_lock(0)) {
            ui_status_bar_set_cloud_state_text("未同步", 0xC00000);
            lvgl_mipilcd_port_unlock();
        }
        return;
    }

    if (line == "WECHAT STATUS ON") {
        if (lvgl_mipilcd_port_lock(0)) {
            ui_status_bar_set_wechat_state_text("已开启", 0x1F9D45);
            lvgl_mipilcd_port_unlock();
        }
        return;
    }

    if (line == "WECHAT STATUS OFF") {
        if (lvgl_mipilcd_port_lock(0)) {
            ui_status_bar_set_wechat_state_text("未开启", 0xC00000);
            lvgl_mipilcd_port_unlock();
        }
        return;
    }

    if (line.startsWith("WECHAT ERROR")) {
        if (lvgl_mipilcd_port_lock(0)) {
            ui_status_bar_set_wechat_state_text("提醒异常", 0xD32F2F);
            lvgl_mipilcd_port_unlock();
        }
        return;
    }

    /* ---- WiFi rich-state messages ---- */

    if (line == "WIFI CONNECTING") {
        s_c6_wifi_state = C6_WIFI_CONNECTING;
        s_c6_wifi_ip[0] = '\0';
        s_c6_wifi_rssi = 0;
        s_c6_last_wifi_msg_ms = millis();
        debug_line("[P4 SELFTEST] C6 WiFi state: CONNECTING");
        sync_wifi_ui();
        return;
    }

    if (line.startsWith("WIFI RECONNECTING")) {
        s_c6_wifi_state = C6_WIFI_RECONNECTING;
        s_c6_wifi_ip[0] = '\0';
        s_c6_wifi_rssi = 0;
        s_c6_last_wifi_msg_ms = millis();
        debug_line("[P4 SELFTEST] C6 WiFi state: RECONNECTING");
        sync_wifi_ui();
        return;
    }

    /* WIFI STATUS STATE=CONNECTED IP=xxx RSSI=-xx */
    if (line.startsWith("WIFI STATUS")) {
        s_c6_wifi_state = C6_WIFI_CONNECTED;
        s_c6_wifi_connected = true;
        s_c6_last_wifi_msg_ms = millis();

        const int ip_pos = line.indexOf("IP=");
        if (ip_pos >= 0) {
            const int ip_end = line.indexOf(' ', ip_pos);
            String ip_str = (ip_end >= 0)
                ? line.substring(ip_pos + 3, ip_end)
                : line.substring(ip_pos + 3);
            strncpy(s_c6_wifi_ip, ip_str.c_str(), sizeof(s_c6_wifi_ip) - 1);
            s_c6_wifi_ip[sizeof(s_c6_wifi_ip) - 1] = '\0';
        }

        const int rssi_pos = line.indexOf("RSSI=");
        if (rssi_pos >= 0) {
            s_c6_wifi_rssi = line.substring(rssi_pos + 5).toInt();
        }

        /* Handle the WIFI STATUS as equivalent to WIFI CONNECTED for legacy flow */
        debug_printf("[P4 SELFTEST] C6 WiFi state: CONNECTED ip=%s rssi=%d\r\n",
                     s_c6_wifi_ip, s_c6_wifi_rssi);
        apply_wifi_ui_state(true);
        request_c6_time_sync(false);
        sync_wifi_ui();
        return;
    }

    /* ---- Legacy messages ---- */

    if (line.startsWith("WIFI CONNECTED")) {
        s_c6_wifi_state = C6_WIFI_CONNECTED;
        s_c6_last_wifi_msg_ms = millis();
        /* Extract IP from legacy WIFI CONNECTED IP=xxx */
        const int ip_pos = line.indexOf("IP=");
        if (ip_pos >= 0) {
            String ip_str = line.substring(ip_pos + 3);
            ip_str.trim();
            strncpy(s_c6_wifi_ip, ip_str.c_str(), sizeof(s_c6_wifi_ip) - 1);
            s_c6_wifi_ip[sizeof(s_c6_wifi_ip) - 1] = '\0';
        }
        debug_printf("[P4 SELFTEST] C6 WiFi legacy connected ip=%s\r\n", s_c6_wifi_ip);
        apply_wifi_ui_state(true);
        request_c6_time_sync(false);
        sync_wifi_ui();
        return;
    }

    if (line.startsWith("TIME UNIX=")) {
        const long long unix_time = strtoll(line.c_str() + 10, nullptr, 10);
        apply_network_time((time_t)unix_time);
        s_c6_last_wifi_msg_ms = millis();
        return;
    }

    if (line == "TIME SYNC FAILED" || line == "TIME WIFI NOT CONNECTED") {
        s_c6_time_request_pending = false;
        s_c6_last_wifi_msg_ms = millis();
        return;
    }

    if (line == "WIFI DISCONNECTED" || line == "WIFI OFF" || line == "WIFI CONFIG MISSING") {
        s_c6_wifi_state = C6_WIFI_DISCONNECTED;
        s_c6_wifi_ip[0] = '\0';
        s_c6_wifi_rssi = 0;
        s_c6_last_wifi_msg_ms = millis();
        debug_line("[P4 SELFTEST] C6 WiFi state: DISCONNECTED/OFF/CONFIG MISSING");
        apply_wifi_ui_state(false);
        sync_wifi_ui();
        return;
    }
}

static void read_c6_lines()
{
    while (C6Serial.available() > 0) {
        String line = C6Serial.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            handle_c6_line(line);
        }
    }
}

static void send_c6_line(const char *line)
{
    C6Serial.println(line);
    debug_printf("[P4 SELFTEST][C6 ->] %s\r\n", line);
}

static void send_c6_line(const String &line)
{
    C6Serial.println(line);
    debug_printf("[P4 SELFTEST][C6 ->] %s\r\n", line.c_str());
}

static void request_c6_time_sync(bool force)
{
    if (!s_c6_wifi_connected || s_c6_time_request_pending) {
        return;
    }

    const uint32_t now = millis();
    if (!force && s_last_successful_time_sync_ms > 0
        && now - s_last_successful_time_sync_ms < C6_TIME_SYNC_INTERVAL_MS) {
        return;
    }
    if (!force && s_last_c6_time_request_ms > 0 && now - s_last_c6_time_request_ms < 10000) {
        return;
    }

    s_last_c6_time_request_ms = now;
    s_c6_time_request_pending = true;
    send_c6_line("TIME?");
}

void c6_network_init(void)
{
    C6Serial.setRxBufferSize(2048);
    C6Serial.begin(C6_UART_BAUD, SERIAL_8N1, C6_UART_RX_PIN, C6_UART_TX_PIN);
    debug_printf("[P4 SELFTEST] C6 UART begin RX=%d TX=%d baud=%lu\r\n",
                 C6_UART_RX_PIN, C6_UART_TX_PIN, (unsigned long)C6_UART_BAUD);
}

void c6_network_probe_uart(void)
{
    debug_line("[P4 SELFTEST] C6 UART PROBE start pre-LCD PING burst");
    for (int i = 0; i < 5; i++) {
        debug_printf("[P4 SELFTEST] C6 UART PROBE ping %d/5\r\n", i + 1);
        send_c6_line("PING");
        const uint32_t start_ms = millis();
        while (millis() - start_ms < 300) {
            read_c6_lines();
            delay(10);
        }
    }
    debug_line(s_c6_handshake_done ? "[P4 SELFTEST] C6 UART PROBE PASS: C6 replied before LCD" :
                                     "[P4 SELFTEST] C6 UART PROBE PENDING: no early C6 reply before LCD");
}

void c6_network_pre_lcd_handshake(void)
{
    uint32_t start_ms = millis();
    s_last_c6_ping_ms = start_ms;
    set_c6_online_state("连接中", 0xE87500);
    send_c6_line("PING");

    while (!s_c6_handshake_done && millis() - start_ms < 1500) {
        read_c6_lines();
        delay(10);
    }

    if (!s_c6_handshake_done) {
        debug_line("[P4 SELFTEST] C6 pre-LCD handshake pending");
        set_c6_online_state("待连接", 0xE87500);
    }
}

void c6_network_poll(void)
{
    read_c6_lines();
    update_fake_ai_progress();
    update_network_required_notice();

    uint32_t now = millis();
    if (!s_c6_handshake_done && now - s_last_c6_ping_ms >= 2000) {
        s_last_c6_ping_ms = now;
        send_c6_line("PING");
    }

    if (s_c6_handshake_done && now - s_last_c6_status_request_ms >= C6_STATUS_REQUEST_INTERVAL_MS) {
        s_last_c6_status_request_ms = now;
        send_c6_line("STATUS?");
        send_c6_line("WECHAT STATUS?");
    }

    if (s_c6_wifi_connected) {
        request_c6_time_sync(false);
    }

    /* 35s timeout: if no WiFi message from C6 → UNRESPONSIVE */
    if (s_c6_handshake_done
        && s_c6_last_wifi_msg_ms > 0
        && now - s_c6_last_wifi_msg_ms >= C6_WIFI_TIMEOUT_MS
        && s_c6_wifi_state != C6_WIFI_UNRESPONSIVE) {
        s_c6_wifi_state = C6_WIFI_UNRESPONSIVE;
        s_c6_wifi_connected = false;
        s_c6_marked_unresponsive = true;
        apply_wifi_ui_state(false);
        set_c6_online_state("未连接", 0xC00000);
        sync_wifi_ui();
    }

    if (s_c6_handshake_done
        && s_last_c6_rx_ms > 0
        && now - s_last_c6_rx_ms >= C6_RESPONSE_TIMEOUT_MS
        && !s_c6_marked_unresponsive) {
        s_c6_marked_unresponsive = true;
        set_c6_online_state("未连接", 0xC00000);
    }
}

void c6_network_set_enabled(bool enabled)
{
    s_c6_network_enabled = enabled;
    if (enabled) {
        send_c6_line("WIFI ON");
    } else {
        send_c6_line("WIFI OFF");
        apply_wifi_ui_state(false);
        set_c6_online_state("在线", 0x1F9D45);
    }
}

void c6_network_set_wechat_enabled(bool enabled)
{
    send_c6_line(enabled ? "WECHAT ON" : "WECHAT OFF");
}

void c6_network_set_cloud_sync_enabled(bool enabled)
{
    if (lvgl_mipilcd_port_lock(0)) {
        ui_status_bar_set_cloud_state_text(enabled ? "同步中" : "未同步",
                                           enabled ? 0xD6A600 : 0xC00000);
        lvgl_mipilcd_port_unlock();
    }
    send_c6_line(enabled ? "CLOUD SYNC ON" : "CLOUD SYNC OFF");
}

void c6_network_request_status(void)
{
    send_c6_line("STATUS?");
}

void c6_network_set_latest_ai_summary(int max_heart, int min_heart, int avg_heart,
                                      int max_spo2, int min_spo2, int avg_spo2,
                                      int detect_hour,
                                      bool after_exercise,
                                      const char *local_judgement)
{
    s_ai_summary.valid = true;
    s_ai_summary.max_heart = max_heart;
    s_ai_summary.min_heart = min_heart;
    s_ai_summary.avg_heart = avg_heart;
    s_ai_summary.max_spo2 = max_spo2;
    s_ai_summary.min_spo2 = min_spo2;
    s_ai_summary.avg_spo2 = avg_spo2;
    s_ai_summary.detect_hour = detect_hour;
    s_ai_summary.after_exercise = after_exercise;
    if (local_judgement == nullptr) {
        local_judgement = "";
    }
    strncpy(s_ai_summary.local_judgement, local_judgement, sizeof(s_ai_summary.local_judgement) - 1);
    s_ai_summary.local_judgement[sizeof(s_ai_summary.local_judgement) - 1] = '\0';
    debug_printf("[P4 SELFTEST] AI summary cached HR=%d/%d/%d SPO2=%d/%d/%d hour=%d mode=%s local=%s\r\n",
                 s_ai_summary.max_heart, s_ai_summary.min_heart, s_ai_summary.avg_heart,
                 s_ai_summary.max_spo2, s_ai_summary.min_spo2, s_ai_summary.avg_spo2,
                 s_ai_summary.detect_hour,
                 s_ai_summary.after_exercise ? "after_exercise" : "rest",
                 s_ai_summary.local_judgement);
}

void c6_network_send_ai_question(const char *question)
{
    if (question == nullptr || question[0] == '\0') {
        return;
    }

    if (!s_c6_wifi_connected) {
        s_ai_waiting_to_show = false;
        s_ai_progress_complete = false;
        s_ai_result_ready = false;
        s_ai_waiting_for_network = false;
        show_network_required_notice();
        debug_line("[P4 SELFTEST] AI request skipped: WiFi is not connected");
        return;
    }

    start_fake_ai_progress();
    show_ai_panel("AI诊断", "等待结果", 0xD6A600, "AI分析中", true, 1);
    String line = "AI ASK ";
    line += question;
    send_c6_line(line);
}

void c6_network_dismiss_ai_panel(void)
{
    s_ai_panel_dismissed = true;
}

bool c6_network_is_connected(void)
{
    return s_c6_wifi_connected;
}

c6_wifi_state_t c6_network_wifi_state(void)
{
    return s_c6_wifi_state;
}

const char *c6_network_wifi_ip(void)
{
    return s_c6_wifi_ip;
}

int c6_network_wifi_rssi(void)
{
    return s_c6_wifi_rssi;
}

/*
 * Call once after ui_init() to bind the Screen2 module state label
 * (cui_Labelwifi1, child index 9 of ui_Panelscreen2).
 */
void c6_network_bind_wifi_detail_ui(void)
{
    lv_obj_t *panelscreen2 = ui_Panelscreen2;
    if (panelscreen2 == nullptr) return;

    lv_obj_t *detail_label = ui_comp_get_child(panelscreen2,
        UI_COMP_PANELSCREEN2_PANELWIFI_LABELWIFI1);
    if (detail_label != nullptr) {
        ui_status_bar_register_c6_state_label(detail_label);
    }
}
