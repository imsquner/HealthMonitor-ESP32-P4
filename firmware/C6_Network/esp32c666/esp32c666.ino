#include <Arduino.h>
#include <stdarg.h>
#include <time.h>
#include <WiFi.h>

#include "volcano_ai.h"
#include "wechat_push.h"
#include "secrets.h"

static HardwareSerial P4Serial(1);

static const int P4_UART_RX_PIN = 4;  // C6 GPIO4 / LP_UART_RXD, connect to P4 TXD
static const int P4_UART_TX_PIN = 5;  // C6 GPIO5 / LP_UART_TXD, connect to P4 RXD
static const uint32_t P4_UART_BAUD = 115200;
// Wi-Fi credentials are provided by secrets.h
static const char *NTP_SERVER_1 = "ntp.aliyun.com";
static const char *NTP_SERVER_2 = "cn.pool.ntp.org";
static const char *NTP_SERVER_3 = "pool.ntp.org";

// WeCom webhook URL is provided by secrets.h

static wl_status_t s_last_wifi_status = WL_NO_SHIELD;
static bool s_wifi_config_reported = false;
static bool s_wifi_enabled = false;
static bool s_wechat_enabled = true;
static uint32_t s_last_wifi_check_ms = 0;

// ---- 开机企业微信通知状态机 ----
static enum {
    SN_WAIT_WIFI,
    SN_WAIT_NTP,
    SN_SENDING,
    SN_RETRY_WAIT,
    SN_DONE,
    SN_ABANDONED
} s_sn_state = SN_WAIT_WIFI;
static uint32_t s_sn_wifi_since_ms = 0;     // when WiFi first connected (for NTP timeout)
static uint32_t s_sn_next_retry_ms = 0;     // millis() deadline for retry
static int      s_sn_attempts = 0;
static bool     s_sn_wifi_was_connected = false; // edge-detect WiFi connect

// ---- WiFi 状态上报与自动重连 ----
static uint32_t s_wifi_heartbeat_ms  = 0;   // 10s heartbeat timer
static uint32_t s_wifi_reconnect_ms  = 0;   // 5s reconnect timer
static int      s_wifi_reconnect_retry = 0;
static bool     s_wifi_edge_connected = false; // edge-detect for initial heartbeat

static char s_recent_p4_tx[4][96] = {{0}};
static uint32_t s_recent_p4_tx_ms[4] = {0};
static uint8_t s_recent_p4_tx_index = 0;

static void remember_p4_tx(const String &text)
{
    const size_t index = s_recent_p4_tx_index % 4;
    text.toCharArray(s_recent_p4_tx[index], sizeof(s_recent_p4_tx[index]));
    s_recent_p4_tx_ms[index] = millis();
    s_recent_p4_tx_index++;
}

static bool is_recent_p4_tx_echo(const String &line)
{
    const uint32_t now = millis();
    for (size_t i = 0; i < 4; i++) {
        if (s_recent_p4_tx[i][0] == '\0') {
            continue;
        }
        if (now - s_recent_p4_tx_ms[i] > 3000) {
            continue;
        }
        if (line.equals(s_recent_p4_tx[i])) {
            return true;
        }
    }
    return false;
}

static bool is_known_p4_command(const String &line)
{
    return line == "PING" ||
           line == "WIFI ON" ||
           line == "WIFI OFF" ||
           line == "STATUS?" ||
           line == "TIME?" ||
           line == "WECHAT ON" ||
           line == "WECHAT OFF" ||
           line == "WECHAT STATUS?" ||
           line == "AI STATUS?" ||
           line.startsWith("AI ASK ") ||
           line == "CLOUD SYNC ON" ||
           line == "CLOUD SYNC OFF";
}

static bool recover_known_p4_command(const String &raw, String *command)
{
    static const char *exact_commands[] = {
        "PING",
        "WIFI ON",
        "WIFI OFF",
        "STATUS?",
        "TIME?",
        "WECHAT ON",
        "WECHAT OFF",
        "WECHAT STATUS?",
        "AI STATUS?",
        "CLOUD SYNC ON",
        "CLOUD SYNC OFF",
    };

    for (size_t i = 0; i < sizeof(exact_commands) / sizeof(exact_commands[0]); i++) {
        if (raw.indexOf(exact_commands[i]) >= 0) {
            *command = exact_commands[i];
            return true;
        }
    }

    const int ai_pos = raw.indexOf("AI ASK ");
    if (ai_pos >= 0) {
        *command = raw.substring(ai_pos);
        command->trim();
        return command->length() > 7;
    }

    return false;
}

static void debug_println(const char *text)
{
    Serial.println(text);
    Serial0.println(text);
}

static void debug_printf(const char *format, ...)
{
    char buffer[160];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Serial.print(buffer);
    Serial0.print(buffer);
}

static void send_to_p4(const char *text)
{
    P4Serial.println(text);
    remember_p4_tx(String(text));
    Serial.print("[P4 ->] ");
    Serial.println(text);
    Serial0.print("[P4 ->] ");
    Serial0.println(text);
}

static void send_to_p4(const String &text)
{
    P4Serial.println(text);
    remember_p4_tx(text);
    Serial.print("[P4 ->] ");
    Serial.println(text);
    Serial0.print("[P4 ->] ");
    Serial0.println(text);
}

static bool send_wechat_notice(const char *title, const char *status)
{
    if (WiFi.status() != WL_CONNECTED) {
        send_to_p4("WECHAT ERROR WIFI NOT CONNECTED");
        debug_println("[WECHAT] notice skipped: WiFi not connected");
        return false;
    }

    String md;
    md += "**";
    md += title;
    md += "**\n";
    md += "> 设备：健康守护心率血氧检测装置\n";
    md += "> 状态：";
    md += status;

    String error;
    if (wechat_push_send_markdown(WECHAT_WEBHOOK_URL, md, &error)) {
        debug_println("[WECHAT] notice sent");
        return true;
    }

    if (error.length() == 0) error = "unknown";
    debug_printf("[WECHAT] notice failed: %s\r\n", error.c_str());
    send_to_p4(String("WECHAT ERROR ") + error);
    return false;
}

static String compact_wechat_text(String text, int max_bytes)
{
    text.replace("\r", " ");
    text.replace("\n", " ");
    text.trim();
    if (text.length() <= max_bytes) {
        return text;
    }
    const int end = next_uart_chunk_end(text, 0, max_bytes - 3);
    return text.substring(0, end) + "...";
}

static String clean_field_text(String text)
{
    text.replace("\r", " ");
    text.replace("\n", " ");
    text.replace("@@|AI|", "");
    text.replace("|##", "");
    int level_pos = text.indexOf("LEVEL:");
    while (level_pos >= 0) {
        int end = text.indexOf('|', level_pos);
        if (end < 0) end = text.length();
        text.remove(level_pos, end - level_pos + ((end < text.length()) ? 1 : 0));
        level_pos = text.indexOf("LEVEL:");
    }
    text.trim();
    return text;
}

static String chinese_level(String level)
{
    level.trim();
    level.toLowerCase();
    if (level == "normal") return "正常";
    if (level == "warn" || level == "warning") return "数据波动";
    if (level == "danger") return "异常";
    return "数据波动";
}

static int risk_severity_from_text(String text)
{
    text.trim();
    text.toLowerCase();
    if (text.indexOf("danger") >= 0 || text.indexOf("异常") >= 0 || text.indexOf("危险") >= 0) {
        return 3;
    }
    if (text.indexOf("warn") >= 0 || text.indexOf("warning") >= 0 ||
        text.indexOf("数据波动") >= 0 || text.indexOf("关注") >= 0 || text.indexOf("波动") >= 0) {
        return 2;
    }
    if (text.indexOf("normal") >= 0 || text.indexOf("正常") >= 0) {
        return 1;
    }
    return 0;
}

static const char *risk_text_from_severity(int risk)
{
    if (risk >= 3) return "异常";
    if (risk == 2) return "数据波动";
    if (risk == 1) return "正常";
    return "数据波动";
}

static int extract_local_risk(const String &question)
{
    const int pos = question.indexOf("本地判断:");
    if (pos < 0) {
        return 0;
    }
    String field = question.substring(pos + strlen("本地判断:"));
    int end = field.indexOf(';');
    if (end < 0) end = field.indexOf('。');
    if (end >= 0) field = field.substring(0, end);
    return risk_severity_from_text(field);
}

static int extract_metric_after(const String &text, const char *key)
{
    const int pos = text.indexOf(key);
    if (pos < 0) {
        return -1;
    }
    return text.substring(pos + strlen(key)).toInt();
}

static String time_period_text(int hour)
{
    if (hour >= 5 && hour <= 9) return "早晨起身或活动后";
    if (hour >= 10 && hour <= 16) return "日间活动后";
    if (hour >= 17 && hour <= 21) return "晚间活动后";
    if (hour >= 0 && hour <= 4) return "夜间静息时";
    return "当前检测时段";
}

static String build_behavior_hint(int risk, int hr_max, int hr_min, int spo2_min, int hour)
{
    String hint;
    hint += time_period_text(hour);
    if (spo2_min >= 0 && spo2_min <= 90) {
        hint += "出现血氧低值";
        if (hr_max > 120) hint += "并伴心率偏高";
        hint += "，请先确认佩戴稳定并静息复测";
    } else if (hr_max > 120) {
        hint += "心率偏高，可能与活动、紧张或刚起身有关，建议静息复测";
    } else if (hr_min > 0 && hr_min < 50) {
        hint += "心率偏低，建议结合头晕、乏力等感受观察";
    } else if (risk >= 2) {
        hint += "数据波动，可能与活动或佩戴变化有关，建议复测";
    } else {
        hint += "数据与日常状态基本一致，继续观察";
    }
    hint += "。";
    return hint;
}

static int count_occurrences(const String &text, const char *token)
{
    int count = 0;
    int pos = text.indexOf(token);
    while (pos >= 0) {
        count++;
        pos = text.indexOf(token, pos + strlen(token));
    }
    return count;
}

static String build_history_trend_summary(const String &question)
{
    const int pos = question.indexOf("历史:");
    if (pos < 0) {
        return "";
    }

    String history = question.substring(pos + strlen("历史:"));
    history.trim();
    if (history.length() == 0) {
        return "";
    }

    int records = 0;
    int danger_count = 0;
    int warn_count = 0;
    int normal_count = 0;
    int hr_high_repeats = 0;
    int hr_low_repeats = 0;
    int spo2_low_repeats = 0;
    int spo2_warn_repeats = 0;
    int overall_hr_max = 0;
    int overall_hr_min = 999;
    int overall_spo2_min = 101;
    int latest_hr_max = -1;
    int latest_hr_min = -1;
    int latest_spo2_min = -1;

    int start = 0;
    while (start >= 0 && start < history.length()) {
        int hash = history.indexOf("#", start);
        if (hash < 0) break;
        int next = history.indexOf(";", hash);
        String record = (next >= 0) ? history.substring(hash, next) : history.substring(hash);

        const int heart_pos = record.indexOf("心");
        const int heart_dash = (heart_pos >= 0) ? record.indexOf("-", heart_pos) : -1;
        const int heart_avg = (heart_dash >= 0) ? record.indexOf("均", heart_dash) : -1;
        const int spo2_pos = record.indexOf("氧");
        const int spo2_dash = (spo2_pos >= 0) ? record.indexOf("-", spo2_pos) : -1;
        const int spo2_avg = (spo2_dash >= 0) ? record.indexOf("均", spo2_dash) : -1;
        if (heart_pos >= 0 && heart_dash > heart_pos && heart_avg > heart_dash &&
            spo2_pos >= 0 && spo2_dash > spo2_pos && spo2_avg > spo2_dash) {
            const int hr_min = record.substring(heart_pos + strlen("心"), heart_dash).toInt();
            const int hr_max = record.substring(heart_dash + 1, heart_avg).toInt();
            const int spo2_min = record.substring(spo2_pos + strlen("氧"), spo2_dash).toInt();

            if (records == 0) {
                latest_hr_min = hr_min;
                latest_hr_max = hr_max;
                latest_spo2_min = spo2_min;
            }
            records++;
            if (hr_max > overall_hr_max) overall_hr_max = hr_max;
            if (hr_min > 0 && hr_min < overall_hr_min) overall_hr_min = hr_min;
            if (spo2_min > 0 && spo2_min < overall_spo2_min) overall_spo2_min = spo2_min;
            if (hr_max >= 120) hr_high_repeats++;
            if (hr_min > 0 && hr_min < 50) hr_low_repeats++;
            if (spo2_min > 0 && spo2_min <= 90) spo2_low_repeats++;
            if (spo2_min > 0 && spo2_min <= 95) spo2_warn_repeats++;
        }

        const int severity = risk_severity_from_text(record);
        if (severity >= 3) danger_count++;
        else if (severity == 2) warn_count++;
        else if (severity == 1) normal_count++;

        if (next < 0) break;
        start = next + 1;
    }

    if (records <= 0) {
        return "";
    }

    String summary;
    summary += "历史总结：已纳入系统保存的";
    summary += records;
    summary += "次记录，历史最高心率";
    summary += overall_hr_max;
    summary += "次/分、最低心率";
    summary += (overall_hr_min == 999 ? 0 : overall_hr_min);
    summary += "次/分、最低血氧";
    summary += (overall_spo2_min == 101 ? 0 : overall_spo2_min);
    summary += "%。";

    if (records == 1) {
        summary += "当前样本仍少，先作为个人基线起点，建议接下来固定时段连续记录。";
    } else if (spo2_low_repeats >= 2 || hr_high_repeats >= 2 || hr_low_repeats >= 2) {
        summary += "已出现重复风险点：";
        if (spo2_low_repeats >= 2) {
            summary += "低血氧";
            summary += spo2_low_repeats;
            summary += "次";
        }
        if (hr_high_repeats >= 2) {
            if (!summary.endsWith("：")) summary += "、";
            summary += "心率偏高";
            summary += hr_high_repeats;
            summary += "次";
        }
        if (hr_low_repeats >= 2) {
            if (!summary.endsWith("：")) summary += "、";
            summary += "心率偏低";
            summary += hr_low_repeats;
            summary += "次";
        }
        summary += "，建议把复测结果和胸闷、气短、头晕、乏力等症状一起记录。";
    } else if (danger_count > 0) {
        summary += "既往记录曾触发异常风险，本次应重点复核佩戴稳定性，并观察极值是否再次出现。";
    } else if (warn_count >= 2 || spo2_warn_repeats >= 2) {
        summary += "多次记录存在波动，建议在静息5分钟后、同一时段复测，减少活动和佩戴松动干扰。";
    } else if (normal_count >= records - 1) {
        summary += "整体趋势较平稳，可继续作为日常健康基线，但仍需关注后续下滑趋势。";
    } else {
        summary += "趋势仍不充分，建议增加连续记录后再判断个人基线。";
    }

    if (latest_hr_max >= 0 && latest_spo2_min >= 0) {
        summary += "本次对照历史：心率最高";
        summary += latest_hr_max;
        summary += "次/分，最低血氧";
        summary += latest_spo2_min;
        summary += "%；若与历史极值接近或伴随不适，应优先复测并联系监护人。";
    }
    return summary;
}

static String build_guardian_ai_notice(const String &question, const String &answer)
{
    const int hr_avg = extract_metric_after(question, "心率均值");
    const int hr_max = extract_metric_after(question, "最高");
    const int hr_min = extract_metric_after(question, "最低");
    const int spo2_pos = question.indexOf("血氧均值");
    const String spo2_part = (spo2_pos >= 0) ? question.substring(spo2_pos) : "";
    const int spo2_avg = extract_metric_after(spo2_part, "血氧均值");
    const int spo2_max = extract_metric_after(spo2_part, "最高");
    const int spo2_min = extract_metric_after(spo2_part, "最低");
    const int detect_hour = extract_metric_after(question, "检测小时");
    const int local_risk = extract_local_risk(question);
    const int level_pos = answer.indexOf("LEVEL:");
    String level = (level_pos >= 0) ? answer.substring(level_pos + 6) : "";
    const int level_end = level.indexOf("|");
    if (level_end >= 0) {
        level = level.substring(0, level_end);
    }
    const int ai_risk = risk_severity_from_text(level);
    int risk = (local_risk > ai_risk) ? local_risk : ai_risk;
    if (risk == 0) {
        risk = 2;
    }

    String child = answer;
    const int child_pos = child.indexOf("CHILD:");
    if (child_pos >= 0) {
        child = child.substring(child_pos + 6);
        int end = child.indexOf("|LEVEL:");
        if (end < 0) end = child.indexOf("|##");
        if (end >= 0) child = child.substring(0, end);
    }
    child = clean_field_text(child);

    String notice;
    notice += "本次健康检测完成。\n";
    notice += "> 风险：";
    notice += risk_text_from_severity(risk);
    notice += "\n";
    if (hr_avg >= 0) {
        notice += "> 数据：心率最高";
        notice += hr_max;
        notice += "、最低";
        notice += hr_min;
        notice += "、均值";
        notice += hr_avg;
        notice += "次/分";
    }
    if (spo2_avg >= 0) {
        if (hr_avg >= 0) notice += "；";
        else notice += "> 数据：";
        notice += "血氧最高";
        notice += spo2_max;
        notice += "%、最低";
        notice += spo2_min;
        notice += "%、均值";
        notice += spo2_avg;
        notice += "%";
    }
    notice += "。\n";
    notice += "> 推测：";
    notice += build_behavior_hint(risk, hr_max, hr_min, spo2_min, detect_hour);
    notice += "\n";
    if (child.length() > 0) {
        notice += "> 建议：";
        notice += child;
        if (!child.endsWith("。")) {
            notice += "。";
        }
    }
    String history_summary = build_history_trend_summary(question);
    if (history_summary.length() > 0) {
        notice += "\n> ";
        notice += history_summary;
    }
    notice += "\n> 处置提醒：";
    if (risk >= 3) {
        notice += "若复测仍异常或老人不适，请联系专业人员。";
    } else {
        notice += "请结合老人当下感受观察。";
    }
    return notice;
}

static bool wifi_config_valid()
{
    return WIFI_SSID[0] != '\0';
}

static void report_wifi_status(bool force)
{
    if (!s_wifi_enabled) {
        if (force) {
            send_to_p4("WIFI OFF");
        }
        return;
    }

    if (!wifi_config_valid()) {
        if (force || !s_wifi_config_reported) {
            s_wifi_config_reported = true;
            send_to_p4("WIFI CONFIG MISSING");
        }
        return;
    }

    wl_status_t status = WiFi.status();
    if (!force && status == s_last_wifi_status) {
        return;
    }

    s_last_wifi_status = status;
    if (status == WL_CONNECTED) {
        char text[80];
        snprintf(text, sizeof(text), "WIFI STATUS STATE=CONNECTED IP=%s RSSI=%d",
                 WiFi.localIP().toString().c_str(), WiFi.RSSI());
        send_to_p4(text);
        debug_printf("[WIFI] status=WL_CONNECTED ip=%s rssi=%d\r\n",
                     WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
        send_to_p4("WIFI DISCONNECTED");
        debug_printf("[WIFI] status=%d disconnected\r\n", (int)status);
    }
}

static void start_wifi()
{
    s_wifi_enabled = true;
    s_wifi_config_reported = false;

    s_wifi_reconnect_retry = 0;
    s_wifi_reconnect_ms    = 0;
    s_wifi_heartbeat_ms    = 0;
    s_wifi_edge_connected  = false;

    if (!wifi_config_valid()) {
        report_wifi_status(true);
        return;
    }

    debug_println("[WIFI] connecting...");
    send_to_p4("WIFI CONNECTING");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    s_last_wifi_status = WL_NO_SHIELD;
    debug_printf("[WIFI] SSID=%s password_len=%u\r\n",
                 WIFI_SSID, (unsigned)strlen(WIFI_PASSWORD));
}

static void stop_wifi()
{
    s_wifi_enabled = false;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    s_last_wifi_status = WL_NO_SHIELD;
    send_to_p4("WIFI OFF");
}

static void sync_and_report_time()
{
    if (WiFi.status() != WL_CONNECTED) {
        send_to_p4("TIME WIFI NOT CONNECTED");
        return;
    }

    send_to_p4("TIME SYNCING");
    configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);

    struct tm time_info = {};
    if (!getLocalTime(&time_info, 5000)) {
        send_to_p4("TIME SYNC FAILED");
        return;
    }

    const time_t now = time(nullptr);
    if (now < 1704067200) {
        send_to_p4("TIME SYNC FAILED");
        return;
    }

    char text[40];
    snprintf(text, sizeof(text), "TIME UNIX=%lu", (unsigned long)now);
    send_to_p4(text);
}

static int next_uart_chunk_end(const String &text, int start, int max_bytes)
{
    const int length = text.length();
    int end = start + max_bytes;
    if (end >= length) {
        return length;
    }

    while (end > start && (((uint8_t)text[end]) & 0xC0) == 0x80) {
        end--;
    }

    return (end > start) ? end : start + max_bytes;
}

static void send_ai_answer(const String &answer)
{
    String safe_answer = answer;
    safe_answer.replace("\r", " ");
    safe_answer.replace("\n", " ");

    send_to_p4("AI BEGIN");

    const int max_chunk_bytes = 160;
    int pos = 0;
    while (pos < safe_answer.length()) {
        const int end = next_uart_chunk_end(safe_answer, pos, max_chunk_bytes);
        send_to_p4(String("AI TEXT ") + safe_answer.substring(pos, end));
        pos = end;
    }

    send_to_p4("AI END");
}

static void handle_ai_ask(String question)
{
    question.trim();
    if (question.length() == 0) {
        send_to_p4("AI ERROR QUESTION EMPTY");
        return;
    }

    if (!volcano_ai_is_configured()) {
        send_to_p4("AI ERROR API KEY MISSING");
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        send_to_p4("AI ERROR WIFI NOT CONNECTED");
        return;
    }

    send_to_p4("AI BUSY");
    String error_message;
    String answer = volcano_ai_ask(question, &error_message);
    if (answer.length() == 0) {
        if (error_message.length() == 0) {
            error_message = "AI ANSWER EMPTY";
        }
        error_message.replace("\r", " ");
        error_message.replace("\n", " ");
        send_to_p4(String("AI ERROR ") + error_message);
        return;
    }

    send_ai_answer(answer);
    String notice = build_guardian_ai_notice(question, answer);
    if (notice.length() == 0) {
        notice = "本次健康检测完成，请结合老人当下感受观察。";
    }
    send_wechat_notice("监护人健康提醒", notice.c_str());
}

/* ================================================================
 *  开机企业微信通知状态机  (millis()-driven, never blocks)
 * ================================================================ */
static void startup_wechat_notice_poll()
{
    if (!s_wechat_enabled) {
        s_sn_state = SN_DONE;
        return;
    }

    const bool wifi_ok = (s_wifi_enabled && WiFi.status() == WL_CONNECTED);
    const bool time_ok = (time(nullptr) > 1704067200);

    /* ---- detect WiFi just-connected edge ---- */
    if (wifi_ok && !s_sn_wifi_was_connected) {
        s_sn_wifi_was_connected = true;
        s_sn_wifi_since_ms   = millis();

        char ip_buf[24];
        snprintf(ip_buf, sizeof(ip_buf), "%s", WiFi.localIP().toString().c_str());
        debug_printf("[WIFI] connected, IP=%s\r\n", ip_buf);

        /* Start NTP sync in background (idempotent) */
        configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
    }
    if (!wifi_ok) {
        s_sn_wifi_was_connected = false;
    }

    /* ---- state machine ---- */
    switch (s_sn_state) {

    case SN_WAIT_WIFI:
        if (wifi_ok) {
            if (time_ok) {
                struct tm ti = {};
                getLocalTime(&ti, 0);
                debug_printf("[NTP] synced: %04d-%02d-%02d %02d:%02d:%02d\r\n",
                             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                             ti.tm_hour, ti.tm_min, ti.tm_sec);
            } else {
                debug_println("[NTP] syncing...");
            }
            s_sn_state = SN_WAIT_NTP;
        }
        break;

    case SN_WAIT_NTP:
        if (!wifi_ok) {
            s_sn_state = SN_WAIT_WIFI;
            break;
        }
        if (time_ok) {
            struct tm ti = {};
            getLocalTime(&ti, 0);
            debug_printf("[NTP] synced: %04d-%02d-%02d %02d:%02d:%02d\r\n",
                         ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                         ti.tm_hour, ti.tm_min, ti.tm_sec);
            s_sn_state = SN_SENDING;
        } else if (millis() - s_sn_wifi_since_ms >= 10000) {
            debug_println("[NTP] sync timeout, sending without time");
            s_sn_state = SN_SENDING;
        }
        break;

    case SN_SENDING: {
        if (!wifi_ok) {
            s_sn_state = SN_WAIT_WIFI;
            break;
        }

        s_sn_attempts++;

        /* Build markdown message */
        String md;
        md += "**健康监测网关已启动**\n";
        md += "> 设备：健康守护心率血氧检测装置\n";
        md += "> 时间：";

        if (time_ok) {
            struct tm ti = {};
            getLocalTime(&ti, 0);
            char time_buf[32];
            snprintf(time_buf, sizeof(time_buf),
                     "%04d-%02d-%02d %02d:%02d:%02d",
                     ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                     ti.tm_hour, ti.tm_min, ti.tm_sec);
            md += time_buf;
        } else {
            md += "时间未同步";
        }
        md += "\n> 状态：企业微信通知链路已启动";

        String error;
        debug_printf("[WECHAT] startup notice sending, attempt=%d\r\n", s_sn_attempts);
        if (wechat_push_send_markdown(WECHAT_WEBHOOK_URL, md, &error)) {
            debug_println("[WECHAT] startup notice sent successfully");
            s_sn_state = SN_DONE;
        } else {
            if (error.length() == 0) error = "unknown";
            debug_printf("[WECHAT] startup notice failed: %s\r\n", error.c_str());

            if (s_sn_attempts >= 3) {
                debug_println("[WECHAT] startup notice abandoned after 3 attempts");
                s_sn_state = SN_ABANDONED;
            } else {
                debug_printf("[WECHAT] retry %d/3 in 15s\r\n", s_sn_attempts + 1);
                s_sn_next_retry_ms = millis() + 15000;
                s_sn_state = SN_RETRY_WAIT;
            }
        }
        break;
    }

    case SN_RETRY_WAIT:
        if (!wifi_ok) {
            s_sn_state = SN_WAIT_WIFI;
            break;
        }
        if (millis() >= s_sn_next_retry_ms) {
            s_sn_state = SN_SENDING;
        }
        break;

    case SN_DONE:
    case SN_ABANDONED:
        /* No more actions this boot */
        break;
    }
}

static void handle_p4_line(const String &line)
{
    String command = line;

    if (is_recent_p4_tx_echo(command)) {
        Serial.print("[UART ECHO ignored] ");
        Serial.println(command);
        Serial0.print("[UART ECHO ignored] ");
        Serial0.println(command);
        return;
    }

    if (!is_known_p4_command(command)) {
        String recovered;
        if (recover_known_p4_command(command, &recovered)) {
            Serial.print("[UART RX recovered] ");
            Serial.print(command);
            Serial.print(" -> ");
            Serial.println(recovered);
            Serial0.print("[UART RX recovered] ");
            Serial0.print(command);
            Serial0.print(" -> ");
            Serial0.println(recovered);
            command = recovered;
        } else {
            Serial.print("[UART RX ignored] ");
            Serial.println(command);
            Serial0.print("[UART RX ignored] ");
            Serial0.println(command);
            return;
        }
    }

    Serial.print("[P4 <-] ");
    Serial.println(command);
    Serial0.print("[P4 <-] ");
    Serial0.println(command);

    if (command == "PING") {
        send_to_p4("PONG");
    } else if (command == "WIFI ON") {
        start_wifi();
    } else if (command == "WIFI OFF") {
        if (s_wifi_enabled) {
            stop_wifi();
        }
    } else if (command == "STATUS?") {
        report_wifi_status(true);
    } else if (command == "TIME?") {
        sync_and_report_time();
    } else if (command == "WECHAT ON") {
        s_wechat_enabled = true;
        send_to_p4("WECHAT STATUS ON");
        send_wechat_notice("健康监测提醒已开启",
                           "设备将推送检测完成、异常风险和联网状态变化提醒。");
    } else if (command == "WECHAT OFF") {
        send_wechat_notice("健康监测提醒已关闭",
                           "设备仍会在本地显示检测结果，但不会主动发送企业微信提醒。");
        s_wechat_enabled = false;
        send_to_p4("WECHAT STATUS OFF");
    } else if (command == "WECHAT STATUS?") {
        send_to_p4(s_wechat_enabled ? "WECHAT STATUS ON" : "WECHAT STATUS OFF");
    } else if (command == "CLOUD SYNC ON") {
        send_to_p4("CLOUD STATUS ON");
        send_wechat_notice("云端同步已开启",
                           "P4 检测数据将通过 C6 联网上传，并在 AI 诊断完成后推送摘要。");
    } else if (command == "CLOUD SYNC OFF") {
        send_wechat_notice("云端同步已关闭",
                           "设备将保留本地显示与历史记录，暂不主动上传云端。");
        send_to_p4("CLOUD STATUS OFF");
    } else if (command == "AI STATUS?") {
        if (!volcano_ai_is_configured()) {
            send_to_p4("AI ERROR API KEY MISSING");
        } else if (WiFi.status() != WL_CONNECTED) {
            send_to_p4("AI ERROR WIFI NOT CONNECTED");
        } else {
            send_to_p4("AI READY");
        }
    } else if (command.startsWith("AI ASK ")) {
        handle_ai_ask(command.substring(7));
    } else {
        // Ignore loopback or stale serial data.
    }
}

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    Serial.begin(115200);
    Serial0.begin(115200);
    delay(1000);

    // ---- 烧录成功标志:同时输出到原生USB(Serial)和UART0(Serial0)----
    // 每次重新烧录把下面的编号 #1 改成 #2、#3... 就能确认串口里跑的是不是最新固件
    const char *flash_banner = ">>> ESP32-C6 FLASH OK | build " __DATE__ " " __TIME__ " | long-ai-uart-v1 <<<";
    for (int i = 0; i < 3; i++) {
        Serial.println(flash_banner);
        Serial0.println(flash_banner);
    }
    // 上电闪灯3次:不依赖串口,用肉眼也能确认新固件已在运行
    for (int i = 0; i < 6; i++) {
        digitalWrite(LED_BUILTIN, (i % 2) ? LOW : HIGH);
        delay(150);
    }
    digitalWrite(LED_BUILTIN, LOW);

    Serial0.println();
    debug_println("=== ESP32-C6 APP IS RUNNING ===");

    P4Serial.setRxBufferSize(2048);
    P4Serial.begin(P4_UART_BAUD, SERIAL_8N1, P4_UART_RX_PIN, P4_UART_TX_PIN);

    debug_println("ESP32-C6 UART bridge test started");
    debug_printf("P4 UART test on RX=%d TX=%d baud=%lu\r\n",
                 P4_UART_RX_PIN, P4_UART_TX_PIN, (unsigned long)P4_UART_BAUD);

    // ---- 开机微信通知序列 ----
    debug_println("[BOOT] ESP32-C6 gateway starting");
    debug_println("[WECHAT] startup notice pending");

    // 自动启动 WiFi（不依赖 P4 发送 WIFI ON）
    start_wifi();
}

void loop()
{
    while (P4Serial.available() > 0) {
        String line = P4Serial.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            handle_p4_line(line);
        }
    }

    uint32_t now = millis();
    if (s_wifi_enabled && now - s_last_wifi_check_ms >= 1000) {
        s_last_wifi_check_ms = now;
        report_wifi_status(false);
    }

    /* ---- WiFi 心跳 + 自动重连 (millis()驱动) ---- */
    if (s_wifi_enabled) {
        const bool connected = (WiFi.status() == WL_CONNECTED);

        /* Edge: WiFi just connected */
        if (connected && !s_wifi_edge_connected) {
            s_wifi_edge_connected = true;
            s_wifi_reconnect_retry = 0;
            s_wifi_heartbeat_ms   = 0;  // force immediate heartbeat
        }
        /* Edge: WiFi just disconnected */
        if (!connected && s_wifi_edge_connected) {
            s_wifi_edge_connected = false;
            debug_println("[WIFI] disconnected");
        }

        /* 10s heartbeat when connected */
        if (connected && now - s_wifi_heartbeat_ms >= 10000) {
            s_wifi_heartbeat_ms = now;
            char buf[72];
            snprintf(buf, sizeof(buf), "WIFI STATUS STATE=CONNECTED IP=%s RSSI=%d",
                     WiFi.localIP().toString().c_str(), WiFi.RSSI());
            send_to_p4(buf);
            debug_printf("[WIFI] status sent, RSSI=%d dBm\r\n", WiFi.RSSI());
        }

        /* Auto-reconnect every 5s when disconnected */
        if (!connected && now - s_wifi_reconnect_ms >= 5000) {
            s_wifi_reconnect_retry++;
            s_wifi_reconnect_ms = now;

            char buf[48];
            snprintf(buf, sizeof(buf), "WIFI RECONNECTING RETRY=%d", s_wifi_reconnect_retry);
            send_to_p4(buf);
            debug_printf("[WIFI] reconnecting, retry=%d\r\n", s_wifi_reconnect_retry);

            WiFi.reconnect();
        }
    }

    /* 开机微信通知状态机 — millis()驱动，不阻塞 */
    if (s_sn_state != SN_DONE && s_sn_state != SN_ABANDONED) {
        startup_wechat_notice_poll();
    }
}
